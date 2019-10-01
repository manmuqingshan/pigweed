// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/bluetooth/core/bt-host/gap/pairing_state.h"

#include "src/connectivity/bluetooth/core/bt-host/common/log.h"
#include "src/connectivity/bluetooth/core/bt-host/sm/util.h"

namespace bt {
namespace gap {

using hci::AuthRequirements;
using hci::IOCapability;
using sm::util::IOCapabilityForHci;

PairingState::PairingState(PeerId peer_id, hci::Connection* link, StatusCallback status_cb)
    : peer_id_(peer_id), link_(link), state_(State::kIdle), status_callback_(std::move(status_cb)) {
  ZX_ASSERT(link_);
  ZX_ASSERT(link_->ll_type() != hci::Connection::LinkType::kLE);
  ZX_ASSERT(status_callback_);
  link_->set_encryption_change_callback(fit::bind_member(this, &PairingState::OnEncryptionChange));
}

PairingState::~PairingState() { link_->set_encryption_change_callback(nullptr); }

PairingState::InitiatorAction PairingState::InitiatePairing(StatusCallback status_cb) {
  // Raise an error to only the initiator—and not others—if we can't pair because there's no pairing
  // delegate.
  if (!pairing_delegate()) {
    bt_log(TRACE, "gap-bredr", "No pairing delegate for link %#.4x (id: %s); not pairing", handle(),
           bt_str(peer_id()));
    status_cb(handle(), hci::Status(HostError::kNotReady));
    return InitiatorAction::kDoNotSendAuthenticationRequest;
  }

  if (state() == State::kIdle) {
    ZX_ASSERT(!is_pairing());
    current_pairing_ = Pairing::MakeInitiator(std::move(status_cb));
    bt_log(TRACE, "gap-bredr", "Initiating pairing on %#.4x (id %s)", handle(), bt_str(peer_id()));
    state_ = State::kInitiatorPairingStarted;
    return InitiatorAction::kSendAuthenticationRequest;
  }

  // More than one consumer may wish to initiate pairing (e.g. concurrent outbound L2CAP channels),
  // but each should wait for the results of any ongoing pairing procedure instead of sending their
  // own Authentication Request.
  if (is_pairing()) {
    ZX_ASSERT(state() != State::kIdle);
    bt_log(TRACE, "gap-bredr", "Already pairing %#.4x (id: %s); blocking callback on completion",
           handle(), bt_str(peer_id()));
    current_pairing_->initiator_callbacks.push_back(std::move(status_cb));
  } else {
    // In the error state, we should expect no pairing to be created and cancel this particular
    // request immediately.
    ZX_ASSERT(state() == State::kFailed);
    status_cb(handle(), hci::Status(HostError::kCanceled));
  }

  return InitiatorAction::kDoNotSendAuthenticationRequest;
}

std::optional<IOCapability> PairingState::OnIoCapabilityRequest() {
  if (state() == State::kInitiatorPairingStarted) {
    ZX_ASSERT(initiator());
    ZX_ASSERT_MSG(pairing_delegate(), "PairingDelegate was reset after pairing began");

    // TODO(37447): PairingDelegate may be reset if bt-gap exits and clears PairingDelegate (which
    // is processed on a different thread).
    current_pairing_->local_iocap =
        sm::util::IOCapabilityForHci(pairing_delegate()->io_capability());

    state_ = State::kInitiatorWaitIoCapResponse;
  } else if (state() == State::kResponderWaitIoCapRequest) {
    ZX_ASSERT(is_pairing());
    ZX_ASSERT(!initiator());

    // Raise an error if we can't respond to a pairing request because there's no pairing delegate.
    if (!pairing_delegate()) {
      bt_log(ERROR, "gap-bredr", "No pairing delegate for link %#.4x (id: %s); not pairing",
             handle(), bt_str(peer_id()));
      state_ = State::kIdle;
      SignalStatus(hci::Status(HostError::kNotReady));
      return std::nullopt;
    }

    // TODO(37447): PairingDelegate may be reset if bt-gap exits and clears PairingDelegate (which
    // is processed on a different thread).
    current_pairing_->local_iocap =
        sm::util::IOCapabilityForHci(pairing_delegate()->io_capability());
    WritePairingData();

    state_ = GetStateForPairingEvent(current_pairing_->expected_event);
  } else {
    FailWithUnexpectedEvent(__func__);
    return std::nullopt;
  }

  return current_pairing_->local_iocap;
}

void PairingState::OnIoCapabilityResponse(IOCapability peer_iocap) {
  if (state() == State::kIdle) {
    ZX_ASSERT(!is_pairing());
    current_pairing_ = Pairing::MakeResponder(peer_iocap);

    // Defer gathering local IO Capability until OnIoCapabilityRequest, where
    // the pairing can be rejected if there's no pairing delegate.
    state_ = State::kResponderWaitIoCapRequest;
  } else if (state() == State::kInitiatorWaitIoCapResponse) {
    ZX_ASSERT(initiator());

    current_pairing_->peer_iocap = peer_iocap;
    WritePairingData();

    state_ = GetStateForPairingEvent(current_pairing_->expected_event);
  } else {
    FailWithUnexpectedEvent(__func__);
  }
}

void PairingState::OnUserConfirmationRequest(uint32_t numeric_value, UserConfirmationCallback cb) {
  if (state() != State::kWaitUserConfirmationRequest) {
    FailWithUnexpectedEvent(__func__);
    cb(false);
    return;
  }
  ZX_ASSERT(is_pairing());

  // TODO(xow): Return actual user response.
  state_ = State::kWaitPairingComplete;
  cb(true);
}

void PairingState::OnUserPasskeyRequest(UserPasskeyCallback cb) {
  if (state() != State::kWaitUserPasskeyRequest) {
    FailWithUnexpectedEvent(__func__);
    cb(std::nullopt);
    return;
  }
  ZX_ASSERT(is_pairing());

  // TODO(xow): Return actual user response.
  state_ = State::kWaitPairingComplete;
  cb(0);
}

void PairingState::OnUserPasskeyNotification(uint32_t numeric_value) {
  if (state() != State::kWaitUserPasskeyNotification) {
    FailWithUnexpectedEvent(__func__);
    return;
  }
  ZX_ASSERT(is_pairing());

  // TODO(xow): Display passkey to user.
  state_ = State::kWaitPairingComplete;
}

void PairingState::OnSimplePairingComplete(hci::StatusCode status_code) {
  if (state() != State::kWaitPairingComplete) {
    FailWithUnexpectedEvent(__func__);
    return;
  }
  ZX_ASSERT(is_pairing());

  if (const hci::Status status(status_code);
      bt_is_error(status, INFO, "gap-bredr", "Pairing failed on link %#.4x (id: %s)", handle(),
                  bt_str(peer_id()))) {
    state_ = State::kFailed;
    SignalStatus(status);
    return;
  }

  state_ = State::kWaitLinkKey;
}

void PairingState::OnLinkKeyNotification(const UInt128& link_key, hci::LinkKeyType key_type) {
  if (state() != State::kWaitLinkKey) {
    FailWithUnexpectedEvent(__func__);
    return;
  }
  ZX_ASSERT(is_pairing());

  // TODO(xow): Store link key.
  if (initiator()) {
    state_ = State::kInitiatorWaitAuthComplete;
  } else {
    EnableEncryption();
  }
}

void PairingState::OnAuthenticationComplete(hci::StatusCode status_code) {
  if (state() != State::kInitiatorPairingStarted && state() != State::kInitiatorWaitAuthComplete) {
    FailWithUnexpectedEvent(__func__);
    return;
  }
  ZX_ASSERT(initiator());

  if (const hci::Status status(status_code);
      bt_is_error(status, INFO, "gap-bredr", "Authentication failed on link %#.4x (id: %s)",
                  handle(), bt_str(peer_id()))) {
    state_ = State::kFailed;
    SignalStatus(status);
    return;
  }

  EnableEncryption();
}

void PairingState::OnEncryptionChange(hci::Status status, bool enabled) {
  if (state() != State::kWaitEncryption) {
    // Ignore encryption changes when not expecting them because they may be
    // triggered by the peer at any time (v5.0 Vol 2, Part F, Sec 4.4).
    bt_log(INFO, "gap-bredr",
           "%#.4x (id: %s): Ignoring %s(%s, %s) in state \"%s\", before pairing completed",
           handle(), bt_str(peer_id()), __func__, bt_str(status), enabled ? "true" : "false",
           ToString(state()));
    return;
  }

  if (status && !enabled) {
    // With Secure Connections, encryption should never be disabled (v5.0 Vol 2,
    // Part E, Sec 7.1.16) at all.
    bt_log(WARN, "gap-bredr", "Pairing failed due to encryption disable on link %#.4x (id: %s)",
           handle(), bt_str(peer_id()));
    status = hci::Status(HostError::kFailed);
  }

  // Perform state transition.
  if (status) {
    // Reset state for another pairing.
    state_ = State::kIdle;
  } else {
    state_ = State::kFailed;
  }

  // TODO(xow): Write link key to Connection::ltk to register new security
  //            properties.
  SignalStatus(status);
}

PairingState::Pairing PairingState::Pairing::MakeInitiator(StatusCallback status_callback) {
  Pairing pairing;
  pairing.initiator = true;
  pairing.initiator_callbacks.push_back(std::move(status_callback));
  return pairing;
}

PairingState::Pairing PairingState::Pairing::MakeResponder(hci::IOCapability peer_iocap) {
  Pairing pairing;
  pairing.initiator = false;
  pairing.peer_iocap = peer_iocap;
  return pairing;
}

const char* PairingState::ToString(PairingState::State state) {
  switch (state) {
    case State::kIdle:
      return "Idle";
    case State::kInitiatorPairingStarted:
      return "InitiatorPairingStarted";
    case State::kInitiatorWaitIoCapResponse:
      return "InitiatorWaitIoCapResponse";
    case State::kResponderWaitIoCapRequest:
      return "ResponderWaitIoCapRequest";
    case State::kWaitUserConfirmationRequest:
      return "WaitUserConfirmationRequest";
    case State::kWaitUserPasskeyRequest:
      return "WaitUserPasskeyRequest";
    case State::kWaitUserPasskeyNotification:
      return "WaitUserPasskeyNotification";
    case State::kWaitPairingComplete:
      return "WaitPairingComplete";
    case State::kWaitLinkKey:
      return "WaitLinkKey";
    case State::kInitiatorWaitAuthComplete:
      return "InitiatorWaitAuthComplete";
    case State::kWaitEncryption:
      return "WaitEncryption";
    case State::kFailed:
      return "Failed";
    default:
      break;
  }
  return "";
}

PairingState::State PairingState::GetStateForPairingEvent(hci::EventCode event_code) {
  switch (event_code) {
    case hci::kUserConfirmationRequestEventCode:
      return State::kWaitUserConfirmationRequest;
    case hci::kUserPasskeyRequestEventCode:
      return State::kWaitUserPasskeyRequest;
    case hci::kUserPasskeyNotificationEventCode:
      return State::kWaitUserPasskeyNotification;
    default:
      break;
  }
  return State::kFailed;
}

void PairingState::SignalStatus(hci::Status status) {
  bt_log(SPEW, "gap-bredr", "Signaling pairing listeners for %#.4x (id: %s) with %s", handle(),
         bt_str(peer_id()), bt_str(status));
  std::vector<StatusCallback> callbacks_to_signal;
  if (is_pairing()) {
    std::swap(callbacks_to_signal, current_pairing_->initiator_callbacks);
    current_pairing_ = std::nullopt;
  }

  // This PairingState may be destroyed by these callbacks (e.g. if signaling an error causes a
  // disconnection), so care must be taken not to access any members.
  const auto handle = this->handle();
  status_callback_(handle, status);
  for (auto& cb : callbacks_to_signal) {
    cb(handle, status);
  }
}

void PairingState::EnableEncryption() {
  if (!link_->StartEncryption()) {
    bt_log(ERROR, "gap-bredr", "%#.4x (id: %s): Failed to enable encryption (state \"%s\")",
           handle(), bt_str(peer_id()), ToString(state()));
    status_callback_(link_->handle(), hci::Status(HostError::kFailed));
    state_ = State::kFailed;
    return;
  }
  state_ = State::kWaitEncryption;
}

void PairingState::FailWithUnexpectedEvent(const char* handler_name) {
  bt_log(ERROR, "gap-bredr", "%#.4x (id: %s): Unexpected event %s while in state \"%s\"", handle(),
         bt_str(peer_id()), handler_name, ToString(state()));
  state_ = State::kFailed;
  SignalStatus(hci::Status(HostError::kNotSupported));
}

void PairingState::WritePairingData() {
  ZX_ASSERT(is_pairing());
  current_pairing_->expected_event =
      GetExpectedEvent(current_pairing_->local_iocap, current_pairing_->peer_iocap);
  ZX_DEBUG_ASSERT(GetStateForPairingEvent(current_pairing_->expected_event) != State::kFailed);
}

PairingAction GetInitiatorPairingAction(IOCapability initiator_cap, IOCapability responder_cap) {
  if (initiator_cap == IOCapability::kNoInputNoOutput) {
    return PairingAction::kAutomatic;
  }
  if (responder_cap == IOCapability::kNoInputNoOutput) {
    if (initiator_cap == IOCapability::kDisplayYesNo) {
      return PairingAction::kGetConsent;
    }
    return PairingAction::kAutomatic;
  }
  if (initiator_cap == IOCapability::kKeyboardOnly) {
    return PairingAction::kRequestPasskey;
  }
  if (responder_cap == IOCapability::kDisplayOnly) {
    if (initiator_cap == IOCapability::kDisplayYesNo) {
      return PairingAction::kComparePasskey;
    }
    return PairingAction::kAutomatic;
  }
  return PairingAction::kDisplayPasskey;
}

PairingAction GetResponderPairingAction(IOCapability initiator_cap, IOCapability responder_cap) {
  if (initiator_cap == IOCapability::kNoInputNoOutput &&
      responder_cap == IOCapability::kKeyboardOnly) {
    return PairingAction::kGetConsent;
  }
  if (initiator_cap == IOCapability::kDisplayYesNo &&
      responder_cap == IOCapability::kDisplayYesNo) {
    return PairingAction::kComparePasskey;
  }
  return GetInitiatorPairingAction(responder_cap, initiator_cap);
}

hci::EventCode GetExpectedEvent(IOCapability local_cap, IOCapability peer_cap) {
  if (local_cap == IOCapability::kNoInputNoOutput || peer_cap == IOCapability::kNoInputNoOutput) {
    return hci::kUserConfirmationRequestEventCode;
  }
  if (local_cap == IOCapability::kKeyboardOnly) {
    return hci::kUserPasskeyRequestEventCode;
  }
  if (peer_cap == IOCapability::kKeyboardOnly) {
    return hci::kUserPasskeyNotificationEventCode;
  }
  return hci::kUserConfirmationRequestEventCode;
}

bool IsPairingAuthenticated(IOCapability local_cap, IOCapability peer_cap) {
  if (local_cap == IOCapability::kNoInputNoOutput || peer_cap == IOCapability::kNoInputNoOutput) {
    return false;
  }
  if (local_cap == IOCapability::kDisplayYesNo && peer_cap == IOCapability::kDisplayYesNo) {
    return true;
  }
  if (local_cap == IOCapability::kKeyboardOnly || peer_cap == IOCapability::kKeyboardOnly) {
    return true;
  }
  return false;
}

AuthRequirements GetInitiatorAuthRequirements(IOCapability local_cap) {
  if (local_cap == IOCapability::kNoInputNoOutput) {
    return AuthRequirements::kGeneralBonding;
  }
  return AuthRequirements::kMITMGeneralBonding;
}

AuthRequirements GetResponderAuthRequirements(IOCapability local_cap, IOCapability peer_cap) {
  if (IsPairingAuthenticated(local_cap, peer_cap)) {
    return AuthRequirements::kMITMGeneralBonding;
  }
  return AuthRequirements::kGeneralBonding;
}

}  // namespace gap
}  // namespace bt
