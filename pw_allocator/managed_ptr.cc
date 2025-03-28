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

#include "pw_allocator/internal/managed_ptr.h"

#include "pw_allocator/allocator.h"
#include "pw_allocator/deallocator.h"

namespace pw::allocator::internal {

bool BaseManagedPtr::HasCapability(Deallocator* deallocator,
                                   Capability capability) {
  return deallocator->HasCapability(capability);
}

void BaseManagedPtr::Deallocate(Deallocator* deallocator, void* ptr) {
  deallocator->Deallocate(ptr);
}

bool BaseManagedPtr::Resize(Allocator* allocator, void* ptr, size_t new_size) {
  return allocator->Resize(ptr, new_size);
}

}  // namespace pw::allocator::internal
