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

#include "pw_digital_io/digital_io.h"

#include <array>
#include <mutex>

#include "fsl_clock.h"
#include "fsl_gpio.h"
#include "fsl_reset.h"
#include "pw_assert/check.h"
#include "pw_digital_io_mcuxpresso/digital_io.h"
#include "pw_status/status.h"
#include "pw_sync/interrupt_spin_lock.h"

namespace pw::digital_io {
namespace {

constexpr std::array kGpioClocks = GPIO_CLOCKS;
constexpr std::array kGpioResets = GPIO_RSTS_N;
constexpr uint32_t kNumGpioPorts = GPIO_INTSTATA_COUNT;
constexpr gpio_interrupt_index_t kGpioInterruptBankIndex = kGPIO_InterruptA;

// This lock is used to prevent simultaneous access to the list of registered
// interrupt handlers and the underlying interrupt hardware.
sync::InterruptSpinLock port_interrupts_lock;

// The HS GPIO block culminates all pin interrupts into single interrupt
// vectors. Each GPIO port has a corresponding interrupt register and status
// register.
//
// It would be expensive from a memory perspective to statically define a
// handler pointer for each pin so a linked list used instead. It's added to
// whenever a handler is registered with DoSetInterruptHandler() and removed
// whenever a handler is de-registered with DoSetInterruptHandler().
//
// To improve handler lookup performance, an array of linked lists is created
// below, 1 per port. This allows for traversal of smaller linked lists, and
// only the linked lists that have active interrupts.
std::array<pw::IntrusiveForwardList<McuxpressoDigitalInOutInterrupt>,
           kNumGpioPorts>
    port_interrupts PW_GUARDED_BY(port_interrupts_lock);

uint32_t GPIO_PortGetInterruptEnable(GPIO_Type* base,
                                     uint32_t port,
                                     gpio_interrupt_index_t interrupt) {
  switch (interrupt) {
    case kGPIO_InterruptA:
      return base->INTENA[port];
    case kGPIO_InterruptB:
      return base->INTENB[port];
    default:
      PW_CRASH("Invalid interrupt");
  }
}

bool GPIO_PinGetInterruptEnable(GPIO_Type* base,
                                uint32_t port,
                                uint32_t pin,
                                gpio_interrupt_index_t interrupt) {
  return ((GPIO_PortGetInterruptEnable(base, port, interrupt) >> pin) & 1);
}

}  // namespace

McuxpressoDigitalOut::McuxpressoDigitalOut(GPIO_Type* base,
                                           uint32_t port,
                                           uint32_t pin,
                                           pw::digital_io::State initial_state)
    : base_(base), port_(port), pin_(pin), initial_state_(initial_state) {
  PW_CHECK(base != nullptr);
  PW_CHECK(port < kGpioClocks.size());
  PW_CHECK(port < kGpioResets.size());
}

pw::Status McuxpressoDigitalOut::DoEnable(bool enable) {
  if (enable) {
    if (is_enabled()) {
      return pw::OkStatus();
    }

    CLOCK_EnableClock(kGpioClocks[port_]);
    RESET_ClearPeripheralReset(kGpioResets[port_]);

    gpio_pin_config_t config = {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic = static_cast<uint8_t>(
            initial_state_ == pw::digital_io::State::kActive ? 1U : 0U),
    };
    GPIO_PinInit(base_, port_, pin_, &config);

  } else {
    // Set to input on disable.
    gpio_pin_config_t config = {
        .pinDirection = kGPIO_DigitalInput,
        .outputLogic = 0,
    };

    // Must enable the clock, since GPIO can get disabled without ever
    // being enabled.
    CLOCK_EnableClock(kGpioClocks[port_]);
    GPIO_PinInit(base_, port_, pin_, &config);

    // Can't disable clock as other users on same port may be active.
  }
  enabled_ = enable;
  return pw::OkStatus();
}

pw::Status McuxpressoDigitalOut::DoSetState(pw::digital_io::State state) {
  if (!is_enabled()) {
    return pw::Status::FailedPrecondition();
  }
  GPIO_PinWrite(
      base_, port_, pin_, state == pw::digital_io::State::kActive ? 1 : 0);
  return pw::OkStatus();
}

McuxpressoDigitalIn::McuxpressoDigitalIn(GPIO_Type* base,
                                         uint32_t port,
                                         uint32_t pin)
    : base_(base), port_(port), pin_(pin) {
  PW_CHECK(base != nullptr);
  PW_CHECK(port < kGpioClocks.size());
  PW_CHECK(port < kGpioResets.size());
}

pw::Status McuxpressoDigitalIn::DoEnable(bool enable) {
  if (enable) {
    if (is_enabled()) {
      return pw::OkStatus();
    }
  } else {
    enabled_ = false;
    // Can't disable clock as other users on same port may be active.
    return pw::OkStatus();
  }

  CLOCK_EnableClock(kGpioClocks[port_]);
  RESET_ClearPeripheralReset(kGpioResets[port_]);

  gpio_pin_config_t config = {
      .pinDirection = kGPIO_DigitalInput,
      .outputLogic = 0,
  };
  GPIO_PinInit(base_, port_, pin_, &config);

  enabled_ = enable;
  return pw::OkStatus();
}

pw::Result<pw::digital_io::State> McuxpressoDigitalIn::DoGetState() {
  if (!is_enabled()) {
    return pw::Status::FailedPrecondition();
  }
  uint32_t value = GPIO_PinRead(base_, port_, pin_);
  return value == 1 ? pw::digital_io::State::kActive
                    : pw::digital_io::State::kInactive;
}

McuxpressoDigitalInOutInterrupt::McuxpressoDigitalInOutInterrupt(
    GPIO_Type* base, uint32_t port, uint32_t pin, bool output)
    : base_(base), port_(port), pin_(pin), output_(output) {
  PW_CHECK(base != nullptr);
  PW_CHECK(port < kGpioClocks.size());
  PW_CHECK(port < kGpioResets.size());
  PW_CHECK(port < port_interrupts.size());
}

pw::Status McuxpressoDigitalInOutInterrupt::DoEnable(bool enable) {
  if (enable) {
    if (is_enabled()) {
      return pw::OkStatus();
    }
  } else {
    enabled_ = false;
    // Can't disable clock as other users on same port may be active.
    return pw::OkStatus();
  }

  CLOCK_EnableClock(kGpioClocks[port_]);
  RESET_ClearPeripheralReset(kGpioResets[port_]);

  gpio_pin_config_t config = {
      .pinDirection = (output_ ? kGPIO_DigitalOutput : kGPIO_DigitalInput),
      .outputLogic = 0,
  };
  GPIO_PinInit(base_, port_, pin_, &config);

  enabled_ = enable;
  return pw::OkStatus();
}

pw::Result<pw::digital_io::State>
McuxpressoDigitalInOutInterrupt::DoGetState() {
  if (!is_enabled()) {
    return pw::Status::FailedPrecondition();
  }

  uint32_t value = GPIO_PinRead(base_, port_, pin_);
  return value == 1 ? pw::digital_io::State::kActive
                    : pw::digital_io::State::kInactive;
}

pw::Status McuxpressoDigitalInOutInterrupt::DoSetState(
    pw::digital_io::State state) {
  if (!is_enabled()) {
    return pw::Status::FailedPrecondition();
  }
  GPIO_PinWrite(
      base_, port_, pin_, state == pw::digital_io::State::kActive ? 1 : 0);

  return pw::OkStatus();
}

pw::Status McuxpressoDigitalInOutInterrupt::DoSetInterruptHandler(
    pw::digital_io::InterruptTrigger trigger,
    pw::digital_io::InterruptHandler&& handler) {
  if (handler == nullptr) {
    std::lock_guard lock(port_interrupts_lock);
    if (GPIO_PinGetInterruptEnable(
            base_, port_, pin_, kGpioInterruptBankIndex)) {
      // Can only clear handler when the interrupt is disabled
      return pw::Status::FailedPrecondition();
    }

    unlist();

    interrupt_handler_ = nullptr;
    return pw::OkStatus();
  }

  if (interrupt_handler_ != nullptr) {
    // Can only set a handler when none is set
    return pw::Status::FailedPrecondition();
  }

  trigger_ = trigger;
  gpio_pin_enable_polarity_t polarity;

  switch (trigger_) {
    case InterruptTrigger::kActivatingEdge:
      polarity = kGPIO_PinIntEnableHighOrRise;
      break;
    case InterruptTrigger::kDeactivatingEdge:
      polarity = kGPIO_PinIntEnableLowOrFall;
      break;
    default:
      return pw::Status::InvalidArgument();
  }

  gpio_interrupt_config_t config = {
      .mode = kGPIO_PinIntEnableEdge,
      .polarity = static_cast<uint8_t>(polarity),
  };

  std::lock_guard lock(port_interrupts_lock);
  GPIO_SetPinInterruptConfig(base_, port_, pin_, &config);

  interrupt_handler_ = std::move(handler);

  if (unlisted()) {
    auto& list = port_interrupts[port_];
    list.push_front(*this);
  }

  return pw::OkStatus();
}

pw::Status McuxpressoDigitalInOutInterrupt::DoEnableInterruptHandler(
    bool enable) {
  uint32_t mask = 1 << pin_;

  std::lock_guard lock(port_interrupts_lock);

  if (enable) {
    if (interrupt_handler_ == nullptr) {
      return pw::Status::FailedPrecondition();
    }
    GPIO_PortEnableInterrupts(base_, port_, kGpioInterruptBankIndex, mask);
    NVIC_EnableIRQ(GPIO_INTA_IRQn);
  } else {
    GPIO_PortDisableInterrupts(base_, port_, kGpioInterruptBankIndex, mask);
  }

  return pw::OkStatus();
}

PW_EXTERN_C void GPIO_INTA_DriverIRQHandler() PW_NO_LOCK_SAFETY_ANALYSIS {
  auto* base = GPIO;

  // For each port
  for (uint32_t port = 0; port < kNumGpioPorts; port++) {
    const auto& list = port_interrupts[port];
    const uint32_t port_int_flags =
        GPIO_PortGetInterruptEnable(base, port, kGpioInterruptBankIndex) &
        GPIO_PortGetInterruptStatus(base, port, kGpioInterruptBankIndex);

    if (port_int_flags == 0) {
      // If no flags are set, there is nothing to do. Skip traversing the linked
      // list
      continue;
    }

    // For each line registered on that port's interrupt list
    for (const auto& line : list) {
      if ((port_int_flags & (1UL << line.pin_)) != 0) {
        line.interrupt_handler_(line.trigger_ ==
                                        InterruptTrigger::kActivatingEdge
                                    ? State::kActive
                                    : State::kInactive);
        GPIO_PinClearInterruptFlag(
            base, port, line.pin_, kGpioInterruptBankIndex);
      }
    }
  }

  SDK_ISR_EXIT_BARRIER;
}

}  // namespace pw::digital_io
