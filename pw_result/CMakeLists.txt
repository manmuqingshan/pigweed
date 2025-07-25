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

pw_add_library(pw_result INTERFACE
  HEADERS
    public/pw_result/result.h
    public/pw_result/internal/result_internal.h
  PUBLIC_INCLUDES
    public
  PUBLIC_DEPS
    pw_assert
    pw_status
    pw_preprocessor
)

pw_add_library(pw_result.expected INTERFACE
  HEADERS
    public/pw_result/expected.h
    public/pw_result/internal/expected_impl.h
  PUBLIC_INCLUDES
    public
)

pw_add_test(pw_result.result_test
  SOURCES
    result_test.cc
  PRIVATE_DEPS
    pw_result
    pw_status
  GROUPS
    modules
    pw_result
)

pw_add_test(pw_result.expected_test
  SOURCES
    expected_test.cc
  PRIVATE_DEPS
    pw_result.expected
    pw_string
    pw_status
  GROUPS
    modules
    pw_result
)
