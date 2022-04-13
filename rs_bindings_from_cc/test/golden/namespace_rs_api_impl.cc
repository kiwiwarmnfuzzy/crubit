// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <cstddef>
#include <memory>

#include "rs_bindings_from_cc/support/cxx20_backports.h"
#include "rs_bindings_from_cc/support/offsetof.h"
#include "rs_bindings_from_cc/test/golden/namespace.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-analysis"
extern "C" void __rust_thunk___ZN23test_namespace_bindings1SC1Ev(
    class S* __this) {
  crubit::construct_at(std::forward<decltype(__this)>(__this));
}
extern "C" void __rust_thunk___ZN23test_namespace_bindings1SC1ERKS0_(
    class S* __this, const class S& __param_0) {
  crubit::construct_at(std::forward<decltype(__this)>(__this),
                       std::forward<decltype(__param_0)>(__param_0));
}
extern "C" void __rust_thunk___ZN23test_namespace_bindings1SD1Ev(
    class S* __this) {
  std::destroy_at(std::forward<decltype(__this)>(__this));
}
extern "C" class S& __rust_thunk___ZN23test_namespace_bindings1SaSERKS0_(
    class S* __this, const class S& __param_0) {
  return __this->operator=(std::forward<decltype(__param_0)>(__param_0));
}

static_assert(sizeof(class S) == 4);
static_assert(alignof(class S) == 4);
static_assert(CRUBIT_OFFSET_OF(i, class S) * 8 == 0);

#pragma clang diagnostic pop
