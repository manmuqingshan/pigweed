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

#include "pw_perf_test/event_handler.h"

namespace pw::perf_test {

class LogCsvEventHandler : public EventHandler {
 public:
  LogCsvEventHandler() : iterations_(0) {}

  void RunAllTestsStart(const TestRunInfo& summary) override;
  void RunAllTestsEnd() override;
  void TestCaseStart(const TestCase& info) override;
  void TestCaseIteration(const TestIteration& iteration) override;
  void TestCaseEnd(const TestCase& info,
                   const TestMeasurement& measurement) override;

 private:
  int iterations_;
};

}  // namespace pw::perf_test
