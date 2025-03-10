// Copyright 2023 The Pigweed Authors
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

#include "pw_bluetooth_sapphire/internal/host/gap/bredr_discovery_manager.h"

#include "pw_bluetooth_sapphire/internal/host/gap/peer_cache.h"
#include "pw_bluetooth_sapphire/internal/host/hci-spec/protocol.h"
#include "pw_bluetooth_sapphire/internal/host/testing/controller_test.h"
#include "pw_bluetooth_sapphire/internal/host/testing/inspect.h"
#include "pw_bluetooth_sapphire/internal/host/testing/mock_controller.h"
#include "pw_bluetooth_sapphire/internal/host/testing/test_helpers.h"
#include "pw_bluetooth_sapphire/internal/host/testing/test_packets.h"

namespace bt::gap {
namespace {

using namespace inspect::testing;

using TestingBase =
    bt::testing::FakeDispatcherControllerTest<bt::testing::MockController>;

// clang-format off
#define COMMAND_COMPLETE_RSP(opcode)                                         \
  StaticByteBuffer(hci_spec::kCommandCompleteEventCode, 0x04, 0xF0, \
                                 LowerBits((opcode)), UpperBits((opcode)),   \
                                 pw::bluetooth::emboss::StatusCode::SUCCESS)

#define COMMAND_STATUS_RSP(opcode, statuscode)                       \
  StaticByteBuffer( hci_spec::kCommandStatusEventCode, 0x04, \
                                 (statuscode), 0xF0,                 \
                                 LowerBits((opcode)), UpperBits((opcode)))
// clang-format on

const auto kWriteInquiryActivity =
    testing::WriteInquiryScanActivity(kInquiryScanInterval, kInquiryScanWindow);

const auto kWriteInquiryActivityRsp =
    testing::CommandCompletePacket(hci_spec::kWriteInquiryScanActivity);

const StaticByteBuffer kWriteInquiryType(
    LowerBits(hci_spec::kWriteInquiryScanType),
    UpperBits(hci_spec::kWriteInquiryScanType),
    0x01,  // Param total size
    0x01   // Interlaced Inquiry Scan
);

const auto kWriteInquiryTypeRsp =
    COMMAND_COMPLETE_RSP(hci_spec::kWriteInquiryScanType);

class BrEdrDiscoveryManagerTest : public TestingBase {
 public:
  BrEdrDiscoveryManagerTest() = default;
  ~BrEdrDiscoveryManagerTest() override = default;

  void SetUp() override {
    TestingBase::SetUp();

    NewDiscoveryManager(pw::bluetooth::emboss::InquiryMode::STANDARD);
  }

  void TearDown() override {
    discovery_manager_ = nullptr;
    TestingBase::TearDown();
  }

  void NewDiscoveryManager(pw::bluetooth::emboss::InquiryMode mode) {
    // We expect to set the Inquiry Scan and the Type when we start.
    EXPECT_CMD_PACKET_OUT(
        test_device(), kWriteInquiryActivity, &kWriteInquiryActivityRsp);
    EXPECT_CMD_PACKET_OUT(
        test_device(), kWriteInquiryType, &kWriteInquiryTypeRsp);

    discovery_manager_ = std::make_unique<BrEdrDiscoveryManager>(
        dispatcher(),
        transport()->command_channel()->AsWeakPtr(),
        mode,
        &peer_cache_);

    RunUntilIdle();
  }

  void DestroyDiscoveryManager() { discovery_manager_.reset(); }

  PeerCache* peer_cache() { return &peer_cache_; }

 protected:
  BrEdrDiscoveryManager* discovery_manager() const {
    return discovery_manager_.get();
  }

 private:
  PeerCache peer_cache_{dispatcher()};
  std::unique_ptr<BrEdrDiscoveryManager> discovery_manager_;

