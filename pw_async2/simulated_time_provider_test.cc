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

#include "pw_async2/simulated_time_provider.h"

#include "pw_chrono/system_clock.h"
#include "pw_unit_test/framework.h"

namespace {

using ::pw::async2::Context;
using ::pw::async2::Dispatcher;
using ::pw::async2::Pending;
using ::pw::async2::Poll;
using ::pw::async2::Ready;
using ::pw::async2::SimulatedTimeProvider;
using ::pw::async2::Task;
using ::pw::async2::TimeFuture;
using ::pw::chrono::SystemClock;
using ::std::chrono_literals::operator""min;
using ::std::chrono_literals::operator""h;

// We can't control the SystemClock's period configuration, so just in case
// 42 hours cannot be accurately expressed in integer ticks, round the
// duration up.
constexpr SystemClock::duration kRoundedArbitraryDuration =
    SystemClock::for_at_least(42h);

TEST(SimulatedTimeProvider, InitialTime) {
  SimulatedTimeProvider<SystemClock> tp;

  EXPECT_EQ(SystemClock::time_point(SystemClock::duration(0)), tp.now());
}

TEST(SimulatedTimeProvider, SetTime) {
  SimulatedTimeProvider<SystemClock> tp;

  tp.SetTime(SystemClock::time_point(kRoundedArbitraryDuration));
  EXPECT_EQ(kRoundedArbitraryDuration, tp.now().time_since_epoch());
}

TEST(SimulatedTimeProvider, AdvanceTime) {
  SimulatedTimeProvider<SystemClock> tp;

  const SystemClock::time_point before_timestamp = tp.now();
  tp.AdvanceTime(kRoundedArbitraryDuration);
  const SystemClock::time_point after_timestamp = tp.now();

  EXPECT_EQ(kRoundedArbitraryDuration, tp.now().time_since_epoch());
  EXPECT_EQ(kRoundedArbitraryDuration, after_timestamp - before_timestamp);
}

struct WaitTask : public Task {
  WaitTask(TimeFuture<SystemClock>&& future) : future_(std::move(future)) {}

  Poll<> DoPend(Context& cx) final {
    if (future_.Pend(cx).IsPending()) {
      return Pending();
    }
    return Ready();
  }
  TimeFuture<SystemClock> future_;
};

TEST(SimulatedTimeProvider, AdvanceTimeRunsPastTimers) {
  SimulatedTimeProvider<SystemClock> tp;
  WaitTask task(tp.WaitFor(1h));
  Dispatcher dispatcher;
  dispatcher.Post(task);
  tp.AdvanceTime(30min);
  EXPECT_EQ(dispatcher.RunUntilStalled(), Pending());
  tp.AdvanceTime(40min);
  EXPECT_EQ(dispatcher.RunUntilStalled(), Ready());
}

}  // namespace