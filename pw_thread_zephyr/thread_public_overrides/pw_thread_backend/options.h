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
#pragma once

#include "pw_thread_backend/context.h"
#include "pw_thread_zephyr/options.h"

namespace pw::thread::backend {

using NativeOptions = ::pw::thread::zephyr::Options;

constexpr NativeOptions GetNativeOptions(NativeContext& context,
                                         const ThreadAttrs& attributes) {
  return ::pw::thread::zephyr::GetOptions(context, attributes);
}

}  // namespace pw::thread::backend