  BT_DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BrEdrDiscoveryManagerTest);
};

using GAP_BrEdrDiscoveryManagerTest = BrEdrDiscoveryManagerTest;

// Suffix DeathTest has GoogleTest-specific behavior
using BrEdrDiscoveryManagerDeathTest = BrEdrDiscoveryManagerTest;

const auto kInquiry = testing::InquiryCommandPacket();

const auto kWriteLocalNameRsp = testing::CommandCompletePacket(
    hci_spec::kWriteLocalName, pw::bluetooth::emboss::StatusCode::SUCCESS);

const auto kWriteLocalNameRspError = testing::CommandCompletePacket(
    hci_spec::kWriteLocalName,
    pw::bluetooth::emboss::StatusCode::HARDWARE_FAILURE);

const auto kWriteExtendedInquiryResponseRsp =
    testing::CommandCompletePacket(hci_spec::kWriteExtendedInquiryResponse,
                                   pw::bluetooth::emboss::StatusCode::SUCCESS);

const auto kWriteExtendedInquiryResponseRspError =
    testing::CommandCompletePacket(
        hci_spec::kWriteExtendedInquiryResponse,
        pw::bluetooth::emboss::StatusCode::HARDWARE_FAILURE);

const auto kInquiryRsp = testing::CommandStatusPacket(
    hci_spec::kInquiry, pw::bluetooth::emboss::StatusCode::SUCCESS);

const auto kInquiryRspError = testing::CommandStatusPacket(
    hci_spec::kInquiry, pw::bluetooth::emboss::StatusCode::HARDWARE_FAILURE);

const StaticByteBuffer kInquiryComplete(
    hci_spec::kInquiryCompleteEventCode,
    0x01,  // parameter_total_size (1 bytes)
    pw::bluetooth::emboss::StatusCode::SUCCESS);

const StaticByteBuffer kInquiryCompleteError(
    hci_spec::kInquiryCompleteEventCode,
    0x01,  // parameter_total_size (1 bytes)
    pw::bluetooth::emboss::StatusCode::HARDWARE_FAILURE);

#define BD_ADDR(addr1) addr1, 0x00, 0x00, 0x00, 0x00, 0x00

const DeviceAddress kDeviceAddress1(DeviceAddress::Type::kBREDR,
                                    {BD_ADDR(0x01)});
const DeviceAddress kLeAliasAddress1(DeviceAddress::Type::kLEPublic,
                                     kDeviceAddress1.value());
const DeviceAddress kDeviceAddress2(DeviceAddress::Type::kBREDR,
                                    {BD_ADDR(0x02)});
const DeviceAddress kLeAliasAddress2(DeviceAddress::Type::kLEPublic,
                                     kDeviceAddress2.value());
const DeviceAddress kDeviceAddress3(DeviceAddress::Type::kBREDR,
                                    {BD_ADDR(0x03)});
const DeviceAddress kLeAliasAddress3(DeviceAddress::Type::kLEPublic,
                                     kDeviceAddress3.value());

// clang-format off
const StaticByteBuffer kInquiryResult(
  hci_spec::kInquiryResultEventCode,
  0x0F, // parameter_total_size (15 bytes)
  0x01, // num_responses
  BD_ADDR(0x01), // bd_addr[0]
  0x00, // page_scan_repetition_mode[0] (R0)
  0x00, // unused / reserved
  0x00, // unused / reserved
  0x00, 0x1F, 0x00, // class_of_device[0] (unspecified)
  0x00, 0x00 // clock_offset[0]
);

const StaticByteBuffer kInquiryResultIncompleteHeader(
  hci_spec::kInquiryResultEventCode,
  0x00 // parameter_total_size (0 bytes)
  // truncated
);

const StaticByteBuffer kInquiryResultMissingResponses(
  hci_spec::kInquiryResultEventCode,
  0x1D, // parameter_total_size (29 bytes)
  0x03, // num_responses (only two responses are packed)

  // first response
  BD_ADDR(0x01), // bd_addr[0]
  0x00, // page_scan_repetition_mode[0] (R0)
  0x00, // unused / reserved
  0x00, // unused / reserved
  0x00, 0x1F, 0x00, // class_of_device[0] (unspecified)
  0x00, 0x00, // clock_offset[0]

  // second response
  BD_ADDR(0x02), // bd_addr[0]
  0x00, // page_scan_repetition_mode[0] (R0)
  0x00, // unused / reserved
  0x00, // unused / reserved
  0x00, 0x1F, 0x00, // class_of_device[0] (unspecified)
  0x00, 0x00 // clock_offset[0]
);

const StaticByteBuffer kInquiryResultIncompleteResponse(
  hci_spec::kInquiryResultEventCode,
  0x15, // parameter_total_size (21 bytes)
  0x02, // num_responses

  // first response
  BD_ADDR(0x01), // bd_addr[0]
  0x00, // page_scan_repetition_mode[0] (R0)
  0x00, // unused / reserved
  0x00, // unused / reserved
  0x00, 0x1F, 0x00, // class_of_device[0] (unspecified)
  0x00, 0x00, // clock_offset[0]

  // second response
  BD_ADDR(0x02) // bd_addr[0]
  // truncated
);

const StaticByteBuffer kRSSIInquiryResult(
  hci_spec::kInquiryResultWithRSSIEventCode,
  0x0F, // parameter_total_size (15 bytes)
  0x01, // num_responses
  BD_ADDR(0x02), // bd_addr[0]
  0x00, // page_scan_repetition_mode[0] (R0)
  0x00, // unused / reserved
  0x00, 0x1F, 0x00, // class_of_device[0] (unspecified)
  0x00, 0x00, // clock_offset[0]
  0xEC // RSSI (-20dBm)
);

#define REMOTE_NAME_REQUEST(addr1) StaticByteBuffer( \
    LowerBits(hci_spec::kRemoteNameRequest), UpperBits(hci_spec::kRemoteNameRequest), \
    0x0a, /* parameter_total_size (10 bytes) */ \
    BD_ADDR(addr1),  /* BD_ADDR */ \
    0x00, 0x00, 0x00, 0x80 /* page_scan_repetition_mode, 0, clock_offset */ \
);

const auto kRemoteNameRequest1 = REMOTE_NAME_REQUEST(0x01)
const auto kRemoteNameRequest2 = REMOTE_NAME_REQUEST(0x02)

#undef REMOTE_NAME_REQUEST

const auto kRemoteNameRequestRsp =
    COMMAND_STATUS_RSP(hci_spec::kRemoteNameRequest, pw::bluetooth::emboss::StatusCode::SUCCESS);

#undef COMMAND_STATUS_RSP

const auto kRemoteNameRequestComplete1 = testing::RemoteNameRequestCompletePacket(
    kDeviceAddress1, {'F',    'u',    'c',    'h',    's',    'i',    'a',    '\xF0', '\x9F',
                      '\x92', '\x96', '\x00', '\x14', '\x15', '\x16', '\x17', '\x18', '\x19',
                      '\x1a', '\x1b', '\x1c', '\x1d', '\x1e', '\x1f', '\x20'}
    // remote name (Fuchsia💖)
    // Everything after the 0x00 should be ignored.
);
const auto kRemoteNameRequestComplete2 =
    testing::RemoteNameRequestCompletePacket(kDeviceAddress2, "Sapphire");

const StaticByteBuffer kExtendedInquiryResult(
  hci_spec::kExtendedInquiryResultEventCode,
  0xFF, // parameter_total_size (255 bytes)
  0x01, // num_responses
  BD_ADDR(0x03),  // bd_addr
  0x00, // page_scan_repetition_mode (R0)
  0x00, // unused / reserved
  0x00, 0x1F, 0x00, // class_of_device (unspecified)
  0x00, 0x00, // clock_offset
  0xEC, // RSSI (-20dBm)
  // Extended Inquiry Response (240 bytes total)
  // Complete Local Name (12 bytes): Fuchsia 💖
  0x0C, 0x09, 'F', 'u', 'c', 'h', 's', 'i', 'a', 0xF0, 0x9F, 0x92, 0x96,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00
);

#undef BD_ADDR

const StaticByteBuffer kSetExtendedMode(
    LowerBits(hci_spec::kWriteInquiryMode), UpperBits(hci_spec::kWriteInquiryMode),
    0x01, // parameter_total_size
    0x02 // Extended Inquiry Result or Inquiry Result with RSSI
);

const auto kSetExtendedModeRsp = COMMAND_COMPLETE_RSP(hci_spec::kWriteInquiryMode);

