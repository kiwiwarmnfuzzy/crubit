#![rustfmt::skip]
// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#![feature(const_ptr_offset_from, custom_inner_attributes, negative_impls)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use memoffset_unstable_const::offset_of;

pub type __builtin_ms_va_list = *mut u8;

/// Implicitly defined special member functions are trivial on a struct with
/// only trivial members.
#[derive(Clone, Copy)]
#[repr(C)]
pub struct Trivial {
    pub trivial_field: i32,
}

impl Default for Trivial {
    #[inline(always)]
    fn default() -> Self {
        let mut tmp = std::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN7TrivialC1Ev(&mut tmp);
            tmp.assume_init()
        }
    }
}

impl From<*const Trivial> for Trivial {
    #[inline(always)]
    fn from(__param_0: *const Trivial) -> Self {
        let mut tmp = std::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN7TrivialC1ERKS_(&mut tmp, __param_0);
            tmp.assume_init()
        }
    }
}

// rs_bindings_from_cc/test/golden/trivial_type.h;l=8
// Error while generating bindings for item 'Trivial::Trivial':
// Parameter type 'struct Trivial &&' is not supported

// rs_bindings_from_cc/test/golden/trivial_type.h;l=8
// Error while generating bindings for item 'Trivial::operator=':
// Parameter type 'struct Trivial &&' is not supported

/// Defaulted special member functions are trivial on a struct with only trivial
/// members.
#[derive(Clone, Copy)]
#[repr(C)]
pub struct TrivialWithDefaulted {
    pub trivial_field: i32,
}

impl Default for TrivialWithDefaulted {
    #[inline(always)]
    fn default() -> Self {
        let mut tmp = std::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN20TrivialWithDefaultedC1Ev(&mut tmp);
            tmp.assume_init()
        }
    }
}

// rs_bindings_from_cc/test/golden/trivial_type.h;l=19
// Error while generating bindings for item 'TrivialWithDefaulted::TrivialWithDefaulted':
// Parameter type 'struct TrivialWithDefaulted &&' is not supported

// rs_bindings_from_cc/test/golden/trivial_type.h;l=20
// Error while generating bindings for item 'TrivialWithDefaulted::operator=':
// Parameter type 'struct TrivialWithDefaulted &&' is not supported

/// This struct is trivial, and therefore trivially relocatable etc., but still
/// not safe to pass by reference as it is not final.
#[repr(C)]
pub struct TrivialNonfinal {
    pub trivial_field: i32,
}

impl !Unpin for TrivialNonfinal {}

// rs_bindings_from_cc/test/golden/trivial_type.h;l=29
// Error while generating bindings for item 'TrivialNonfinal::TrivialNonfinal':
// Parameter type 'struct TrivialNonfinal &&' is not supported

// rs_bindings_from_cc/test/golden/trivial_type.h;l=29
// Error while generating bindings for item 'TrivialNonfinal::operator=':
// Parameter type 'struct TrivialNonfinal &&' is not supported

#[inline(always)]
pub fn TakesByValue(trivial: Trivial) {
    unsafe { crate::detail::__rust_thunk___Z12TakesByValue7Trivial(trivial) }
}

#[inline(always)]
pub fn TakesWithDefaultedByValue(trivial: TrivialWithDefaulted) {
    unsafe {
        crate::detail::__rust_thunk___Z25TakesWithDefaultedByValue20TrivialWithDefaulted(trivial)
    }
}

#[inline(always)]
pub fn TakesTrivialNonfinalByValue(trivial: TrivialNonfinal) {
    unsafe {
        crate::detail::__rust_thunk___Z27TakesTrivialNonfinalByValue15TrivialNonfinal(trivial)
    }
}

#[inline(always)]
pub fn TakesByReference<'a>(trivial: &'a mut Trivial) {
    unsafe { crate::detail::__rust_thunk___Z16TakesByReferenceR7Trivial(trivial) }
}

