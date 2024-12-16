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

#include "pw_async/fake_dispatcher.h"
#include "pw_async2/pend_func_task.h"
#include "pw_async2/poll.h"
#include "pw_bluetooth_sapphire/internal/host/gap/fake_adapter.h"
#include "pw_multibuf/simple_allocator_for_test.h"
#include "pw_unit_test/framework.h"

namespace {

using pw::bluetooth_sapphire::Central;
using ScanStartResult = Central::ScanStartResult;
using Pending = pw::async2::PendingType;
using Ready = pw::async2::ReadyType;
using Context = pw::async2::Context;
using ScanHandle = Central::ScanHandle;
using ScanResult = Central::ScanResult;
using pw::async2::PendFuncTask;
using pw::async2::Poll;
using pw::chrono::SystemClock;
using ScanFilter = Central::ScanFilter;

const bt::DeviceAddress kAddress0(bt::DeviceAddress::Type::kLEPublic, {0});
const bt::StaticByteBuffer kAdvDataWithName(0x05,  // length
                                            0x09,  // type (name)
                                            'T',
                                            'e',
                                            's',
                                            't');

const pw::bluetooth::Uuid kUuid1(1);
const bt::StaticByteBuffer kAdvDataWithUuid1(
    0x05, bt::DataType::kIncomplete16BitServiceUuids, 0x01, 0x00, 0x00, 0x00);

auto MakePendResultTask(
    ScanHandle::Ptr& scan_handle,
    std::optional<pw::Result<ScanResult>>& scan_result_out) {
  return PendFuncTask([&scan_handle, &scan_result_out](Context& cx) -> Poll<> {
    Poll<pw::Result<ScanResult>> pend = scan_handle->PendResult(cx);
    if (pend.IsPending()) {
      return Pending();
    }
    scan_result_out = std::move(pend.value());
    return Ready();
  });
}

class CentralTest : public ::testing::Test {
 public:
  ScanHandle::Ptr Scan(Central::ScanOptions& options) {
    pw::async2::OnceReceiver<ScanStartResult> scan_receiver =
        central().Scan(options);

    std::optional<pw::Result<ScanStartResult>> scan_pend_result;
    PendFuncTask scan_receiver_task(
        [&scan_receiver, &scan_pend_result](Context& cx) -> Poll<> {
          Poll<pw::Result<ScanStartResult>> scan_pend = scan_receiver.Pend(cx);
          if (scan_pend.IsPending()) {
            return Pending();
          }
          scan_pend_result = std::move(scan_pend.value());
          return Ready();
        });
    async2_dispatcher().Post(scan_receiver_task);
    EXPECT_FALSE(scan_pend_result.has_value());

    async_dispatcher().RunUntilIdle();
    EXPECT_TRUE(async2_dispatcher().RunUntilStalled().IsReady());

    if (!scan_pend_result.has_value()) {
      ADD_FAILURE();
      return nullptr;
    }
    if (!scan_pend_result.value().ok()) {
      ADD_FAILURE();
      return nullptr;
    }
    ScanStartResult scan_start_result =
        std::move(scan_pend_result.value().value());
    if (!scan_start_result.has_value()) {
      ADD_FAILURE();
      return nullptr;
    }
    return std::move(scan_start_result.value());
  }

  bt::gap::testing::FakeAdapter& adapter() { return adapter_; }
  bt::gap::PeerCache& peer_cache() { return *adapter_.peer_cache(); }
  pw::bluetooth_sapphire::Central& central() { return central_; }
  pw::async::test::FakeDispatcher& async_dispatcher() {
    return async_dispatcher_;
  }
  pw::async2::Dispatcher& async2_dispatcher() { return async2_dispatcher_; }

 private:
  pw::async::test::FakeDispatcher async_dispatcher_;
  pw::async2::Dispatcher async2_dispatcher_;
  bt::gap::testing::FakeAdapter adapter_{async_dispatcher_};