const StaticByteBuffer kWriteLocalName(
  LowerBits(hci_spec::kWriteLocalName), UpperBits(hci_spec::kWriteLocalName),
  0xF8, // parameter_total_size (248 bytes)
  // Complete Local Name ()
  'A', 'B', 'C', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00
);

const StaticByteBuffer kWriteExtendedInquiryResponse(
  LowerBits(hci_spec::kWriteExtendedInquiryResponse),
  UpperBits(hci_spec::kWriteExtendedInquiryResponse),
  0xF1, // parameter_total_size (241 bytes)
  0x00, // fec_required
  0x04, // name_length + 1
  0x09, // DataType::kCompleteLocalName,
  // Complete Local Name (3 bytes + 1 byte null terminator + 234 bytes of zero padding)
  'A', 'B', 'C', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00
);

const auto kReadScanEnable = testing::ReadScanEnable();
const auto kReadScanEnableRspNone = testing::ReadScanEnableResponse(0x00);
const auto kReadScanEnableRspInquiry = testing::ReadScanEnableResponse(0x01);
const auto kReadScanEnableRspPage = testing::ReadScanEnableResponse(0x02);
const auto kReadScanEnableRspBoth = testing::ReadScanEnableResponse(0x03);

const auto kWriteScanEnableNone = testing::WriteScanEnable(0x00);
const auto kWriteScanEnableInq = testing::WriteScanEnable(0x01);
const auto kWriteScanEnablePage = testing::WriteScanEnable(0x02);
const auto kWriteScanEnableBoth = testing::WriteScanEnable(0x03);
const auto kWriteScanEnableRsp = testing::CommandCompletePacket(hci_spec::kWriteScanEnable);

#undef COMMAND_COMPLETE_RSP
// clang-format on

// Test: malformed inquiry result is fatal
TEST_F(BrEdrDiscoveryManagerDeathTest,
       MalformedInquiryResultFromControllerIsFatal) {
  EXPECT_CMD_PACKET_OUT(test_device(), hci_spec::kInquiry, &kInquiryRsp);

  std::unique_ptr<BrEdrDiscoverySession> session;

  discovery_manager()->RequestDiscovery(
      [&session](auto status, auto cb_session) {
        EXPECT_EQ(fit::ok(), status);
        session = std::move(cb_session);
      });

  RunUntilIdle();

  for (auto event : {kInquiryResultIncompleteHeader.view(),
                     kInquiryResultMissingResponses.view(),
                     kInquiryResultIncompleteResponse.view()}) {
    EXPECT_DEATH_IF_SUPPORTED(
        (test_device()->SendCommandChannelPacket(event), RunUntilIdle()), ".*");
  }
}

// Test: discovering() answers correctly

// Test: requesting discovery should start inquiry
// Test: Inquiry Results that come in when there is discovery get reported up
// correctly to the sessions
// Test: Peers discovered are reported to the cache
// Test: RemoteNameRequest is processed correctly
// Test: Inquiry Results that come in when there's no discovery happening get
// discarded.
TEST_F(BrEdrDiscoveryManagerTest, RequestDiscoveryAndDrop) {
  EXPECT_CMD_PACKET_OUT(
      test_device(), hci_spec::kInquiry, &kInquiryRsp, &kInquiryResult);
  EXPECT_CMD_PACKET_OUT(test_device(),
                        kRemoteNameRequest1,
                        &kRemoteNameRequestRsp,
                        &kRemoteNameRequestComplete1);

  std::unique_ptr<BrEdrDiscoverySession> session;
  size_t peers_found = 0u;

  discovery_manager()->RequestDiscovery(
      [&session, &peers_found](auto status, auto cb_session) {
        EXPECT_EQ(fit::ok(), status);
        cb_session->set_result_callback(
            [&peers_found](const auto&) { peers_found++; });
        session = std::move(cb_session);
      });

  EXPECT_FALSE(discovery_manager()->discovering());

  RunUntilIdle();

  EXPECT_EQ(1u, peers_found);
  EXPECT_TRUE(discovery_manager()->discovering());

  EXPECT_CMD_PACKET_OUT(
      test_device(), hci_spec::kInquiry, &kInquiryRsp, &kInquiryResult);

  test_device()->SendCommandChannelPacket(kInquiryComplete);

  RunUntilIdle();

  // Confirm that post-inquiry peer name request is processed correctly.
  Peer* peer = peer_cache()->FindByAddress(kDeviceAddress1);
  ASSERT_TRUE(peer);
  EXPECT_EQ("Fuchsia💖", *peer->name());
  EXPECT_EQ(Peer::NameSource::kNameDiscoveryProcedure, *peer->name_source());

  EXPECT_EQ(2u, peers_found);

  // TODO(fxbug.dev/42145646): test InquiryCancel when it is implemented

  session = nullptr;
  test_device()->SendCommandChannelPacket(kInquiryResult);

  RunUntilIdle();

  EXPECT_EQ(2u, peers_found);
  EXPECT_FALSE(discovery_manager()->discovering());

  test_device()->SendCommandChannelPacket(kInquiryComplete);
  RunUntilIdle();
}

