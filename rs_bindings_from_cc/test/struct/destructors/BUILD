"""End-to-end test for destructors."""


load("//rs_bindings_from_cc/test:test_bindings.bzl", "crubit_test_cc_library")

package(default_applicable_licenses = ["//:license"])

crubit_test_cc_library(
    name = "field_destruction_order",
    srcs = ["field_destruction_order.cc"],
    hdrs = ["field_destruction_order.h"],
)

rust_test(
    name = "main",
    srcs = ["test.rs"],
    cc_deps = [":field_destruction_order"],
)