  pw::multibuf::test::SimpleAllocatorForTest</*kDataSizeBytes=*/2024,
                                             /*kMetaSizeBytes=*/2024>
      multibuf_allocator_;
  Central central_{
      adapter_.AsWeakPtr(), async_dispatcher_, multibuf_allocator_};
};

TEST_F(CentralTest, ScanOneResultSuccess) {
  Central::ScanOptions options;
  options.scan_type = Central::ScanType::kActiveUsePublicAddress;
  // Don't filter results.
  std::array<Central::ScanFilter, 1> filters{Central::ScanFilter{}};
  options.filters = filters;

  ScanHandle::Ptr scan_handle = Scan(options);
  ASSERT_TRUE(scan_handle);
  ASSERT_EQ(adapter().fake_le()->discovery_sessions().size(), 1u);
  EXPECT_TRUE((*adapter().fake_le()->discovery_sessions().cbegin())->active());

  std::optional<pw::Result<ScanResult>> scan_result_result;
  PendFuncTask scan_handle_task =
      MakePendResultTask(scan_handle, scan_result_result);
  async2_dispatcher().Post(scan_handle_task);
  EXPECT_TRUE(async2_dispatcher().RunUntilStalled().IsPending());

  const bool connectable = true;
  bt::gap::Peer* peer = peer_cache().NewPeer(kAddress0, connectable);
  const int rssi = 5;
  SystemClock::time_point timestamp(SystemClock::duration(5));
  peer->MutLe().SetAdvertisingData(rssi, kAdvDataWithName, timestamp);

  adapter().fake_le()->NotifyScanResult(*peer);

  EXPECT_TRUE(async2_dispatcher().RunUntilStalled().IsReady());
  ASSERT_TRUE(scan_result_result.has_value());
  ASSERT_TRUE(scan_result_result.value().ok());

  ScanResult scan_result = std::move(scan_result_result.value().value());
  scan_result_result.reset();
  EXPECT_EQ(scan_result.peer_id, peer->identifier().value());
  EXPECT_EQ(scan_result.connectable, connectable);
  EXPECT_EQ(scan_result.rssi, rssi);
  EXPECT_EQ(scan_result.last_updated, timestamp);
  ASSERT_EQ(scan_result.data.size(), kAdvDataWithName.size());
  ASSERT_TRUE(scan_result.data.IsContiguous());
  for (size_t i = 0; i < kAdvDataWithName.size(); i++) {
    EXPECT_EQ(scan_result.data.ContiguousSpan().value()[i],
              kAdvDataWithName.subspan()[i]);
  }
  ASSERT_TRUE(scan_result.name.has_value());
  EXPECT_EQ(scan_result.name.value(), "Test");

  // No more scan results should be received.
  async2_dispatcher().Post(scan_handle_task);
  EXPECT_TRUE(async2_dispatcher().RunUntilStalled().IsPending());
  EXPECT_FALSE(scan_result_result.has_value());
  scan_handle_task.Deregister();
}

TEST_F(CentralTest, ScanResultDoesNotMatchFilter) {
  Central::ScanOptions options;
  options.scan_type = Central::ScanType::kActiveUsePublicAddress;
  Central::ScanFilter filter;
  filter.name = "different-name";
  std::array<Central::ScanFilter, 1> filters{filter};
  options.filters = filters;

  ScanHandle::Ptr scan_handle = Scan(options);
  ASSERT_TRUE(scan_handle);

  std::optional<pw::Result<ScanResult>> scan_result_result;
  PendFuncTask scan_handle_task =
      MakePendResultTask(scan_handle, scan_result_result);
  async2_dispatcher().Post(scan_handle_task);

  const bool connectable = true;
  bt::gap::Peer* peer = peer_cache().NewPeer(kAddress0, connectable);
  const int rssi = 5;
  SystemClock::time_point timestamp(SystemClock::duration(5));
  peer->MutLe().SetAdvertisingData(rssi, kAdvDataWithName, timestamp);

  adapter().fake_le()->NotifyScanResult(*peer);

  async_dispatcher().RunUntilIdle();
  EXPECT_TRUE(async2_dispatcher().RunUntilStalled().IsPending());
  EXPECT_FALSE(scan_result_result.has_value());
  scan_handle_task.Deregister();
}

TEST_F(CentralTest, ScanResultMatchesSecondFilterOnly) {
  Central::ScanOptions options;
  options.scan_type = Central::ScanType::kActiveUsePublicAddress;
  ScanFilter filter_0;
  filter_0.service_uuid = pw::bluetooth::Uuid(2);
  ScanFilter filter_1;
  filter_1.service_uuid = kUuid1;
  std::array<ScanFilter, 2> filters{filter_0, filter_1};
  options.filters = filters;

  ScanHandle::Ptr scan_handle = Scan(options);
  ASSERT_TRUE(scan_handle);
  ASSERT_EQ(adapter().fake_le()->discovery_sessions().size(), 1u);
  EXPECT_TRUE((*adapter().fake_le()->discovery_sessions().cbegin())->active());

  std::optional<pw::Result<ScanResult>> scan_result_result;
  PendFuncTask scan_handle_task =
      MakePendResultTask(scan_handle, scan_result_result);
  async2_dispatcher().Post(scan_handle_task);
  EXPECT_TRUE(async2_dispatcher().RunUntilStalled().IsPending());

  const bool connectable = false;
  bt::gap::Peer* peer = peer_cache().NewPeer(kAddress0, connectable);
  const int rssi = 6;
  SystemClock::time_point timestamp(SystemClock::duration(6));
  peer->MutLe().SetAdvertisingData(rssi, kAdvDataWithUuid1, timestamp);

  adapter().fake_le()->NotifyScanResult(*peer);

  EXPECT_TRUE(async2_dispatcher().RunUntilStalled().IsReady());
  ASSERT_TRUE(scan_result_result.has_value());
  ASSERT_TRUE(scan_result_result.value().ok());

  ScanResult scan_result = std::move(scan_result_result.value().value());
  scan_result_result.reset();
  EXPECT_EQ(scan_result.peer_id, peer->identifier().value());
  EXPECT_EQ(scan_result.connectable, connectable);
  EXPECT_EQ(scan_result.rssi, rssi);
  EXPECT_EQ(scan_result.last_updated, timestamp);
  ASSERT_EQ(scan_result.data.size(), kAdvDataWithName.size());
  ASSERT_TRUE(scan_result.data.IsContiguous());
  for (size_t i = 0; i < kAdvDataWithUuid1.size(); i++) {
    EXPECT_EQ(scan_result.data.ContiguousSpan().value()[i],
              kAdvDataWithUuid1.subspan()[i]);
  }
  EXPECT_FALSE(scan_result.name.has_value());
}

TEST_F(CentralTest, CachedScanResult) {
  const bool connectable = true;
  bt::gap::Peer* peer = peer_cache().NewPeer(kAddress0, connectable);
  const int rssi = 5;
  SystemClock::time_point timestamp(SystemClock::duration(5));
  peer->MutLe().SetAdvertisingData(rssi, kAdvDataWithName, timestamp);
  adapter().fake_le()->AddCachedScanResult(peer->identifier());

  Central::ScanOptions options;
  options.scan_type = Central::ScanType::kActiveUsePublicAddress;
  // Don't filter results.
  std::array<ScanFilter, 1> filters{ScanFilter{}};
  options.filters = filters;

  ScanHandle::Ptr scan_handle = Scan(options);
  ASSERT_TRUE(scan_handle);
  ASSERT_EQ(adapter().fake_le()->discovery_sessions().size(), 1u);
  EXPECT_TRUE((*adapter().fake_le()->discovery_sessions().cbegin())->active());

  std::optional<pw::Result<ScanResult>> scan_result_result;
  PendFuncTask scan_handle_task =
      MakePendResultTask(scan_handle, scan_result_result);
  async2_dispatcher().Post(scan_handle_task);

  EXPECT_TRUE(async2_dispatcher().RunUntilStalled().IsReady());
  ASSERT_TRUE(scan_result_result.has_value());
  ASSERT_TRUE(scan_result_result.value().ok());

  ScanResult scan_result = std::move(scan_result_result.value().value());
  EXPECT_EQ(scan_result.peer_id, peer->identifier().value());
  EXPECT_EQ(scan_result.connectable, connectable);
  EXPECT_EQ(scan_result.rssi, rssi);
  EXPECT_EQ(scan_result.last_updated, timestamp);
  ASSERT_EQ(scan_result.data.size(), kAdvDataWithName.size());
  ASSERT_TRUE(scan_result.data.IsContiguous());
  for (size_t i = 0; i < kAdvDataWithName.size(); i++) {
    EXPECT_EQ(scan_result.data.ContiguousSpan().value()[i],
              kAdvDataWithName.subspan()[i]);
  }
  ASSERT_TRUE(scan_result.name.has_value());
  EXPECT_EQ(scan_result.name.value(), "Test");
}

TEST_F(CentralTest, ScanErrorReceivedByScanHandle) {
  Central::ScanOptions options;
  options.scan_type = Central::ScanType::kActiveUsePublicAddress;
  // Don't filter results.
  std::array<Central::ScanFilter, 1> filters{Central::ScanFilter{}};
  options.filters = filters;

  ScanHandle::Ptr scan_handle = Scan(options);
  ASSERT_TRUE(scan_handle);
  ASSERT_EQ(adapter().fake_le()->discovery_sessions().size(), 1u);
  EXPECT_TRUE((*adapter().fake_le()->discovery_sessions().cbegin())->active());

  std::optional<pw::Result<ScanResult>> scan_result_result;
  PendFuncTask scan_handle_task =
      MakePendResultTask(scan_handle, scan_result_result);
  async2_dispatcher().Post(scan_handle_task);
  EXPECT_TRUE(async2_dispatcher().RunUntilStalled().IsPending());

  (*adapter().fake_le()->discovery_sessions().cbegin())->NotifyError();

  EXPECT_TRUE(async2_dispatcher().RunUntilStalled().IsReady());
  ASSERT_TRUE(scan_result_result.has_value());
  EXPECT_TRUE(scan_result_result.value().status().IsCancelled());
}

TEST_F(CentralTest, ScanWithoutFiltersFails) {
  Central::ScanOptions options;
  options.scan_type = Central::ScanType::kActiveUsePublicAddress;
  options.filters = {};

  pw::async2::OnceReceiver<ScanStartResult> scan_receiver =
      central().Scan(options);

  std::optional<pw::Result<ScanStartResult>> scan_pend_result;
  PendFuncTask scan_receiver_task(
      [&scan_receiver, &scan_pend_result](Context& cx) -> Poll<> {
        Poll<pw::Result<ScanStartResult>> scan_pend = scan_receiver.Pend(cx);
        if (scan_pend.IsPending()) {
          return Pending();
        }
        scan_pend_result = std::move(scan_pend.value());
        return Ready();
      });
  async2_dispatcher().Post(scan_receiver_task);
  EXPECT_TRUE(async2_dispatcher().RunUntilStalled().IsReady());
  ASSERT_TRUE(scan_pend_result.has_value());
  ASSERT_TRUE(scan_pend_result.value().ok());
  ScanStartResult scan_start_result =
      std::move(scan_pend_result.value().value());
  ASSERT_FALSE(scan_start_result.has_value());
  EXPECT_EQ(scan_start_result.error(),
            Central::StartScanError::kInvalidParameters);
}

TEST_F(CentralTest, QueueMoreThanMaxScanResultsInScanHandleDropsOldest) {
  Central::ScanOptions options;
  options.scan_type = Central::ScanType::kActiveUsePublicAddress;
  // Don't filter results.
  std::array<Central::ScanFilter, 1> filters{Central::ScanFilter{}};
  options.filters = filters;

  ScanHandle::Ptr scan_handle = Scan(options);
  ASSERT_TRUE(scan_handle);
  ASSERT_EQ(adapter().fake_le()->discovery_sessions().size(), 1u);
  EXPECT_TRUE((*adapter().fake_le()->discovery_sessions().cbegin())->active());

  std::vector<pw::Result<ScanResult>> scan_result_results;
  PendFuncTask scan_handle_task =
      PendFuncTask([&scan_handle, &scan_result_results](Context& cx) -> Poll<> {
        while (true) {
          Poll<pw::Result<ScanResult>> pend = scan_handle->PendResult(cx);
          if (pend.IsPending()) {
            return Pending();
          }
          scan_result_results.emplace_back(std::move(pend.value()));
          if (!scan_result_results.back().ok()) {
            return Ready();
          }
        }
      });
  async2_dispatcher().Post(scan_handle_task);
  EXPECT_TRUE(async2_dispatcher().RunUntilStalled().IsPending());

  const bool connectable = true;
  bt::gap::Peer* peer = peer_cache().NewPeer(kAddress0, connectable);
  SystemClock::time_point timestamp(SystemClock::duration(5));

  // Queue 1 more than the max queue size. Put the index in the rssi field.
  for (int i = 0; i < Central::kMaxScanResultsQueueSize + 1; i++) {
    peer->MutLe().SetAdvertisingData(/*rssi=*/i, kAdvDataWithName, timestamp);
    adapter().fake_le()->NotifyScanResult(*peer);
  }

  EXPECT_TRUE(async2_dispatcher().RunUntilStalled().IsPending());
  scan_handle_task.Deregister();
  ASSERT_EQ(scan_result_results.size(), Central::kMaxScanResultsQueueSize);
  // The first scan result should have been dropped.
  for (int i = 0; i < Central::kMaxScanResultsQueueSize; i++) {
    ASSERT_TRUE(scan_result_results[i].ok());
    EXPECT_EQ(scan_result_results[i].value().rssi, i + 1);
  }
}

}  // namespace