// Test: requesting a second discovery should start a session without sending
// any more HCI commands.
// Test: dropping the first discovery shouldn't stop inquiry
// Test: starting two sessions at once should only start inquiry once
TEST_F(BrEdrDiscoveryManagerTest, MultipleRequests) {
  EXPECT_CMD_PACKET_OUT(
      test_device(), hci_spec::kInquiry, &kInquiryRsp, &kInquiryResult);
  EXPECT_CMD_PACKET_OUT(test_device(),
                        kRemoteNameRequest1,
                        &kRemoteNameRequestRsp,
                        &kRemoteNameRequestComplete1);

  std::unique_ptr<BrEdrDiscoverySession> session1;
  size_t peers_found1 = 0u;

  discovery_manager()->RequestDiscovery(
      [&session1, &peers_found1](auto status, auto cb_session) {
        EXPECT_EQ(fit::ok(), status);
        cb_session->set_result_callback(
            [&peers_found1](const auto&) { peers_found1++; });
        session1 = std::move(cb_session);
      });

  EXPECT_FALSE(discovery_manager()->discovering());

  RunUntilIdle();

  EXPECT_TRUE(session1);
  EXPECT_EQ(1u, peers_found1);
  EXPECT_TRUE(discovery_manager()->discovering());

  std::unique_ptr<BrEdrDiscoverySession> session2;
  size_t peers_found2 = 0u;

  discovery_manager()->RequestDiscovery(
      [&session2, &peers_found2](auto status, auto cb_session) {
        EXPECT_EQ(fit::ok(), status);
        cb_session->set_result_callback(
            [&peers_found2](const auto&) { peers_found2++; });
        session2 = std::move(cb_session);
      });

  RunUntilIdle();

  EXPECT_TRUE(session2);
  EXPECT_EQ(1u, peers_found1);
  EXPECT_EQ(0u, peers_found2);
  EXPECT_TRUE(discovery_manager()->discovering());

  test_device()->SendCommandChannelPacket(kInquiryResult);

  RunUntilIdle();

  EXPECT_EQ(2u, peers_found1);
  EXPECT_EQ(1u, peers_found2);

  session1 = nullptr;

  RunUntilIdle();

  test_device()->SendCommandChannelPacket(kInquiryResult);

  RunUntilIdle();

  EXPECT_EQ(2u, peers_found1);
  EXPECT_EQ(2u, peers_found2);

  // TODO(fxbug.dev/42145646): test InquiryCancel when it is implemented

  session2 = nullptr;

  test_device()->SendCommandChannelPacket(kInquiryResult);

  RunUntilIdle();

  EXPECT_EQ(2u, peers_found1);
  EXPECT_EQ(2u, peers_found2);

  EXPECT_FALSE(discovery_manager()->discovering());

  test_device()->SendCommandChannelPacket(kInquiryComplete);

  RunUntilIdle();
}

// Test: starting a session "while" the other one is stopping a session should
// still restart the Inquiry.
// Test: starting a session "while" the other one is stopping should return
// without needing an InquiryComplete first.
// Test: we should only request a peer's name if it's the first time we
// encounter it.
TEST_F(BrEdrDiscoveryManagerTest, RequestDiscoveryWhileStop) {
  EXPECT_CMD_PACKET_OUT(test_device(), kInquiry, &kInquiryRsp, &kInquiryResult);
  EXPECT_CMD_PACKET_OUT(test_device(),
                        kRemoteNameRequest1,
                        &kRemoteNameRequestRsp,
                        &kRemoteNameRequestComplete1);

  std::unique_ptr<BrEdrDiscoverySession> session1;
  size_t peers_found1 = 0u;

  discovery_manager()->RequestDiscovery(
      [&session1, &peers_found1](auto status, auto cb_session) {
        EXPECT_EQ(fit::ok(), status);
        cb_session->set_result_callback(
            [&peers_found1](const auto&) { peers_found1++; });
        session1 = std::move(cb_session);
      });

  EXPECT_FALSE(discovery_manager()->discovering());

  RunUntilIdle();

  EXPECT_TRUE(session1);
  EXPECT_EQ(1u, peers_found1);
  EXPECT_TRUE(discovery_manager()->discovering());

  // Drop the active session.
  session1 = nullptr;
  RunUntilIdle();

  std::unique_ptr<BrEdrDiscoverySession> session2;
  size_t peers_found2 = 0u;
  discovery_manager()->RequestDiscovery(
      [&session2, &peers_found2](auto status, auto cb_session) {
        EXPECT_EQ(fit::ok(), status);
        cb_session->set_result_callback(
            [&peers_found2](const auto&) { peers_found2++; });
        session2 = std::move(cb_session);
      });

  // The new session should be started at this point, and inquiry results
  // returned.
  EXPECT_TRUE(session2);
  test_device()->SendCommandChannelPacket(kInquiryResult);

  RunUntilIdle();

  EXPECT_EQ(1u, peers_found2);

  // Inquiry should be restarted when the Complete comes in because an active
  // session2 still exists.
  // TODO(fxbug.dev/42145646): test InquiryCancel when it is implemented
  EXPECT_CMD_PACKET_OUT(test_device(), kInquiry, &kInquiryRsp, &kInquiryResult);
  test_device()->SendCommandChannelPacket(kInquiryComplete);

  RunUntilIdle();

  EXPECT_EQ(1u, peers_found1);
  EXPECT_EQ(2u, peers_found2);
  EXPECT_TRUE(discovery_manager()->discovering());

  test_device()->SendCommandChannelPacket(kInquiryResult);

  RunUntilIdle();

  EXPECT_EQ(1u, peers_found1);
  EXPECT_EQ(3u, peers_found2);

  // TODO(fxbug.dev/42145646): test InquiryCancel when it is implemented
  session2 = nullptr;

  // After the session is dropped, even if another result comes in, no results
  // are sent to the callback.
  test_device()->SendCommandChannelPacket(kInquiryResult);

  RunUntilIdle();

  EXPECT_EQ(1u, peers_found1);
  EXPECT_EQ(3u, peers_found2);
}

// Test: When Inquiry Fails to start, we report this back to the requester.
TEST_F(BrEdrDiscoveryManagerTest, RequestDiscoveryError) {
  EXPECT_CMD_PACKET_OUT(
      test_device(), kInquiry, &kInquiryRspError, &kInquiryResult);
  EXPECT_CMD_PACKET_OUT(test_device(),
                        kRemoteNameRequest1,
                        &kRemoteNameRequestRsp,
                        &kRemoteNameRequestComplete1);

  std::unique_ptr<BrEdrDiscoverySession> session;

  discovery_manager()->RequestDiscovery([](auto status, auto cb_session) {
    EXPECT_TRUE(status.is_error());
    EXPECT_FALSE(cb_session);
    EXPECT_EQ(ToResult(pw::bluetooth::emboss::StatusCode::HARDWARE_FAILURE),
              status);
  });

  EXPECT_FALSE(discovery_manager()->discovering());

  RunUntilIdle();

  EXPECT_FALSE(discovery_manager()->discovering());
}

