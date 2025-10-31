// Copyright 2025 The Pigweed Authors
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

#include "pw_async2/channel.h"

#include "pw_allocator/testing.h"
#include "pw_async2/coro.h"
#include "pw_async2/coro_or_else_task.h"
#include "pw_async2/dispatcher.h"
#include "pw_async2/try.h"
#include "pw_containers/vector.h"
#include "pw_unit_test/framework.h"

namespace {

// DOCSTAG: [pw_async2-examples-channel-manual]
using pw::async2::experimental::ReceiveFuture;
using pw::async2::experimental::Receiver;
using pw::async2::experimental::Sender;
using pw::async2::experimental::SendFuture;

class Producer : public pw::async2::Task {
 public:
  explicit Producer(Sender<int>&& sender)
      : pw::async2::Task(PW_ASYNC_TASK_NAME("Producer")),
        sender_(std::move(sender)) {}

 private:
  pw::async2::Poll<> DoPend(pw::async2::Context& cx) override {
    while (data_ < 3) {
      if (!send_future_.has_value()) {
        send_future_.emplace(sender_.Send(data_));
      }
      PW_TRY_READY(send_future_->Pend(cx));
      send_future_.reset();
      ++data_;
    }
    sender_.Disconnect();
    return pw::async2::Ready();
  }

  int data_ = 0;
  Sender<int> sender_;
  std::optional<SendFuture<int>> send_future_;
};

class Consumer : public pw::async2::Task {
 public:
  explicit Consumer(Receiver<int>&& receiver)
      : pw::async2::Task(PW_ASYNC_TASK_NAME("Consumer")),
        receiver_(std::move(receiver)) {}

  const pw::Vector<int>& values() const { return values_; }

 private:
  pw::async2::Poll<> DoPend(pw::async2::Context& cx) override {
    while (true) {
      if (!receive_future_.has_value()) {
        receive_future_.emplace(receiver_.Receive());
      }
      PW_TRY_READY_ASSIGN(std::optional<int> result, receive_future_->Pend(cx));
      if (!result.has_value()) {
        break;
      }

      values_.push_back(*result);
      receive_future_.reset();
    }
    receiver_.Disconnect();
    return pw::async2::Ready();
  }

  Receiver<int> receiver_;
  std::optional<ReceiveFuture<int>> receive_future_;
  pw::Vector<int, 3> values_;
};
// DOCSTAG: [pw_async2-examples-channel-manual]

TEST(Channel, Manual) {
  pw::async2::experimental::ChannelStorage<int, 1> storage;
  auto [channel, sender, receiver] = CreateSpscChannel<int>(storage);

  // The returned channel handle is used to create senders and receivers.
  // Since this is a single producer single consumer channel, that isn't
  // possible, so its only other use is to manually close the channel.
  // We don't need that as we rely on automatic closing when the sender
  // completes.
  //
  // It is important to call `Release` once you are done with the handle to
  // prevent keeping the channel alive longer than needed.
  channel.Release();

  Producer producer(std::move(sender));
  Consumer consumer(std::move(receiver));

  pw::async2::Dispatcher dispatcher;

  dispatcher.Post(producer);
  dispatcher.Post(consumer);

  dispatcher.RunToCompletion();

  EXPECT_EQ(consumer.values().size(), 3u);
}

}  // namespace

namespace {

// DOCSTAG: [pw_async2-examples-channel-coro]
using pw::async2::Coro;
using pw::async2::CoroContext;
using pw::async2::experimental::Receiver;
using pw::async2::experimental::Sender;

Coro<pw::Status> CoroProducer(CoroContext&, Sender<int> sender) {
  for (int data = 0; data < 3; ++data) {
    co_await sender.Send(data);
  }
  co_return pw::OkStatus();
}

Coro<pw::Status> CoroConsumer(CoroContext&,
                              Receiver<int> receiver,
                              pw::Vector<int>& values) {
  while (true) {
    std::optional<int> result = co_await receiver.Receive();
    if (!result.has_value()) {
      break;
    }
    values.push_back(*result);
  }
  co_return pw::OkStatus();
}
// DOCSTAG: [pw_async2-examples-channel-coro]

TEST(Channel, Coro) {
  pw::allocator::test::AllocatorForTest<1024> alloc;
  pw::async2::Dispatcher dispatcher;
  pw::async2::CoroContext coro_cx(alloc);

  pw::async2::experimental::ChannelStorage<int, 1> storage;
  auto [channel, sender, receiver] = CreateSpscChannel<int>(storage);

  // The returned channel handle is used to create senders and receivers.
  // Since this is a single producer single consumer channel, that isn't
  // possible, so its only other use is to manually close the channel.
  // We don't need that as we rely on automatic closing when the sender
  // completes.
  //
  // It is important to call `Release` once you are done with the handle to
  // prevent keeping the channel alive longer than needed.
  channel.Release();

  pw::Vector<int, 3> values;
  auto producer = pw::async2::CoroOrElseTask(
      CoroProducer(coro_cx, std::move(sender)), [](pw::Status) {});
  auto consumer = pw::async2::CoroOrElseTask(
      CoroConsumer(coro_cx, std::move(receiver), values), [](pw::Status) {});

  dispatcher.Post(producer);
  dispatcher.Post(consumer);

  dispatcher.RunToCompletion();

  EXPECT_EQ(values.size(), 3u);
}

}  // namespace
