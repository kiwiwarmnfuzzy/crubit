#![rustfmt::skip]
// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#![feature(const_ptr_offset_from, custom_inner_attributes)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use memoffset_unstable_const::offset_of;

pub type __builtin_ms_va_list = *mut u8;

#[derive(Clone, Copy)]
#[repr(C)]
pub struct FirstStruct {
    pub field: i32,
}

impl Default for FirstStruct {
    #[inline(always)]
    fn default() -> Self {
        let mut tmp = std::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN11FirstStructC1Ev(&mut tmp);
            tmp.assume_init()
        }
    }
}

impl From<*const FirstStruct> for FirstStruct {
    #[inline(always)]
    fn from(__param_0: *const FirstStruct) -> Self {
        let mut tmp = std::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN11FirstStructC1ERKS_(&mut tmp, __param_0);
            tmp.assume_init()
        }
    }
}

// rs_bindings_from_cc/test/golden/item_order.h;l=6
// Error while generating bindings for item 'FirstStruct::FirstStruct':
// Parameter type 'struct FirstStruct &&' is not supported

// rs_bindings_from_cc/test/golden/item_order.h;l=6
// Error while generating bindings for item 'FirstStruct::operator=':
// Parameter type 'struct FirstStruct &&' is not supported

#[inline(always)]
pub fn first_func() -> i32 {
    unsafe { crate::detail::__rust_thunk___Z10first_funcv() }
}

#[derive(Clone, Copy)]
#[repr(C)]
pub struct SecondStruct {
    pub field: i32,
}

impl Default for SecondStruct {
    #[inline(always)]
    fn default() -> Self {
        let mut tmp = std::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN12SecondStructC1Ev(&mut tmp);
            tmp.assume_init()
        }
    }
}

impl From<*const SecondStruct> for SecondStruct {
    #[inline(always)]
    fn from(__param_0: *const SecondStruct) -> Self {
        let mut tmp = std::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN12SecondStructC1ERKS_(&mut tmp, __param_0);
            tmp.assume_init()
        }
    }
}

// rs_bindings_from_cc/test/golden/item_order.h;l=12
// Error while generating bindings for item 'SecondStruct::SecondStruct':
// Parameter type 'struct SecondStruct &&' is not supported

// rs_bindings_from_cc/test/golden/item_order.h;l=12
// Error while generating bindings for item 'SecondStruct::operator=':
// Parameter type 'struct SecondStruct &&' is not supported

#[inline(always)]
pub fn second_func() -> i32 {
    unsafe { crate::detail::__rust_thunk___Z11second_funcv() }
}

// CRUBIT_RS_BINDINGS_FROM_CC_TEST_GOLDEN_ITEM_ORDER_H_

mod detail {
    #[allow(unused_imports)]
    use super::*;
    extern "C" {
        pub(crate) fn __rust_thunk___ZN11FirstStructC1Ev(
            __this: &mut std::mem::MaybeUninit<FirstStruct>,
        );
        pub(crate) fn __rust_thunk___ZN11FirstStructC1ERKS_(
            __this: &mut std::mem::MaybeUninit<FirstStruct>,
            __param_0: *const FirstStruct,
        );
        pub(crate) fn __rust_thunk___Z10first_funcv() -> i32;
        pub(crate) fn __rust_thunk___ZN12SecondStructC1Ev(
            __this: &mut std::mem::MaybeUninit<SecondStruct>,
        );
        pub(crate) fn __rust_thunk___ZN12SecondStructC1ERKS_(
            __this: &mut std::mem::MaybeUninit<SecondStruct>,
            __param_0: *const SecondStruct,
        );
        pub(crate) fn __rust_thunk___Z11second_funcv() -> i32;
    }
}

const _: () = assert!(std::mem::size_of::<Option<&i32>>() == std::mem::size_of::<&i32>());

const _: () = assert!(std::mem::size_of::<FirstStruct>() == 4usize);
const _: () = assert!(std::mem::align_of::<FirstStruct>() == 4usize);
const _: () = assert!(offset_of!(FirstStruct, field) * 8 == 0usize);

const _: () = assert!(std::mem::size_of::<SecondStruct>() == 4usize);
const _: () = assert!(std::mem::align_of::<SecondStruct>() == 4usize);
const _: () = assert!(offset_of!(SecondStruct, field) * 8 == 0usize);
