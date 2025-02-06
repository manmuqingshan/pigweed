// Copyright 2022 The Pigweed Authors
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

#include <new>
#include <type_traits>
#include <utility>

#include "pw_polyfill/language_feature_macros.h"

namespace pw {

/// Helper type to create a global or function-local static variable of type `T`
/// when `T` has a non-trivial destructor. Storing a `T` in a
/// `pw::NoDestructor<T>` will prevent `~T()` from running, even when the
/// variable goes out of scope.
///
/// This class is useful when a variable has static storage duration but its
/// type has a non-trivial destructor. Destructor ordering is not defined and
/// can cause issues in multithreaded environments. Additionally, removing
/// destructor calls can save code size.
///
/// Except in generic code, do not use `pw::NoDestructor<T>` with trivially
/// destructible types. Use the type directly instead. If the variable can be
/// `constexpr`, make it `constexpr`.
///
/// `pw::NoDestructor<T>` provides a similar API to `std::optional`. Use `*` or
/// `->` to access the wrapped type.
///
/// `NoDestructor` instances can be `constinit` if `T` has a `constexpr`
/// constructor. In C++20, `NoDestructor` instances may be `constexpr` if `T`
/// has a `constexpr` destructor. `NoDestructor` is unnecessary for literal
/// types.
///
/// @note `NoDestructor<T>` instances may be constant initialized, whether they
/// are `constinit` or not. This may be undesirable for large objects, since
/// moving them from `.bss` to `.data` increases binary size. To prevent this,
/// use `pw::RuntimeInitGlobal`, which prevents constant initialization and
/// removes the destructor.
///
/// Example usage:
/// @code{.cpp}
///
///   pw::sync::Mutex& GetMutex() {
///     // Use NoDestructor to avoid running the mutex destructor when exit-time
///     // destructors run.
///     static const pw::NoDestructor<pw::sync::Mutex> global_mutex;
///     return *global_mutex;
///   }
///
/// @endcode
///
/// In Clang, `pw::NoDestructor` can be replaced with the
/// <a href="https://clang.llvm.org/docs/AttributeReference.html#no-destroy">
/// [[clang::no_destroy]]</a> attribute. `pw::NoDestructor<T>` is similar to
/// Chromium’s `base::NoDestructor<T>` in <a
/// href="https://chromium.googlesource.com/chromium/src/base/+/5ea6e31f927aa335bfceb799a2007c7f9007e680/no_destructor.h">
/// src/base/no_destructor.h</a>.
///
/// @warning Misuse of `NoDestructor` can cause memory leaks and other problems.
/// Only skip destructors when you know it is safe to do so.
template <typename T>
class NoDestructor {
 public:
  using value_type = T;

  // Initializes a T in place.
  //
  // This overload is disabled when it might collide with copy/move.
  template <typename... Args,
            typename std::enable_if<!std::is_same<void(std::decay_t<Args>&...),
                                                  void(NoDestructor&)>::value,
                                    int>::type = 0>
  explicit constexpr NoDestructor(Args&&... args)
      : storage_(std::forward<Args>(args)...) {}

  // Move or copy from the contained type. This allows for construction from an
  // initializer list, e.g. for std::vector.
  explicit constexpr NoDestructor(const T& x) : storage_(x) {}
  explicit constexpr NoDestructor(T&& x) : storage_(std::move(x)) {}

  NoDestructor(const NoDestructor&) = delete;
  NoDestructor& operator=(const NoDestructor&) = delete;

  // Empty destructor. Not technically a trivial destructor, but does nothing.
  PW_CONSTEXPR_CPP20 ~NoDestructor() {}

  constexpr const T& operator*() const { return storage_; }
  constexpr T& operator*() { return storage_; }

  constexpr const T* operator->() const { return &storage_; }
  constexpr T* operator->() { return &storage_; }

 private:
  union {
    T storage_;
  };
};

}  // namespace pw
