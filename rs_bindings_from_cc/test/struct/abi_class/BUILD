"""End-to-end test of ABI classification of structs."""


load("//rs_bindings_from_cc/test:test_bindings.bzl", "crubit_test_cc_library")

package(default_applicable_licenses = ["//:license"])

crubit_test_cc_library(
    name = "abi_class",
    hdrs = ["abi_class.h"],
)

rust_test(
    name = "main",
    srcs = ["test.rs"],
    cc_deps = [":abi_class"],
)
