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

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pw_allocator/allocator.h"
#include "pw_allocator/block/detailed_block.h"
#include "pw_allocator/block_allocator.h"
#include "pw_allocator/bucket/fast_sorted.h"
#include "pw_bytes/span.h"

namespace pw::allocator::size_report {

/// Default block type to use for tests.
using BlockType = DetailedBlock<uint32_t, GenericFastSortedItem>;

/// Type used for exercising an allocator.
struct Foo final {
  std::array<std::byte, 16> buffer;
};

/// Type used for exercising an allocator.
struct Bar {
  Foo foo;
  size_t number;

  Bar(size_t number_) : number(number_) {
    std::memset(foo.buffer.data(), 0, foo.buffer.size());
  }
};

/// Type used for exercising an allocator.
struct Baz {
  Foo foo;
  uint16_t id;
};

/// Returns a view of a statically allocated array of bytes.
ByteSpan GetBuffer();

/// Measures the size of common functions and data without any allocators.
///
/// @param[in]  mask        A bitmap that can be passed to `PW_BLOAT_COND` and
///                         `PW_BLOAT_EXPR`. See those macros for details.
int SetBaseline(uint32_t mask);

/// Exercises a block implementation as part of a size report.
///
/// @tparam     BlockType   The type of block to create and exercise.
/// @param[in]  mask        A bitmap that can be passed to `PW_BLOAT_COND` and
///                         `PW_BLOAT_EXPR`. See those macros for details.
template <typename BlockType>
int MeasureBlock(uint32_t mask);

/// Exercises an allocator as part of a size report.
///
/// @param[in]  allocator   The allocator to exercise.
/// @param[in]  mask        A bitmap that can be passed to `PW_BLOAT_COND` and
///                         `PW_BLOAT_EXPR`. See those macros for details.
int MeasureAllocator(Allocator& allocator, uint32_t mask);

/// Exercises a block allocator as part of a size report.
///
/// @param[in]  allocator   The block allocator to exercise.
/// @param[in]  mask        A bitmap that can be passed to `PW_BLOAT_COND` and
///                         `PW_BLOAT_EXPR`. See those macros for details.
int MeasureBlockAllocator(BlockAllocator<BlockType>& allocator, uint32_t mask);

// Template method implementations.

template <typename BlockType>
int MeasureBlock(uint32_t mask) {
  if (SetBaseline(mask) != 0) {
    return 1;
  }

  // Measure `Init`.
  auto result = BlockType::Init(GetBuffer());
  BlockType* block = *result;

  // Measure `UsableSpace`.
  std::byte* bytes = block->UsableSpace();

  // Measure `FromUsableSpace`.
  block = BlockType::FromUsableSpace(bytes);

  if constexpr (is_allocatable_v<BlockType>) {
    // Measure `AllocFirst`.
    Layout foo = Layout::Of<Foo>();
    auto block_result = BlockType::AllocFirst(std::move(block), foo);
    if (!block_result.ok()) {
      return 1;
    }

    BlockType* first_block = block_result.block();
    block = first_block->Next();

    // Measure `AllocLast`.
    if constexpr (is_alignable_v<BlockType>) {
      constexpr Layout kOverlyAligned(128, 64);
      block_result = BlockType::AllocLast(std::move(block), kOverlyAligned);
    } else {
      Layout baz = Layout::Of<Baz>();
      block_result = BlockType::AllocLast(std::move(block), baz);
    }
    if (!block_result.ok()) {
      return 1;
    }

    BlockType* last_block = block_result.block();
    block = last_block->Prev();

    // Measure `Resize`.
    block_result = block->Resize(sizeof(Bar));
    if (!block_result.ok()) {
      return 1;
    }

    // Measure `Free`.
    block_result = BlockType::Free(std::move(first_block));
    return block_result.ok() ? 0 : 1;
  }
}

}  // namespace pw::allocator::size_report
