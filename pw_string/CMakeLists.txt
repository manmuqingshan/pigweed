# Copyright 2020 The Pigweed Authors
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

pw_add_module_config(pw_string_CONFIG)

pw_add_library(pw_string.config INTERFACE
  HEADERS
    public/pw_string/internal/config.h
  PUBLIC_INCLUDES
    public
  PUBLIC_DEPS
    ${pw_string_CONFIG}
)

pw_add_library(pw_string INTERFACE
  PUBLIC_DEPS
    pw_string.builder
    pw_string.format
    pw_string.to_string
    pw_string.util
)

pw_add_library(pw_string.builder STATIC
  HEADERS
    public/pw_string/string_builder.h
  PUBLIC_INCLUDES
    public
  PUBLIC_DEPS
    pw_string.format
    pw_string.string
    pw_string.to_string
    pw_string.util
    pw_preprocessor
    pw_status
  SOURCES
    string_builder.cc
)

pw_add_library(pw_string.hex INTERFACE
  HEADERS
    public/pw_string/hex.h
  PUBLIC_INCLUDES
    public
)

pw_add_library(pw_string.format STATIC
  HEADERS
    public/pw_string/format.h
  PUBLIC_INCLUDES
    public
  PUBLIC_DEPS
    pw_preprocessor
    pw_span
    pw_status
    pw_string.string
  SOURCES
    format.cc
)

pw_add_library(pw_string.string INTERFACE
  HEADERS
    public/pw_string/string.h
    public/pw_string/internal/string_common_functions.inc
    public/pw_string/internal/string_impl.h
  PUBLIC_INCLUDES
    public
  PUBLIC_DEPS
    pw_assert
    pw_containers._common
    pw_containers.iterator
)

pw_add_library(pw_string.to_string STATIC
  HEADERS
    public/pw_string/to_string.h
    public/pw_string/type_to_string.h
  PUBLIC_INCLUDES
    public
  PUBLIC_DEPS
    pw_result
    pw_string.config
    pw_string.format
    pw_string.util
    pw_status
    pw_third_party.fuchsia.stdcompat
  SOURCES
    type_to_string.cc
)

pw_add_library(pw_string.utf_codecs STATIC
  HEADERS
    public/pw_string/utf_codecs.h
  PUBLIC_INCLUDES
    public
  PUBLIC_DEPS
    pw_assert
    pw_result
    pw_span
    pw_status
    pw_string.builder
  SOURCES
    utf_codecs.cc
)

pw_add_library(pw_string.util INTERFACE
  HEADERS
    public/pw_string/util.h
    public/pw_string/internal/length.h
  PUBLIC_INCLUDES
    public
  PUBLIC_DEPS
    pw_assert
    pw_result
    pw_span
    pw_status
    pw_string.string
)

pw_add_test(pw_string.format_test
  SOURCES
    format_test.cc
  PRIVATE_DEPS
    pw_string
  GROUPS
    modules
    pw_string
)

pw_add_test(pw_string.hex_test
  SOURCES
    hex_test.cc
  PRIVATE_DEPS
    pw_string.hex
  GROUPS
    modules
    pw_string
)

pw_add_test(pw_string.string_builder_test
  SOURCES
    string_builder_test.cc
  PRIVATE_DEPS
    pw_string
  GROUPS
    modules
    pw_string
)

pw_add_test(pw_string.string_test
  SOURCES
    string_test.cc
  PRIVATE_DEPS
    pw_compilation_testing._pigweed_only_negative_compilation
    pw_string.string
  GROUPS
    modules
    pw_string
)

pw_add_test(pw_string.to_string_test
  SOURCES
    to_string_test.cc
  PRIVATE_DEPS
    pw_string
    pw_string.config
  GROUPS
    modules
    pw_string
)

pw_add_test(pw_string.type_to_string_test
  SOURCES
    type_to_string_test.cc
  PRIVATE_DEPS
    pw_string
  GROUPS
    modules
    pw_string
)

pw_add_test(pw_string.utf_codecs_test
  SOURCES
    utf_codecs_test.cc
  PRIVATE_DEPS
    pw_string
    pw_string.utf_codecs
  GROUPS
    modules
    pw_string
)

pw_add_test(pw_string.util_test
  SOURCES
    util_test.cc
  PRIVATE_DEPS
    pw_string
  GROUPS
    modules
    pw_string
)