// Test: When inquiry complete indicates failure, we signal to the current
// sessions.
TEST_F(BrEdrDiscoveryManagerTest, ContinuingDiscoveryError) {
  EXPECT_CMD_PACKET_OUT(test_device(), kInquiry, &kInquiryRsp, &kInquiryResult);
  EXPECT_CMD_PACKET_OUT(test_device(),
                        kRemoteNameRequest1,
                        &kRemoteNameRequestRsp,
                        &kRemoteNameRequestComplete1);

  std::unique_ptr<BrEdrDiscoverySession> session;
  size_t peers_found = 0u;
  bool error_callback = false;

  discovery_manager()->RequestDiscovery(
      [&session, &peers_found, &error_callback](auto status, auto cb_session) {
        EXPECT_EQ(fit::ok(), status);
        cb_session->set_result_callback(
            [&peers_found](const auto&) { peers_found++; });
        cb_session->set_error_callback(
            [&error_callback]() { error_callback = true; });
        session = std::move(cb_session);
      });

  EXPECT_FALSE(discovery_manager()->discovering());

  RunUntilIdle();

  EXPECT_EQ(1u, peers_found);
  EXPECT_TRUE(discovery_manager()->discovering());

  test_device()->SendCommandChannelPacket(kInquiryCompleteError);

  RunUntilIdle();

  EXPECT_TRUE(error_callback);
  EXPECT_FALSE(discovery_manager()->discovering());

  session = nullptr;

  RunUntilIdle();
}

// clang-format off
const StaticByteBuffer kWriteLocalNameMaxLen(
  LowerBits(hci_spec::kWriteLocalName), UpperBits(hci_spec::kWriteLocalName),
  0xF8, // parameter_total_size (248 bytes)
  // Complete Local Name (exactly 248 bytes)
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W'
);

const StaticByteBuffer kWriteExtInquiryResponseMaxLen(
  LowerBits(hci_spec::kWriteExtendedInquiryResponse),
  UpperBits(hci_spec::kWriteExtendedInquiryResponse),
  0xF1, // parameter_total_size (241 bytes)
  0x00, // fec_required
  0xEF, // 239 bytes (1 + 238 bytes)
  0x08, // DataType::kShortenedLocalName,
  // Shortened Local Name (238 bytes, truncated from above)
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M'
);
// clang-format on

// Test: UpdateLocalName successfully sends hci command, and further calls
// UpdateEIRResponseData (private). Ensures the name is updated at the very end.
TEST_F(BrEdrDiscoveryManagerTest, UpdateLocalNameShortenedSuccess) {
  EXPECT_CMD_PACKET_OUT(test_device(), kWriteLocalNameMaxLen, );

  // Set the status to be an arbitrary invalid status.
  hci::Result<> result =
      ToResult(pw::bluetooth::emboss::StatusCode::PAIRING_NOT_ALLOWED);
  size_t callback_count = 0u;
  auto name_cb = [&result, &callback_count](const auto& status) {
    EXPECT_EQ(fit::ok(), status);
    callback_count++;
    result = status;
  };
  std::string kNewName = "";
  while (kNewName.length() < 225) {
    kNewName.append("ABCDEFGHIJKLMNOPQRSTUVWXY");
  }
  kNewName.append("ABCDEFGHIJKLMNOPQRSTUVW");
  discovery_manager()->UpdateLocalName(kNewName, name_cb);

  RunUntilIdle();

  // Local name should not be set, callback shouldn't be called yet.
  EXPECT_NE(kNewName, discovery_manager()->local_name());
  EXPECT_EQ(0u, callback_count);

  test_device()->SendCommandChannelPacket(kWriteLocalNameRsp);
  EXPECT_CMD_PACKET_OUT(test_device(), kWriteExtInquiryResponseMaxLen, );

  RunUntilIdle();

  // Still waiting on EIR response.
  // Local name should not be set, callback shouldn't be called yet.
  EXPECT_NE(kNewName, discovery_manager()->local_name());
  EXPECT_EQ(0u, callback_count);

  test_device()->SendCommandChannelPacket(kWriteExtendedInquiryResponseRsp);

  RunUntilIdle();

  EXPECT_EQ(kNewName, discovery_manager()->local_name());
  EXPECT_EQ(ToResult(pw::bluetooth::emboss::StatusCode::SUCCESS), result);
  EXPECT_EQ(1u, callback_count);
}

// Test: UpdateLocalName successfully sends hci command, and further calls
// UpdateEIRResponseData (private). Ensures the name is updated at the very end.
TEST_F(BrEdrDiscoveryManagerTest, UpdateLocalNameSuccess) {
  EXPECT_CMD_PACKET_OUT(test_device(), kWriteLocalName, );

  // Set the status to be an arbitrary invalid status.
  hci::Result<> result =
      ToResult(pw::bluetooth::emboss::StatusCode::PAIRING_NOT_ALLOWED);
  size_t callback_count = 0u;
  auto name_cb = [&result, &callback_count](const auto& status) {
    EXPECT_EQ(fit::ok(), status);
    callback_count++;
    result = status;
  };
  const std::string kNewName = "ABC";
  discovery_manager()->UpdateLocalName(kNewName, name_cb);

  RunUntilIdle();

  // Local name should not be set, callback shouldn't be called yet.
  EXPECT_NE(kNewName, discovery_manager()->local_name());
  EXPECT_EQ(0u, callback_count);

  test_device()->SendCommandChannelPacket(kWriteLocalNameRsp);
  EXPECT_CMD_PACKET_OUT(test_device(), kWriteExtendedInquiryResponse, );

  RunUntilIdle();

  // Still waiting on EIR response.
  // Local name should not be set, callback shouldn't be called yet.
  EXPECT_NE(kNewName, discovery_manager()->local_name());
  EXPECT_EQ(0u, callback_count);

  test_device()->SendCommandChannelPacket(kWriteExtendedInquiryResponseRsp);

  RunUntilIdle();

  EXPECT_EQ(kNewName, discovery_manager()->local_name());
  EXPECT_EQ(ToResult(pw::bluetooth::emboss::StatusCode::SUCCESS), result);
  EXPECT_EQ(1u, callback_count);
}

