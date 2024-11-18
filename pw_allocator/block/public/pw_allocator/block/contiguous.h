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
#pragma once

#include "lib/stdcompat/bit.h"
#include "pw_allocator/block/basic.h"
#include "pw_bytes/span.h"
#include "pw_result/result.h"
#include "pw_status/status.h"

namespace pw::allocator {
namespace internal {

// Trivial base class for trait support.
struct ContiguousBase {};

}  // namespace internal

/// Mix-in for blocks that collectively represent a contiguous region of memory.
///
/// Contiguous blocks can be split into smaller blocks and merged when adjacent.
///
/// Block mix-ins are stateless and trivially constructible. See `BasicBlock`
/// for details on how mix-ins can be combined to implement blocks.
///
/// This mix-in requires its derived type also derive from `BasicBlock`, and
/// provide the following symbols:
///
/// - static constexpr size_t MaxAddressableSize()
///   - Size of the largest region that can be addressed by a block.
/// - static Derived* AsBlock(BytesSpan)
///   - Instantiates and returns a block for the given region of memory.
/// - size_t PrevOuterSizeUnchecked() const
///   - Returns the outer size of the previous block, if any, or zero.  Must be
///     multiple of `kAlignment`.
/// - bool IsLastUnchecked() const
///   - Returns whether this block is the last block.
template <typename Derived>
class ContiguousBlock : public internal::ContiguousBase {
 protected:
  constexpr explicit ContiguousBlock() {
    // Assert within a function, since `Derived` is not complete when this type
    // is defined.
    static_assert(
        is_block_v<Derived>,
        "Types derived from ContiguousBlock must also derive from BasicBlock");
  }

 public:
  static constexpr size_t kMaxAddressableSize = Derived::MaxAddressableSize();

  /// @brief Creates the first block for a given memory region.
  ///
  /// @returns @rst
  ///
  /// .. pw-status-codes::
  ///
  ///    OK: Returns a block representing the region.
  ///
  ///    INVALID_ARGUMENT: The region is null.
  ///
  ///    RESOURCE_EXHAUSTED: The region is too small for a block.
  ///
  ///    OUT_OF_RANGE: The region is larger than `kMaxAddressableSize`.
  ///
  /// @endrst
  static Result<Derived*> Init(ByteSpan region, bool is_last = true);

  /// @returns the block immediately before this one, or null if this is the
  /// first block.
  inline Derived* Prev() const;

  /// @returns the block immediately after this one, or null if this is the last
  /// block.
  inline Derived* Next() const;

 protected:
  /// Split a block into two smaller blocks and allocates the leading one.
  ///
  /// This method splits a block into a leading block of the given
  /// `new_inner_size` and a trailing block. It marks the leading block as used.
  /// It is static in order to consume and replace the given block pointer with
  /// the pointer to the new leading block. The remaining trailing space is
  /// returned as a new block.
  ///
  /// @pre The block must not be in use.
  /// @pre The block must have enough usable space for the requested size.
  /// @pre The space remaining after a split can hold a new block.
  static Derived* DoSplitFirst(Derived*& block, size_t new_inner_size);

  /// Split a block into two smaller blocks and allocates the trailing one.
  ///
  /// This method splits a block into a leading block and a trailing block of
  /// the given `new_inner_size`. It marks the trailing block as used. It is
  /// static in order to consume and replace the given block pointer with the
  /// pointer to the new trailing block. The remaining leading space is returned
  /// as a new block.
  ///
  /// @pre The block must not be in use.
  /// @pre The block must have enough usable space for the requested size.
  /// @pre The space remaining after a split can hold a new block.
  static Derived* DoSplitLast(Derived*& block, size_t new_inner_size);

  /// Merges this block with next block.
  ///
  /// This method is static in order to consume and replace the given block
  /// pointer with a pointer to the new, larger block.
  ///
  /// @pre The block must not be the last block.
  static void DoMergeNext(Derived*& block);

