"""End-to-end example of using fully-instantiated templates as function return types."""


load("//rs_bindings_from_cc/test:test_bindings.bzl", "crubit_test_cc_library")

package(default_applicable_licenses = ["//:license"])

crubit_test_cc_library(
    name = "struct_fields",
    hdrs = ["struct_fields.h"],
)

rust_test(
    name = "main",
    srcs = ["test.rs"],
    cc_deps = [":struct_fields"],
    deps = ["//support:ctor"],
)
