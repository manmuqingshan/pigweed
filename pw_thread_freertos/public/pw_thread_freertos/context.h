// Copyright 2020 The Pigweed Authors
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

#include <cstdint>

#include "FreeRTOS.h"
#include "pw_function/function.h"
#include "pw_span/span.h"
#include "pw_thread_freertos/config.h"
#include "task.h"
#if PW_THREAD_JOINING_ENABLED
#include "event_groups.h"
#endif  // PW_THREAD_JOINING_ENABLED

namespace pw::thread {

class Thread;  // Forward declare Thread which depends on Context.

}  // namespace pw::thread

namespace pw::thread::freertos {

class Options;

// FreeRTOS may be used for dynamic thread TCB and stack allocation, but
// because we need some additional context beyond that the concept of a
// thread's context is split into two halves:
//
//   1) Context which just contains the additional Context pw::Thread requires.
//      This is used for both static and dynamic thread allocations.
//
//   2) StaticContext which contains the TCB and a span to the stack which is
//      used only for static allocations.
class Context {
 public:
  Context() = default;
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

 private:
  friend Thread;
  static void CreateThread(const freertos::Options& options,
                           Function<void()>&& thread_fn,
                           Context*& native_type_out);
  void AddToEventGroup();

  TaskHandle_t task_handle() const { return task_handle_; }
  void set_task_handle(const TaskHandle_t task_handle) {
    task_handle_ = task_handle;
  }

  void set_thread_routine(Function<void()>&& rvalue) {
    fn_ = std::move(rvalue);
  }

  bool detached() const { return detached_; }
  void set_detached(bool value = true) { detached_ = value; }

  bool thread_done() const { return thread_done_; }
  void set_thread_done(bool value = true) { thread_done_ = value; }

  static void ThreadEntryPoint(void* void_context_ptr);
  static void TerminateThread(Context& context);

  TaskHandle_t task_handle_ = nullptr;
  Function<void()> fn_;

#if PW_THREAD_JOINING_ENABLED
  StaticEventGroup_t& join_event_group() { return event_group_; }

  // Note that the FreeRTOS life cycle of this event group is managed together
  // with the task life cycle, not this object's life cycle.
  StaticEventGroup_t event_group_;
#endif  // PW_THREAD_JOINING_ENABLED

#if PW_THREAD_FREERTOS_CONFIG_DYNAMIC_ALLOCATION_ENABLED
  bool dynamically_allocated() const { return dynamically_allocated_; }
  void set_dynamically_allocated() { dynamically_allocated_ = true; }

  bool dynamically_allocated_ = false;
#endif  // PW_THREAD_FREERTOS_CONFIG_DYNAMIC_ALLOCATION_ENABLED

  bool detached_ = false;
  bool thread_done_ = false;
};

// Static thread context allocation including the TCB, an event group for
// joining if enabled, and an external statically allocated stack.
//
// Example usage:
//
//   std::array<StackType_t, kFooStackSizeWords> example_thread_stack;
//   pw::thread::freertos::Context example_thread_context(example_thread_stack);
//   void StartExampleThread() {
//      pw::Thread(
//        pw::thread::freertos::Options()
//            .set_name("static_example_thread")
//            .set_priority(kFooPriority)
//            .set_static_context(example_thread_context),
//        example_thread_function).detach();
//   }
class StaticContext : public Context {
 public:
  explicit StaticContext(span<StackType_t> stack_span)
      : tcb_{}, stack_span_(stack_span) {}

 private:
  friend Context;

  StaticTask_t& tcb() { return tcb_; }
  span<StackType_t> stack() { return stack_span_; }

  StaticTask_t tcb_;
  span<StackType_t> stack_span_;
};

// Static thread context allocation including the stack along with the Context.
//
// Example usage:
//
//   pw::thread::freertos::ContextWithStack<kFooStackSizeWords>
//       example_thread_context;
//   void StartExampleThread() {
//      pw::Thread(
//        pw::thread::freertos::Options()
//            .set_name("static_example_thread")
//            .set_priority(kFooPriority)
//            .set_static_context(example_thread_context),
//        example_thread_function).detach();
//   }
template <size_t kStackSizeWords = config::kDefaultStackSizeWords>
class StaticContextWithStack final : public StaticContext {
 public:
  constexpr StaticContextWithStack() : StaticContext(stack_storage_) {
    static_assert(kStackSizeWords >= config::kMinimumStackSizeWords);
  }

 private:
  std::array<StackType_t, kStackSizeWords> stack_storage_;
};

}  // namespace pw::thread::freertos
