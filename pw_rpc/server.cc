// Copyright 2020 The Pigweed Authors
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy of
// the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

// clang-format off
#include "pw_rpc/internal/log_config.h" // PW_LOG_* macros must be first.

#include "pw_rpc/server.h"
// clang-format on

#include <algorithm>

#include "pw_log/log.h"
#include "pw_rpc/internal/endpoint.h"
#include "pw_rpc/internal/packet.h"
#include "pw_rpc/service_id.h"

namespace pw::rpc {
namespace {

using internal::Packet;
using internal::pwpb::PacketType;

}  // namespace

Status Server::ProcessPacket(ConstByteSpan packet_data) {
  PW_TRY_ASSIGN(Packet packet,
                Endpoint::ProcessPacket(packet_data, Packet::kServer));
  return ProcessPacket(packet);
}

Status Server::ProcessPacket(internal::Packet packet) {
  internal::rpc_lock().lock();

  static constexpr bool kLogAllIncomingPackets = false;
  if constexpr (kLogAllIncomingPackets) {
    PW_LOG_INFO("RPC server received packet type %u for %u:%08x/%08x",
                static_cast<unsigned>(packet.type()),
                static_cast<unsigned>(packet.channel_id()),
                static_cast<unsigned>(packet.service_id()),
                static_cast<unsigned>(packet.method_id()));
  }

  internal::ChannelBase* channel = GetInternalChannel(packet.channel_id());
  if (channel == nullptr) {
    internal::rpc_lock().unlock();
    PW_LOG_WARN("RPC server received packet for unknown channel %u",
                static_cast<unsigned>(packet.channel_id()));
    return Status::Unavailable();
  }

  const auto [service, method] = FindMethodLocked(packet);

  if (method == nullptr) {
    // Don't send responses to errors to avoid infinite error cycles.
    if (packet.type() != PacketType::CLIENT_ERROR) {
      channel->Send(Packet::ServerError(packet, Status::NotFound()))
          .IgnoreError();
    }
    internal::rpc_lock().unlock();
    PW_LOG_DEBUG("Received packet on channel %u for unknown RPC %08x/%08x",
                 static_cast<unsigned>(packet.channel_id()),
                 static_cast<unsigned>(packet.service_id()),
                 static_cast<unsigned>(packet.method_id()));
    return OkStatus();  // OK since the packet was handled.
  }

  // Handle request packets separately to avoid an unnecessary call lookup. The
  // Call constructor looks up and cancels any duplicate calls.
  if (packet.type() == PacketType::REQUEST) {
    const internal::CallContext context(
        *this, packet.channel_id(), *service, *method, packet.call_id());
    method->Invoke(context, packet);
    return OkStatus();
  }

  IntrusiveList<internal::Call>::iterator call = FindCall(packet);

  switch (packet.type()) {
    case PacketType::CLIENT_STREAM:
      HandleClientStreamPacket(packet, *channel, call);
      break;
    case PacketType::CLIENT_ERROR:
      if (call != calls_end()) {
        PW_LOG_DEBUG("Server call %u for %u:%08x/%08x terminated with error %s",
                     static_cast<unsigned>(packet.call_id()),
                     static_cast<unsigned>(packet.channel_id()),
                     static_cast<unsigned>(packet.service_id()),
                     static_cast<unsigned>(packet.method_id()),
                     packet.status().str());
        call->HandleError(packet.status());
      } else {
        internal::rpc_lock().unlock();
      }
      break;
    case PacketType::CLIENT_REQUEST_COMPLETION:
      HandleCompletionRequest(packet, *channel, call);
      break;
    case PacketType::REQUEST:  // Handled above
    case PacketType::RESPONSE:
    case PacketType::SERVER_ERROR:
    case PacketType::SERVER_STREAM:
    default:
      internal::rpc_lock().unlock();
      PW_LOG_WARN("pw_rpc server unable to handle packet of type %u",
                  unsigned(packet.type()));
  }

  return OkStatus();  // OK since the packet was handled
}

std::tuple<Service*, const internal::Method*> Server::FindMethod(
    uint32_t service_id, uint32_t method_id) {
  internal::RpcLockGuard lock;
  return FindMethodLocked(service_id, method_id);
}

std::tuple<Service*, const internal::Method*> Server::FindMethodLocked(
    uint32_t service_id, uint32_t method_id) {
  auto service = std::find_if(services_.begin(), services_.end(), [&](auto& s) {
    return internal::UnwrapServiceId(s.service_id()) == service_id;
  });

  if (service == services_.end()) {
    return {};
  }

  return {&(*service), service->FindMethod(method_id)};
}

void Server::HandleCompletionRequest(
    const internal::Packet& packet,
    internal::ChannelBase& channel,
    IntrusiveList<internal::Call>::iterator call) const {
  if (call == calls_end()) {
    channel.Send(Packet::ServerError(packet, Status::FailedPrecondition()))
        .IgnoreError();  // Errors are logged in Channel::Send.
    internal::rpc_lock().unlock();
    PW_LOG_DEBUG(
        "Received a request completion packet for %u:%08x/%08x, which is not a"
        "pending call",
        static_cast<unsigned>(packet.channel_id()),
        static_cast<unsigned>(packet.service_id()),
        static_cast<unsigned>(packet.method_id()));
    return;
  }

  if (call->client_requested_completion()) {
    internal::rpc_lock().unlock();
    PW_LOG_DEBUG("Received multiple completion requests for %u:%08x/%08x",
                 static_cast<unsigned>(packet.channel_id()),
                 static_cast<unsigned>(packet.service_id()),
                 static_cast<unsigned>(packet.method_id()));
    return;
  }

  static_cast<internal::ServerCall&>(*call).HandleClientRequestedCompletion();
}

void Server::HandleClientStreamPacket(
    const internal::Packet& packet,
    internal::ChannelBase& channel,
    IntrusiveList<internal::Call>::iterator call) const {
  if (call == calls_end()) {
    channel.Send(Packet::ServerError(packet, Status::FailedPrecondition()))
        .IgnoreError();  // Errors are logged in Channel::Send.
    internal::rpc_lock().unlock();
    PW_LOG_DEBUG(
        "Received client stream packet for %u:%08x/%08x, which is not pending",
        static_cast<unsigned>(packet.channel_id()),
        static_cast<unsigned>(packet.service_id()),
        static_cast<unsigned>(packet.method_id()));
    return;
  }

  if (!call->has_client_stream()) {
    channel.Send(Packet::ServerError(packet, Status::InvalidArgument()))
        .IgnoreError();  // Errors are logged in Channel::Send.
    internal::rpc_lock().unlock();
    PW_LOG_DEBUG(
        "Received client stream packet for %u:%08x/%08x, which doesn't have a "
        "client stream",
        static_cast<unsigned>(packet.channel_id()),
        static_cast<unsigned>(packet.service_id()),
        static_cast<unsigned>(packet.method_id()));
    return;
  }

  if (call->client_requested_completion()) {
    channel.Send(Packet::ServerError(packet, Status::FailedPrecondition()))
        .IgnoreError();  // Errors are logged in Channel::Send.
    internal::rpc_lock().unlock();
    PW_LOG_DEBUG(
        "Received client stream packet for %u:%08x/%08x, but its client stream "
        "is closed",
        static_cast<unsigned>(packet.channel_id()),
        static_cast<unsigned>(packet.service_id()),
        static_cast<unsigned>(packet.method_id()));
    return;
  }

  call->HandlePayload(packet.payload());
}

}  // namespace pw::rpc
