"""End-to-end test of inheritance."""


load("//rs_bindings_from_cc/test:test_bindings.bzl", "crubit_test_cc_library")

package(default_applicable_licenses = ["//:license"])

crubit_test_cc_library(
    name = "inline",
    hdrs = ["inline.h"],
)

rust_test(
    name = "test",
    srcs = ["test.rs"],
    cc_deps = [":inline"],
)