  /// Performs the ContiguousBlock invariant checks.
  bool DoCheckInvariants(bool crash_on_failure) const;

 private:
  // constexpr Derived* derived() { return static_cast<Derived*>(this); }
  constexpr const Derived* derived() const {
    return static_cast<const Derived*>(this);
  }

  /// @copydoc Prev
  Derived* PrevUnchecked() const;

  /// @copydoc Next
  Derived* NextUnchecked() const;

  /// Split a block into two smaller blocks.
  ///
  /// This method is static in order to consume and replace the given block
  /// pointer with a pointer to the new, smaller block with an inner size of
  /// `new_inner_size` The remaining space will be returned as a new block.
  ///
  /// @pre The block must not be in use.
  /// @pre The block must have enough usable space for the requested size.
  /// @pre The space remaining after a split can hold a new block.
  static Derived* Split(Derived*& block, size_t new_inner_size);

  /// Consumes the block and returns as a span of bytes.
  static ByteSpan AsBytes(Derived*&& block);

  // PoisonableBlock calls DoSplitFirst, DoSplitLast, and DoMergeNext
  template <typename>
  friend class PoisonableBlock;
};

/// Trait type that allow interrogating a block as to whether it is contiguous.
template <typename BlockType>
struct is_contiguous : std::is_base_of<internal::ContiguousBase, BlockType> {};

/// Helper variable template for `is_contiguous<BlockType>::value`.
template <typename BlockType>
inline constexpr bool is_contiguous_v = is_contiguous<BlockType>::value;

namespace internal {

/// Functions to crash with an error message describing which block invariant
/// has been violated. These functions are implemented independent of any
/// template parameters to allow them to use `PW_CHECK`.
void CrashNextMisaligned(uintptr_t addr, uintptr_t next);
void CrashNextPrevMismatched(uintptr_t addr,
                             uintptr_t next,
                             uintptr_t next_prev);
void CrashPrevMisaligned(uintptr_t addr, uintptr_t prev);
void CrashPrevNextMismatched(uintptr_t addr,
                             uintptr_t prev,
                             uintptr_t prev_next);

}  // namespace internal

// Template method implementations.

template <typename Derived>
Result<Derived*> ContiguousBlock<Derived>::Init(ByteSpan region, bool is_last) {
  region = GetAlignedSubspan(region, Derived::kAlignment);
  if (region.size() <= Derived::kBlockOverhead) {
    return Status::ResourceExhausted();
  }
  if (region.size() > Derived::MaxAddressableSize()) {
    return Status::OutOfRange();
  }
  Derived* next = nullptr;
  if (!is_last) {
    std::byte* data = region.data() + region.size() + Derived::kBlockOverhead;
    next = Derived::FromUsableSpace(data);
  }
  auto* block = Derived::AsBlock(region, nullptr, next);
  block->CheckInvariantsIfStrict();
  return block;
}

template <typename Derived>
Derived* ContiguousBlock<Derived>::Prev() const {
  derived()->CheckInvariantsIfStrict();
  return PrevUnchecked();
}

template <typename Derived>
Derived* ContiguousBlock<Derived>::PrevUnchecked() const {
  size_t prev_outer_size = derived()->PrevOuterSizeUnchecked();
  if (prev_outer_size == 0) {
    return nullptr;
  }
  auto addr = cpp20::bit_cast<uintptr_t>(this);
  PW_ASSERT(!PW_SUB_OVERFLOW(addr, prev_outer_size, &addr));
  return std::launder(reinterpret_cast<Derived*>(addr));
}

template <typename Derived>
Derived* ContiguousBlock<Derived>::Next() const {
  derived()->CheckInvariantsIfStrict();
  return NextUnchecked();
}

template <typename Derived>
Derived* ContiguousBlock<Derived>::NextUnchecked() const {
  if (derived()->IsLastUnchecked()) {
    return nullptr;
  }
  size_t outer_size = derived()->OuterSizeUnchecked();
  auto addr = cpp20::bit_cast<uintptr_t>(this);
  PW_ASSERT(!PW_ADD_OVERFLOW(addr, outer_size, &addr));
  return std::launder(reinterpret_cast<Derived*>(addr));
}

template <typename Derived>
Derived* ContiguousBlock<Derived>::DoSplitFirst(Derived*& block,
                                                size_t new_inner_size) {
  return Split(block, new_inner_size);
}

template <typename Derived>
Derived* ContiguousBlock<Derived>::DoSplitLast(Derived*& block,
                                               size_t new_inner_size) {
  size_t inner_size = block->InnerSize();
  size_t new_outer_size = Derived::kBlockOverhead + new_inner_size;
  Derived* trailing = Split(block, inner_size - new_outer_size);
  Derived* leading = block;
  block = trailing;
  return leading;
}

template <typename Derived>
Derived* ContiguousBlock<Derived>::Split(Derived*& block,
                                         size_t new_inner_size) {
  size_t new_outer_size = Derived::kBlockOverhead + new_inner_size;
  Derived* prev = block->Prev();
  Derived* next = block->Next();
  ByteSpan bytes = AsBytes(std::move(block));
  ByteSpan leading = bytes.subspan(0, new_outer_size);
  ByteSpan trailing = bytes.subspan(new_outer_size);
  Derived* block2 = Derived::AsBlock(trailing, nullptr, next);
  Derived* block1 = Derived::AsBlock(leading, prev, block2);
  block1->CheckInvariantsIfStrict();
  block2->CheckInvariantsIfStrict();
  block = std::move(block1);
  return block2;
}

template <typename Derived>
void ContiguousBlock<Derived>::DoMergeNext(Derived*& block) {
  Derived* next = block->Next();
  Derived* prev = block->Prev();
  Derived* next_next = next->Next();
  ByteSpan bytes = AsBytes(std::move(block));
  ByteSpan next_bytes = AsBytes(std::move(next));
  size_t outer_size = bytes.size() + next_bytes.size();
  std::byte* merged = ::new (bytes.data()) std::byte[outer_size];
  block = Derived::AsBlock(ByteSpan(merged, outer_size), prev, next_next);
}

template <typename Derived>
ByteSpan ContiguousBlock<Derived>::AsBytes(Derived*&& block) {
  size_t outer_size = block->OuterSize();
  std::byte* bytes = ::new (std::move(block)) std::byte[outer_size];
  return {bytes, outer_size};
}

template <typename Derived>
bool ContiguousBlock<Derived>::DoCheckInvariants(bool crash_on_failure) const {
  auto addr = cpp20::bit_cast<uintptr_t>(this);
  Derived* next = derived()->NextUnchecked();
  if (next != nullptr) {
    auto next_addr = cpp20::bit_cast<uintptr_t>(next);
    if (next_addr % Derived::kAlignment != 0) {
      if (crash_on_failure) {
        internal::CrashNextMisaligned(addr, next_addr);
      }
      return false;
    }
    Derived* next_prev = next->PrevUnchecked();
    if (this != next_prev) {
      if (crash_on_failure) {
        auto next_prev_addr = cpp20::bit_cast<uintptr_t>(next_prev);
        internal::CrashNextPrevMismatched(addr, next_addr, next_prev_addr);
      }
      return false;
    }
  }
  Derived* prev = derived()->PrevUnchecked();
  if (prev != nullptr) {
    auto prev_addr = cpp20::bit_cast<uintptr_t>(prev);
    if (prev_addr % Derived::kAlignment != 0) {
      if (crash_on_failure) {
        internal::CrashPrevMisaligned(addr, prev_addr);
      }
      return false;
    }
    Derived* prev_next = prev->NextUnchecked();
    auto prev_next_addr = cpp20::bit_cast<uintptr_t>(prev_next);
    if (this != prev_next) {
      if (crash_on_failure) {
        internal::CrashPrevNextMismatched(addr, prev_addr, prev_next_addr);
      }
      return false;
    }
  }
  return true;
}

}  // namespace pw::allocator
