// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef THIRD_PARTY_CRUBIT_COMMON_TEST_MIDDLE_CC_LIB_H_
#define THIRD_PARTY_CRUBIT_COMMON_TEST_MIDDLE_CC_LIB_H_

#include "common/test/bidirectional_deps/leaf_rs_lib_cc_api.h"

#pragma clang lifetime_elision

namespace crubit {

inline leaf_rs_lib::LeafRsType Wrap(unsigned char x) {
  return leaf_rs_lib::wrap(x);
}
inline unsigned char Unwrap(leaf_rs_lib::LeafRsType x) {
  return leaf_rs_lib::unwrap(std::move(x));
}

}  // namespace crubit

#endif  // THIRD_PARTY_CRUBIT_COMMON_TEST_MIDDLE_CC_LIB_H_
