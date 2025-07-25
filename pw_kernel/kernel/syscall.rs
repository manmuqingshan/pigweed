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

use pw_cast::CastInto as _;
use pw_log::info;
use pw_status::{Error, Result};
use syscall_defs::{SysCallId, SysCallReturnValue};

use crate::Kernel;

const SYSCALL_DEBUG: bool = false;

pub fn handle_syscall<K: Kernel>(
    kernel: K,
    id: u16,
    arg0: usize,
    arg1: usize,
    _arg2: usize,
    _arg3: usize,
) -> Result<u64> {
    log_if::debug_if!(SYSCALL_DEBUG, "syscall: {:#06x}", id as usize);

    // Instead of having a architecture independent match here, an array of
    // extern "C" function pointers could be kept and use the architecture's
    // calling convention to directly call them.
    //
    // This allows [`arch::arm_cortex_m::in_interrupt_handler()`] to treat
    // active SVCalls as not in interrupt context.
    let id: SysCallId = id.try_into()?;
    let res = match id {
        SysCallId::DebugNoOp => Ok(0),
        SysCallId::DebugAdd => {
            log_if::debug_if!(
                SYSCALL_DEBUG,
                "syscall: DebugAdd({:#x}, {:#x}) sleeping",
                arg0 as usize,
                arg1 as usize,
            );
            crate::sleep_until(kernel, kernel.now() + crate::Duration::from_secs(1));
            log_if::debug_if!(SYSCALL_DEBUG, "sycall: DebugAdd woken");
            match arg0.checked_add(arg1) {
                Some(res) => Ok(res.cast_into()),
                None => Err(Error::OutOfRange),
            }
        }
        // TODO: Remove this syscall when logging is added.
        SysCallId::DebugPutc => {
            crate::sleep_until(kernel, kernel.now() + crate::Duration::from_secs(1));
            let c = u32::try_from(arg0)
                .ok()
                .and_then(char::from_u32)
                .ok_or(Error::InvalidArgument)?;
            info!("{}", c as char);
            Ok(arg0.cast_into())
        }
    };
    log_if::debug_if!(SYSCALL_DEBUG, "syscall: {:#06x} returning", id as usize);
    res
}

#[allow(dead_code)]
pub fn raw_handle_syscall<K: Kernel>(
    kernel: K,
    id: u16,
    arg0: usize,
    arg1: usize,
    arg2: usize,
    arg3: usize,
) -> i64 {
    let ret_val: SysCallReturnValue = handle_syscall(kernel, id, arg0, arg1, arg2, arg3).into();
    ret_val.0
}