// Test: UpdateLocalName passes back error code through the callback and
// |local_name_| does not get updated.
TEST_F(BrEdrDiscoveryManagerTest, UpdateLocalNameError) {
  EXPECT_CMD_PACKET_OUT(test_device(), kWriteLocalName, );

  // Set the status to be an arbitrary invalid status.
  hci::Result<> result =
      ToResult(pw::bluetooth::emboss::StatusCode::UNSUPPORTED_REMOTE_FEATURE);
  size_t callback_count = 0u;
  auto name_cb = [&result, &callback_count](const auto& status) {
    EXPECT_TRUE(status.is_error());
    callback_count++;
    result = status;
  };
  const std::string kNewName = "ABC";
  discovery_manager()->UpdateLocalName(kNewName, name_cb);

  RunUntilIdle();

  // Local name should not be set, callback shouldn't be called yet.
  EXPECT_NE(kNewName, discovery_manager()->local_name());
  EXPECT_EQ(0u, callback_count);

  // Send a response error.
  test_device()->SendCommandChannelPacket(kWriteLocalNameRspError);

  RunUntilIdle();

  // |local_name_| should not be updated, return status should be error.
  EXPECT_NE(kNewName, discovery_manager()->local_name());
  EXPECT_EQ(ToResult(pw::bluetooth::emboss::StatusCode::HARDWARE_FAILURE),
            result);
  EXPECT_EQ(1u, callback_count);
}

// Test: UpdateLocalName should succeed, but UpdateEIRResponseData should fail.
// Consequently, the |local_name_| should not be updated, and the callback
// should return the error.
TEST_F(BrEdrDiscoveryManagerTest, UpdateEIRResponseDataError) {
  EXPECT_CMD_PACKET_OUT(test_device(), kWriteLocalName, );

  // Set the status to be an arbitrary invalid status.
  hci::Result<> result =
      ToResult(pw::bluetooth::emboss::StatusCode::UNSUPPORTED_REMOTE_FEATURE);
  size_t callback_count = 0u;
  auto name_cb = [&result, &callback_count](const auto& status) {
    EXPECT_TRUE(status.is_error());
    callback_count++;
    result = status;
  };
  const std::string kNewName = "ABC";
  discovery_manager()->UpdateLocalName(kNewName, name_cb);

  RunUntilIdle();

  // Local name should not be set, callback shouldn't be called yet.
  EXPECT_NE(kNewName, discovery_manager()->local_name());
  EXPECT_EQ(0u, callback_count);

  // kWriteLocalName should succeed.
  test_device()->SendCommandChannelPacket(kWriteLocalNameRsp);
  EXPECT_CMD_PACKET_OUT(test_device(), kWriteExtendedInquiryResponse, );

  RunUntilIdle();

  // Still waiting on EIR response.
  // Local name should not be set, callback shouldn't be called yet.
  EXPECT_NE(kNewName, discovery_manager()->local_name());
  EXPECT_EQ(0u, callback_count);

  // kWriteExtendedInquiryResponse should fail.
  test_device()->SendCommandChannelPacket(
      kWriteExtendedInquiryResponseRspError);

  RunUntilIdle();

  // |local_name_| should not be updated, return status should be error.
  EXPECT_NE(kNewName, discovery_manager()->local_name());
  EXPECT_EQ(ToResult(pw::bluetooth::emboss::StatusCode::HARDWARE_FAILURE),
            result);
  EXPECT_EQ(1u, callback_count);
}

// Test: requesting discoverable works
// Test: requesting discoverable while discoverable is pending doesn't send
// any more HCI commands
TEST_F(BrEdrDiscoveryManagerTest, DiscoverableSet) {
  EXPECT_CMD_PACKET_OUT(test_device(), kReadScanEnable, );

  std::vector<std::unique_ptr<BrEdrDiscoverableSession>> sessions;
  auto session_cb = [&sessions](auto status, auto cb_session) {
    EXPECT_EQ(fit::ok(), status);
    sessions.emplace_back(std::move(cb_session));
  };

  discovery_manager()->RequestDiscoverable(session_cb);

  RunUntilIdle();

  EXPECT_EQ(0u, sessions.size());
  EXPECT_FALSE(discovery_manager()->discoverable());

  EXPECT_CMD_PACKET_OUT(test_device(), kWriteScanEnableInq, );

  test_device()->SendCommandChannelPacket(kReadScanEnableRspNone);

  RunUntilIdle();

  // Request another session while the first is pending.
  discovery_manager()->RequestDiscoverable(session_cb);

  test_device()->SendCommandChannelPacket(kWriteScanEnableRsp);

  RunUntilIdle();

  EXPECT_EQ(2u, sessions.size());
  EXPECT_TRUE(discovery_manager()->discoverable());

  discovery_manager()->RequestDiscoverable(session_cb);

  EXPECT_EQ(3u, sessions.size());
  EXPECT_TRUE(discovery_manager()->discoverable());

  EXPECT_CMD_PACKET_OUT(
      test_device(), kReadScanEnable, &kReadScanEnableRspInquiry);
  EXPECT_CMD_PACKET_OUT(
      test_device(), kWriteScanEnableNone, &kWriteScanEnableRsp);

  sessions.clear();

  RunUntilIdle();

  EXPECT_FALSE(discovery_manager()->discoverable());
}

// Test: requesting discoverable while discovery is disabling leaves
// the discoverable enabled and reports success
// Test: enable/disable while page scan is enabled works.
TEST_F(BrEdrDiscoveryManagerTest, DiscoverableRequestWhileStopping) {
  EXPECT_CMD_PACKET_OUT(
      test_device(), kReadScanEnable, &kReadScanEnableRspPage);
  EXPECT_CMD_PACKET_OUT(
      test_device(), kWriteScanEnableBoth, &kWriteScanEnableRsp);

  std::vector<std::unique_ptr<BrEdrDiscoverableSession>> sessions;
  auto session_cb = [&sessions](auto status, auto cb_session) {
    EXPECT_EQ(fit::ok(), status);
    sessions.emplace_back(std::move(cb_session));
  };

  discovery_manager()->RequestDiscoverable(session_cb);

  RunUntilIdle();

  EXPECT_EQ(1u, sessions.size());
  EXPECT_TRUE(discovery_manager()->discoverable());

  EXPECT_CMD_PACKET_OUT(test_device(), kReadScanEnable, );

  sessions.clear();

  RunUntilIdle();

  // Request a new discovery before the procedure finishes.
  // This will queue another ReadScanEnable just in case the disable write is
  // in progress.
  EXPECT_CMD_PACKET_OUT(test_device(), kReadScanEnable, );
  discovery_manager()->RequestDiscoverable(session_cb);

  test_device()->SendCommandChannelPacket(kReadScanEnableRspBoth);

  // This shouldn't send any WriteScanEnable because we're already in the right
  // mode (MockController will assert if we do as it's not expecting)
  RunUntilIdle();

  EXPECT_EQ(1u, sessions.size());
  EXPECT_TRUE(discovery_manager()->discoverable());

  // If somehow the scan got turned off, we will still turn it back on.
  EXPECT_CMD_PACKET_OUT(
      test_device(), kWriteScanEnableBoth, &kWriteScanEnableRsp);
  test_device()->SendCommandChannelPacket(kReadScanEnableRspPage);

  RunUntilIdle();

  EXPECT_EQ(1u, sessions.size());
  EXPECT_TRUE(discovery_manager()->discoverable());

  EXPECT_CMD_PACKET_OUT(
      test_device(), kReadScanEnable, &kReadScanEnableRspBoth);
  EXPECT_CMD_PACKET_OUT(
      test_device(), kWriteScanEnablePage, &kWriteScanEnableRsp);

  sessions.clear();

  RunUntilIdle();

  EXPECT_FALSE(discovery_manager()->discoverable());
}

