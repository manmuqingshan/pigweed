# Copyright 2024 The Pigweed Authors
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.

include($ENV{PW_ROOT}/pw_build/pigweed.cmake)

pw_add_module_config(pw_malloc_CONFIG)

pw_add_facade(pw_malloc INTERFACE
  BACKEND
    pw_malloc_BACKEND
  HEADERS
    public/pw_malloc/malloc.h
  PUBLIC_INCLUDES
    public
  PUBLIC_DEPS
    pw_malloc.config
  SOURCES
    malloc.cc
)

pw_add_library(pw_malloc.config INTERFACE
  PUBLIC_INCLUDES
    .
    public
  HEADERS
    public/pw_malloc/config.h
  PUBLIC_DEPS
    pw_malloc.common
    ${pw_malloc_CONFIG}
)

pw_add_library(pw_malloc.common INTERFACE
  PUBLIC_INCLUDES
    public
  HEADERS
    public/pw_malloc/config.h
  PUBLIC_DEPS
    pw_allocator.allocator
    pw_allocator.synchronized_allocator
    pw_allocator.tracking_allocator
    pw_assert
    pw_bytes
    pw_numeric.checked_arithmetic
  PUBLIC_LINK_OPTIONS
    "-Wl,--wrap=malloc"
    "-Wl,--wrap=free"
    "-Wl,--wrap=realloc"
    "-Wl,--wrap=calloc"
    "-Wl,--wrap=_malloc_r"
    "-Wl,--wrap=_realloc_r"
    "-Wl,--wrap=_free_r"
    "-Wl,--wrap=_calloc_r"
)

# Allocator-based backends.

pw_add_library(pw_malloc.best_fit STATIC
  PUBLIC_DEPS
    pw_malloc.facade
  PRIVATE_DEPS
    pw_allocator.best_fit
  SOURCES
    best_fit.cc
)

pw_add_library(pw_malloc.bucket_allocator STATIC
  PUBLIC_DEPS
    pw_malloc.facade
  PRIVATE_DEPS
    pw_allocator.bucket_allocator
  SOURCES
    bucket_allocator.cc
)

# TODO(b/376730645): Remove deprecated backends.
pw_add_library(pw_malloc.bucket_block_allocator INTERFACE
  PUBLIC_DEPS
    pw_malloc.facade
    pw_allocator.bucket_allocator
)

pw_add_library(pw_malloc.first_fit STATIC
  PUBLIC_DEPS
    pw_malloc.facade
  PRIVATE_DEPS
    pw_allocator.first_fit
  SOURCES
  first_fit.cc
)

pw_add_library(pw_malloc.worst_fit STATIC
  PUBLIC_DEPS
    pw_malloc.facade
  PRIVATE_DEPS
    pw_allocator.worst_fit
  SOURCES
    worst_fit.cc
)

# TODO(b/343531095): Add unit tests to CMake build whenever the following
# conditions are met:
# * The backend is not set.
# * The host is not "mac", due to missing linker support.
# * No sanitizer is configured, due to conflicts with interceptors.
# * pico_malloc is not in use.
# * gtest is not in use, since it dynamically allocates before calling SetUp().
