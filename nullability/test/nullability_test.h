// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// This header defines functions available in nullability_tests.
//
// A test is a C++ source file that contains code to be analyzed.
// Any functions marked with TEST are analysis targets.
// These can include calls to assertion functions like nullable() defined here.
// Such calls assert details of analysis results (nullability of expressions).
//
// The nullability_test tool parses the code, runs the analysis, checks the
// assertions, and reports results.
//
// Example:
//  #include "nullability_test.h"
//  TEST void controlFlow(Nullable<int*> x) {
//    if (x) {
//      nonnull(x);
//    } else {
//      nullable(x);
//    }
//  }

#ifndef CRUBIT_NULLABILITY_TEST_NULLABILITY_TEST_H_
#define CRUBIT_NULLABILITY_TEST_NULLABILITY_TEST_H_

namespace preamble_detail {
template <typename, typename>
struct require_same;
template <typename T>
struct require_same<T, T> {
  using type = T;
};
}  // namespace preamble_detail

// Attribute applied to tests to be analyzed.
// For now, only functions are supported (including constructors).
// If TEST is applied to an unsupported construct, the test will fail.
#define TEST [[clang::annotate("test")]]

////////////// Assertion functions interpreted by the test driver /////////////

// Non-flow-sensitive analysis assertions.
// (These check the nullability vector of an expression's type).

// Asserts the exact static type and nullability of an expression.
// e.g. type<Nonnull<int*>(&i);
template <
    typename Expected, typename Actual,
    // Statically verify that the canonical types are the same.
    typename = typename preamble_detail::require_same<Expected, Actual>::type>
void type(Actual) {}

// Assertions for the full (flow-sensitive) analysis results.
// (These check whether is_known and is_null are implied by the flow condition).

// Asserts that its argument is considered nullable.
template <typename T>
void nullable(T) {}
// Asserts that its argument is considered non-null.
template <typename T>
void nonnull(T) {}
// Asserts that its argument is neither considered nullable nor non-null.
template <typename T>
void unknown(T) {}

///////////////// Helpers to make writing tests more convenient ////////////////

template <typename T>
using Nullable [[clang::annotate("Nullable")]] = T;
template <typename T>
using Nonnull [[clang::annotate("Nonnull")]] = T;

// Generic factory for generating values of arbitrary types and nullability.
//
// `make<Nullable<int*>>()` is a value whose type in the AST is `int*` (no
// nullability sugar) and whose static nullability is [Nullable].
template <typename T>
static T make()
    // suppresses 'undefined' error when instantiated with no-linkage type.
    __attribute__((weakref("")));

// Tests tend to contain unused expressions like *x, so don't warn on them.
#pragma clang diagnostic ignored "-Wunused-value"
// Tests define functions that are not declared in any header.
#pragma clang diagnostic ignored "-Wmissing-prototypes"

#endif