// Test: non-standard inquiry modes mean before the first discovery, the
// inquiry mode is set.
// Test: extended inquiry is stored in the remote peer
TEST_F(BrEdrDiscoveryManagerTest, ExtendedInquiry) {
  NewDiscoveryManager(pw::bluetooth::emboss::InquiryMode::EXTENDED);

  EXPECT_CMD_PACKET_OUT(test_device(), kSetExtendedMode, &kSetExtendedModeRsp);
  EXPECT_CMD_PACKET_OUT(test_device(),
                        kInquiry,
                        &kInquiryRsp,
                        &kExtendedInquiryResult,
                        &kRSSIInquiryResult);
  EXPECT_CMD_PACKET_OUT(test_device(),
                        kRemoteNameRequest2,
                        &kRemoteNameRequestRsp,
                        &kRemoteNameRequestComplete2);

  std::unique_ptr<BrEdrDiscoverySession> session1;
  size_t peers_found1 = 0u;

  discovery_manager()->RequestDiscovery(
      [&session1, &peers_found1](auto status, auto cb_session) {
        EXPECT_EQ(fit::ok(), status);
        cb_session->set_result_callback(
            [&peers_found1](const auto&) { peers_found1++; });
        session1 = std::move(cb_session);
      });

  EXPECT_FALSE(discovery_manager()->discovering());

  RunUntilIdle();

  EXPECT_TRUE(session1);
  EXPECT_EQ(2u, peers_found1);
  EXPECT_TRUE(discovery_manager()->discovering());
  session1 = nullptr;

  Peer* peer1 = peer_cache()->FindByAddress(kDeviceAddress2);
  ASSERT_TRUE(peer1);
  EXPECT_EQ(-20, peer1->rssi());

  Peer* peer2 = peer_cache()->FindByAddress(kDeviceAddress3);
  ASSERT_TRUE(peer2);
  ASSERT_TRUE(peer2->name());
  EXPECT_EQ("Fuchsia💖", *peer2->name());
  EXPECT_EQ(Peer::NameSource::kInquiryResultComplete, *peer2->name_source());

  test_device()->SendCommandChannelPacket(kInquiryComplete);

  RunUntilIdle();

  EXPECT_FALSE(discovery_manager()->discovering());
}

// Verify that receiving a inquiry response for a known LE non-connectable peer
// results in the peer being changed to DualMode and connectable.
TEST_F(BrEdrDiscoveryManagerTest, InquiryResultUpgradesKnownLowEnergyPeer) {
  Peer* peer = peer_cache()->NewPeer(kLeAliasAddress1, /*connectable=*/false);
  ASSERT_TRUE(peer);
  ASSERT_FALSE(peer->connectable());
  ASSERT_EQ(TechnologyType::kLowEnergy, peer->technology());

  EXPECT_CMD_PACKET_OUT(test_device(), kInquiry, &kInquiryRsp, &kInquiryResult);
  EXPECT_CMD_PACKET_OUT(test_device(),
                        kRemoteNameRequest1,
                        &kRemoteNameRequestRsp,
                        &kRemoteNameRequestComplete1);

  std::unique_ptr<BrEdrDiscoverySession> session;
  size_t peers_found = 0u;

  discovery_manager()->RequestDiscovery([&session, &peers_found](
                                            auto status, auto cb_session) {
    EXPECT_EQ(fit::ok(), status);
    cb_session->set_result_callback([&peers_found](auto&) { peers_found++; });
    session = std::move(cb_session);
  });
  RunUntilIdle();
  session = nullptr;

  EXPECT_EQ(1u, peers_found);
  ASSERT_EQ(peer, peer_cache()->FindByAddress(kDeviceAddress1));
  EXPECT_EQ(TechnologyType::kDualMode, peer->technology());
  EXPECT_TRUE(peer->connectable());

  test_device()->SendCommandChannelPacket(kInquiryComplete);

  RunUntilIdle();
}

// Verify that receiving an extended inquiry response for a known LE
// non-connectable peer results in the peer being changed to DualMode and
// connectable.
TEST_F(BrEdrDiscoveryManagerTest,
       ExtendedInquiryResultUpgradesKnownLowEnergyPeer) {
  Peer* peer = peer_cache()->NewPeer(kLeAliasAddress3, /*connectable=*/false);
  ASSERT_TRUE(peer);
  ASSERT_FALSE(peer->connectable());
  ASSERT_EQ(TechnologyType::kLowEnergy, peer->technology());

  NewDiscoveryManager(pw::bluetooth::emboss::InquiryMode::EXTENDED);

  EXPECT_CMD_PACKET_OUT(test_device(), kSetExtendedMode, &kSetExtendedModeRsp);
  EXPECT_CMD_PACKET_OUT(
      test_device(), kInquiry, &kInquiryRsp, &kExtendedInquiryResult);

  std::unique_ptr<BrEdrDiscoverySession> session;
  size_t peers_found = 0u;

  discovery_manager()->RequestDiscovery([&session, &peers_found](
                                            auto status, auto cb_session) {
    EXPECT_EQ(fit::ok(), status);
    cb_session->set_result_callback([&peers_found](auto&) { peers_found++; });
    session = std::move(cb_session);
  });
  RunUntilIdle();
  session = nullptr;

  EXPECT_EQ(1u, peers_found);
  ASSERT_EQ(peer, peer_cache()->FindByAddress(kDeviceAddress3));
  EXPECT_EQ(TechnologyType::kDualMode, peer->technology());
  EXPECT_TRUE(peer->connectable());

  test_device()->SendCommandChannelPacket(kInquiryComplete);

  RunUntilIdle();
}

