// Copyright 2024 The Pigweed Authors
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

#include "pw_bluetooth_sapphire/central.h"

namespace pw::bluetooth_sapphire {
namespace {

pw::sync::Mutex g_peripheral_lock;

bt::UUID UuidFrom(const pw::bluetooth::Uuid& uuid) {
  return bt::UUID(bt::BufferView(pw::as_bytes(uuid.As128BitSpan())));
}

bt::gap::DiscoveryFilter DiscoveryFilterFrom(const Central::ScanFilter& in) {
  bt::gap::DiscoveryFilter out;
  if (in.service_uuid.has_value()) {
    bt::UUID uuid = UuidFrom(in.service_uuid.value());
    out.set_service_uuids(std::vector<bt::UUID>{std::move(uuid)});
  }
  if (in.service_data_uuid.has_value()) {
    bt::UUID uuid = UuidFrom(in.service_data_uuid.value());
    out.set_service_data_uuids(std::vector<bt::UUID>{std::move(uuid)});
  }
  if (in.manufacturer_id.has_value()) {
    out.set_manufacturer_code(in.manufacturer_id.value());
  }
  if (in.connectable.has_value()) {
    out.set_connectable(in.connectable.value());
  }
  if (in.name.has_value()) {
    out.set_name_substring(std::string(in.name.value()));
  }
  if (in.max_path_loss.has_value()) {
    out.set_pathloss(in.max_path_loss.value());
  }
  return out;
}

std::optional<Central::ScanResult> ScanResultFrom(
    const bt::gap::Peer& peer, pw::multibuf::MultiBufAllocator& allocator) {
  Central::ScanResult out;
  out.peer_id = peer.identifier().value();
  // TODO: https://pwbug.dev/377301546 - Report the "connectable" value of this
  // advertisement, not the Peer's dual-mode connectability.
  out.connectable = peer.connectable();
  out.rssi = peer.rssi();

  if (!peer.le()->parsed_advertising_data_timestamp().has_value()) {
    bt_log(DEBUG, "api", "failed to get advertising data time");
    return std::nullopt;
  }
  out.last_updated = peer.le()->parsed_advertising_data_timestamp().value();

  bt::BufferView data_view = peer.le()->advertising_data();
  std::optional<pw::multibuf::MultiBuf> data =
      allocator.Allocate(data_view.size());
  if (!data) {
    bt_log(DEBUG, "api", "failed to allocate buffer for advertising data");
    return std::nullopt;
  }
  StatusWithSize copy_status = data->CopyFrom(data_view.subspan());
  if (!copy_status.ok()) {
    bt_log(DEBUG,
           "api",
           "failed to copy scan result data: %s",
           copy_status.status().str());
    return std::nullopt;
  }
  out.data = std::move(data.value());

  if (peer.name().has_value()) {
    out.name.emplace();
    Status append_status =
        pw::string::Append(out.name.value(), peer.name().value());
    // RESOURCE_EXHAUSTED means that the name was truncated, which is OK.
    if (!append_status.ok() && !append_status.IsResourceExhausted()) {
      bt_log(DEBUG,
             "api",
             "failed to set scan result name: %s",
             append_status.str());
      return std::nullopt;
    }
  }

  return out;
}

}  // namespace

Central::Central(bt::gap::Adapter::WeakPtr adapter,
                 pw::async::Dispatcher& dispatcher,
                 pw::multibuf::MultiBufAllocator& allocator)
    : adapter_(std::move(adapter)),
      dispatcher_(dispatcher),
      allocator_(allocator),
      weak_factory_(this),
      self_(weak_factory_.GetWeakPtr()) {}

async2::OnceReceiver<Central::ConnectResult> Central::Connect(
    pw::bluetooth::PeerId,
    bluetooth::low_energy::Connection2::ConnectionOptions) {
  // TODO: https://pwbug.dev/377301546 - Implement Connect
  return async2::OnceReceiver<ConnectResult>(
      pw::unexpected(ConnectError::kCouldNotBeEstablished));
}

async2::OnceReceiver<Central::ScanStartResult> Central::Scan(
    ScanOptions options) {
  // TODO: https://pwbug.dev/377301546 - Support the different types of active
  // scans.
  bool active = (options.scan_type != ScanType::kPassive);

  if (options.filters.empty()) {
    return async2::OnceReceiver<ScanStartResult>(
        pw::unexpected(StartScanError::kInvalidParameters));
  }

  // Convert options to filters now because options contains non-owning views
  // that won't be valid in callbacks.
  std::vector<bt::gap::DiscoveryFilter> discovery_filters;
  for (const ScanFilter& filter : options.filters) {
    discovery_filters.emplace_back(DiscoveryFilterFrom(filter));
  }

  auto [result_sender, result_receiver] =
      async2::MakeOnceSenderAndReceiver<Central::ScanStartResult>();

  auto callback =
      [self = self_,
       sender = std::move(result_sender),
       filters = std::move(discovery_filters)](
          std::unique_ptr<bt::gap::LowEnergyDiscoverySession> session) mutable {
        // callback will always be run on the Bluetooth thread

        if (!self.is_alive()) {
          sender.emplace(pw::unexpected(StartScanError::kInternal));
          return;
        }

        if (!session) {
          bt_log(WARN, "api", "failed to start LE discovery session");
          sender.emplace(pw::unexpected(StartScanError::kInternal));
          return;
        }

        uint16_t scan_id = self->next_scan_id_++;
        ScanHandleImpl* scan_handle_raw_ptr =
            new ScanHandleImpl(scan_id, &self.get());
        ScanHandle::Ptr scan_handle_ptr(scan_handle_raw_ptr);
        {
          std::lock_guard guard(lock());
          auto [iter, emplaced] = self->scans_.try_emplace(scan_id,
                                                           std::move(session),
                                                           std::move(filters),
                                                           scan_handle_raw_ptr,
                                                           scan_id,
                                                           &self.get());
          PW_CHECK(emplaced);
        }

        sender.emplace(std::move(scan_handle_ptr));
      };

  async::TaskFunction task_fn =
      [self = self_, active, cb = std::move(callback)](async::Context&,
                                                       Status status) mutable {
        if (status.ok()) {
          // TODO: https://pwbug.dev/377301546 - Support configuring interval,
          // window, and PHY.
          self->adapter_->le()->StartDiscovery(active, std::move(cb));
        }
      };
  Status post_status = dispatcher_.Post(std::move(task_fn));
  PW_CHECK_OK(post_status);

  return std::move(result_receiver);
}

pw::sync::Mutex& Central::lock() { return g_peripheral_lock; }

void Central::ScanHandleImpl::QueueScanResultLocked(ScanResult&& result) {
  if (results_.size() == kMaxScanResultsQueueSize) {
    results_.pop();
  }
  results_.push(std::move(result));
  std::move(waker_).Wake();
}

async2::Poll<pw::Result<Central::ScanResult>>
Central::ScanHandleImpl::PendResult(async2::Context& cx) {
  std::lock_guard guard(lock());
  if (!results_.empty()) {
    ScanResult result = std::move(results_.front());
    results_.pop();
    return async2::Ready(std::move(result));
  }

  if (!central_) {
    return async2::Ready(pw::Status::Cancelled());
  }

  PW_ASYNC_STORE_WAKER(cx, waker_, "scan result");
  return async2::Pending();
}

Central::ScanState::ScanState(
    std::unique_ptr<bt::gap::LowEnergyDiscoverySession> session,
    std::vector<bt::gap::DiscoveryFilter> filters,
    ScanHandleImpl* scan_handle,
    uint16_t scan_id,
    Central* central)
    : scan_id_(scan_id),
      scan_handle_(scan_handle),
      central_(central),
      session_(std::move(session)),
      filters_(std::move(filters)) {
  session_->SetResultCallback(
      [this](const bt::gap::Peer& peer) { OnScanResult(peer); });
  session_->set_error_callback([this]() { OnError(); });
}

void Central::ScanState::OnScanResult(const bt::gap::Peer& peer) {
  // TODO: https://pwbug.dev/377301546 - Getting only a Peer as a scan result is
  // awkward. Update LowEnergyDiscoverySession to give us the actual
  // LowEnergyScanResult.
  std::lock_guard guard(lock());
  if (!scan_handle_) {
    return;
  }
  for (const bt::gap::DiscoveryFilter& filter : filters_) {
    if (filter.MatchLowEnergyResult(peer.le()->parsed_advertising_data(),
                                    peer.connectable(),
                                    peer.rssi())) {
      std::optional<Central::ScanResult> scan_result =
          ScanResultFrom(peer, central_->allocator_);
      if (!scan_result.has_value()) {
        return;
      }
      scan_handle_->QueueScanResultLocked(std::move(scan_result.value()));
      scan_handle_->WakeLocked();
      return;
    }
  }
}

void Central::ScanState::OnError() {
  std::lock_guard guard(lock());
  if (scan_handle_) {
    scan_handle_->OnScanErrorLocked();
    scan_handle_ = nullptr;
  }
  central_->scans_.erase(scan_id_);
  // This object has been destroyed.
}

}  // namespace pw::bluetooth_sapphire