#[inline(always)]
pub fn TakesWithDefaultedByReference<'a>(trivial: &'a mut TrivialWithDefaulted) {
    unsafe {
        crate::detail::__rust_thunk___Z29TakesWithDefaultedByReferenceR20TrivialWithDefaulted(
            trivial,
        )
    }
}

#[inline(always)]
pub fn TakesTrivialNonfinalByReference<'a>(trivial: &'a mut TrivialNonfinal) {
    unsafe {
        crate::detail::__rust_thunk___Z31TakesTrivialNonfinalByReferenceR15TrivialNonfinal(trivial)
    }
}

// CRUBIT_RS_BINDINGS_FROM_CC_TEST_GOLDEN_TRIVIAL_TYPE_H_

mod detail {
    #[allow(unused_imports)]
    use super::*;
    extern "C" {
        pub(crate) fn __rust_thunk___ZN7TrivialC1Ev(__this: &mut std::mem::MaybeUninit<Trivial>);
        pub(crate) fn __rust_thunk___ZN7TrivialC1ERKS_(
            __this: &mut std::mem::MaybeUninit<Trivial>,
            __param_0: *const Trivial,
        );
        pub(crate) fn __rust_thunk___ZN20TrivialWithDefaultedC1Ev<'a>(
            __this: &'a mut std::mem::MaybeUninit<TrivialWithDefaulted>,
        );
        #[link_name = "_Z12TakesByValue7Trivial"]
        pub(crate) fn __rust_thunk___Z12TakesByValue7Trivial(trivial: Trivial);
        #[link_name = "_Z25TakesWithDefaultedByValue20TrivialWithDefaulted"]
        pub(crate) fn __rust_thunk___Z25TakesWithDefaultedByValue20TrivialWithDefaulted(
            trivial: TrivialWithDefaulted,
        );
        #[link_name = "_Z27TakesTrivialNonfinalByValue15TrivialNonfinal"]
        pub(crate) fn __rust_thunk___Z27TakesTrivialNonfinalByValue15TrivialNonfinal(
            trivial: TrivialNonfinal,
        );
        #[link_name = "_Z16TakesByReferenceR7Trivial"]
        pub(crate) fn __rust_thunk___Z16TakesByReferenceR7Trivial<'a>(trivial: &'a mut Trivial);
        #[link_name = "_Z29TakesWithDefaultedByReferenceR20TrivialWithDefaulted"]
        pub(crate) fn __rust_thunk___Z29TakesWithDefaultedByReferenceR20TrivialWithDefaulted<'a>(
            trivial: &'a mut TrivialWithDefaulted,
        );
        #[link_name = "_Z31TakesTrivialNonfinalByReferenceR15TrivialNonfinal"]
        pub(crate) fn __rust_thunk___Z31TakesTrivialNonfinalByReferenceR15TrivialNonfinal<'a>(
            trivial: &'a mut TrivialNonfinal,
        );
    }
}

const _: () = assert!(std::mem::size_of::<Option<&i32>>() == std::mem::size_of::<&i32>());

const _: () = assert!(std::mem::size_of::<Trivial>() == 4usize);
const _: () = assert!(std::mem::align_of::<Trivial>() == 4usize);
const _: () = assert!(offset_of!(Trivial, trivial_field) * 8 == 0usize);

const _: () = assert!(std::mem::size_of::<TrivialWithDefaulted>() == 4usize);
const _: () = assert!(std::mem::align_of::<TrivialWithDefaulted>() == 4usize);
const _: () = assert!(offset_of!(TrivialWithDefaulted, trivial_field) * 8 == 0usize);

const _: () = assert!(std::mem::size_of::<TrivialNonfinal>() == 4usize);
const _: () = assert!(std::mem::align_of::<TrivialNonfinal>() == 4usize);
const _: () = assert!(offset_of!(TrivialNonfinal, trivial_field) * 8 == 0usize);
