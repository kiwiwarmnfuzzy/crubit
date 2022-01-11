// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "rs_bindings_from_cc/test/struct/constructors/elided_lifetimes.h"

ElidedLifetimes::ElidedLifetimes() : int_field(456) {}

ElidedLifetimes::ElidedLifetimes(const ElidedLifetimes& other)
    : int_field(10000 + other.int_field) {}

ElidedLifetimes::ElidedLifetimes(int i) : int_field(i) {}
