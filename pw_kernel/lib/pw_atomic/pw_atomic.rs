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
#![no_std]

use core::sync::atomic::Ordering;

pub trait AtomicAdd<T> {
    // Adds `val` to the current value, returning the previous value.
    //
    // Behaves the same as [`core::atomic::AtomicUsize::fetch_add()`].
    fn fetch_add(&self, val: T, order: Ordering) -> T;
}

pub trait AtomicSub<T> {
    // Subtracts `val` to the current value, returning the previous value.
    //
    // Behaves the same as [`core::atomic::AtomicUsize::fetch_sub()`].
    fn fetch_sub(&self, val: T, order: Ordering) -> T;
}

pub trait AtomicLoad<T> {
    // Loads and returns the current value.
    //
    // Behaves the same as [`core::atomic::AtomicUsize::load()`].
    fn load(&self, order: Ordering) -> T;
}

pub trait AtomicNew<T> {
    // Returns a new atomic with `val`
    fn new(val: T) -> Self;
}

pub trait Atomic<T>: AtomicNew<T> + AtomicLoad<T> + AtomicAdd<T> + AtomicSub<T> {}
pub trait AtomicUsize: Atomic<usize> {}

macro_rules! impl_for_type {
    ($atomic_type:ty, $primitive_type:ty) => {
        impl AtomicAdd<$primitive_type> for $atomic_type {
            fn fetch_add(&self, val: $primitive_type, ordering: Ordering) -> $primitive_type {
                self.fetch_add(val, ordering)
            }
        }

        impl AtomicSub<$primitive_type> for $atomic_type {
            fn fetch_sub(&self, val: $primitive_type, ordering: Ordering) -> $primitive_type {
                self.fetch_sub(val, ordering)
            }
        }

        impl AtomicLoad<$primitive_type> for $atomic_type {
            fn load(&self, ordering: Ordering) -> $primitive_type {
                self.load(ordering)
            }
        }

        impl AtomicNew<$primitive_type> for $atomic_type {
            fn new(val: $primitive_type) -> Self {
                Self::new(val)
            }
        }

        impl Atomic<$primitive_type> for $atomic_type {}
    };
}

impl_for_type!(core::sync::atomic::AtomicUsize, usize);
impl AtomicUsize for core::sync::atomic::AtomicUsize {}
