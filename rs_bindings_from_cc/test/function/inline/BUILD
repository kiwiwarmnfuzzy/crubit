"""End-to-end example of using a simple inline function."""


load("//rs_bindings_from_cc/test:test_bindings.bzl", "crubit_test_cc_library")

package(default_applicable_licenses = ["//:license"])

crubit_test_cc_library(
    name = "hello_world",
    hdrs = ["hello_world.h"],
)

rust_test(
    name = "main",
    srcs = ["test.rs"],
    cc_deps = [":hello_world"],
)