// Verify that receiving an extended inquiry response with RSSI for a known LE
// non-connectable peer results in the peer being changed to DualMode and
// connectable.
TEST_F(BrEdrDiscoveryManagerTest, RSSIInquiryResultUpgradesKnownLowEnergyPeer) {
  Peer* peer = peer_cache()->NewPeer(kLeAliasAddress2, /*connectable=*/false);
  ASSERT_TRUE(peer);
  ASSERT_FALSE(peer->connectable());
  ASSERT_EQ(TechnologyType::kLowEnergy, peer->technology());

  NewDiscoveryManager(pw::bluetooth::emboss::InquiryMode::EXTENDED);

  EXPECT_CMD_PACKET_OUT(test_device(), kSetExtendedMode, &kSetExtendedModeRsp);
  EXPECT_CMD_PACKET_OUT(
      test_device(), kInquiry, &kInquiryRsp, &kRSSIInquiryResult);
  EXPECT_CMD_PACKET_OUT(test_device(),
                        kRemoteNameRequest2,
                        &kRemoteNameRequestRsp,
                        &kRemoteNameRequestComplete2);

  std::unique_ptr<BrEdrDiscoverySession> session;
  size_t peers_found = 0u;

  discovery_manager()->RequestDiscovery([&session, &peers_found](
                                            auto status, auto cb_session) {
    EXPECT_EQ(fit::ok(), status);
    cb_session->set_result_callback([&peers_found](auto&) { peers_found++; });
    session = std::move(cb_session);
  });
  RunUntilIdle();
  session = nullptr;

  EXPECT_EQ(1u, peers_found);
  ASSERT_EQ(peer, peer_cache()->FindByAddress(kDeviceAddress2));
  EXPECT_EQ(TechnologyType::kDualMode, peer->technology());
  EXPECT_TRUE(peer->connectable());

  test_device()->SendCommandChannelPacket(kInquiryComplete);

  RunUntilIdle();
}

#ifndef NINSPECT
TEST_F(BrEdrDiscoveryManagerTest, Inspect) {
  inspect::Inspector inspector;
  discovery_manager()->AttachInspect(inspector.GetRoot(),
                                     "bredr_discovery_manager");

  auto discoverable_session_active_matcher =
      Contains(UintIs("discoverable_sessions", 1));

  std::unique_ptr<BrEdrDiscoverableSession> discoverable_session;
  auto session_cb = [&discoverable_session](auto status, auto cb_session) {
    EXPECT_EQ(fit::ok(), status);
    discoverable_session = std::move(cb_session);
  };

  EXPECT_CMD_PACKET_OUT(
      test_device(), kReadScanEnable, &kReadScanEnableRspPage);
  EXPECT_CMD_PACKET_OUT(
      test_device(), kWriteScanEnableBoth, &kWriteScanEnableRsp);
  discovery_manager()->RequestDiscoverable(session_cb);
  RunUntilIdle();
  EXPECT_TRUE(discoverable_session);

  auto properties = inspect::ReadFromVmo(inspector.DuplicateVmo())
                        .take_value()
                        .take_children()
                        .front()
                        .node_ptr()
                        ->take_properties();
  EXPECT_THAT(properties, discoverable_session_active_matcher);

  auto discoverable_session_counted_matcher =
      ::testing::IsSupersetOf({UintIs("discoverable_sessions", 0),
                               UintIs("discoverable_sessions_count", 1),
                               UintIs("last_discoverable_length_sec", 4)});

  RunFor(std::chrono::seconds(4));
  discoverable_session = nullptr;
  EXPECT_CMD_PACKET_OUT(
      test_device(), kReadScanEnable, &kReadScanEnableRspBoth);
  EXPECT_CMD_PACKET_OUT(
      test_device(), kWriteScanEnablePage, &kWriteScanEnableRsp);
  RunUntilIdle();

  properties = inspect::ReadFromVmo(inspector.DuplicateVmo())
                   .take_value()
                   .take_children()
                   .front()
                   .node_ptr()
                   ->take_properties();
  EXPECT_THAT(properties, discoverable_session_counted_matcher);

  auto discovery_session_active_matcher =
      Contains(UintIs("discovery_sessions", 1));

  std::unique_ptr<BrEdrDiscoverySession> discovery_session;

  discovery_manager()->RequestDiscovery(
      [&discovery_session](auto status, auto cb_session) {
        EXPECT_EQ(fit::ok(), status);
        discovery_session = std::move(cb_session);
      });

  EXPECT_CMD_PACKET_OUT(test_device(), kInquiry, &kInquiryRsp);
  RunUntilIdle();
  EXPECT_TRUE(discovery_session);

  properties = inspect::ReadFromVmo(inspector.DuplicateVmo())
                   .take_value()
                   .take_children()
                   .front()
                   .node_ptr()
                   ->take_properties();
  EXPECT_THAT(properties, discovery_session_active_matcher);

  auto discovery_session_counted_matcher =
      ::testing::IsSupersetOf({UintIs("discovery_sessions", 0),
                               UintIs("discovery_sessions_count", 1),
                               UintIs("last_discovery_length_sec", 7)});

  RunFor(std::chrono::seconds(7));
  discovery_session = nullptr;
  RunUntilIdle();
  test_device()->SendCommandChannelPacket(kInquiryComplete);
  RunUntilIdle();

  properties = inspect::ReadFromVmo(inspector.DuplicateVmo())
                   .take_value()
                   .take_children()
                   .front()
                   .node_ptr()
                   ->take_properties();
  EXPECT_THAT(properties, discovery_session_counted_matcher);
}
#endif  // NINSPECT

}  // namespace
}  // namespace bt::gap
