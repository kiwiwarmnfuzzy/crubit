# Part of the Crubit project, under the Apache License v2.0 with LLVM
# Exceptions. See /LICENSE for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

load(
    "//rs_bindings_from_cc/bazel_support:toolchain_headers.bzl",
    "bindings_for_toolchain_headers",
)
load("@bazel_skylib//lib:selects.bzl", "selects")

package(default_applicable_licenses = ["//:license"])

filegroup(
    name = "extra_rs_srcs_for_cc_std",
    srcs = glob([
        "*.rs",
    ]),
)

LIBCXX_HEADERS = [
    "algorithm",
    "any",
    "array",
    "atomic",
    "bitset",
    "cassert",
    "cctype",
    "cerrno",
    "cfenv",
    "cfloat",
    "charconv",
    "chrono",
    "cinttypes",
    "climits",
    "cmath",
    "condition_variable",
    "csetjmp",
    "csignal",
    "cstdarg",
    "cstddef",
    "cstdint",
    "cstdio",
    "cstdlib",
    "cstring",
    "ctime",
    "cuchar",
    "cwchar",
    "cwctype",
    "deque",
    "exception",
    "forward_list",
    "functional",
    "initializer_list",
    "ios",
    "limits",
    "list",
    "map",
    "memory",
    "mutex",
    "new",
    "numeric",
    "optional",
    "queue",
    "ratio",
    "set",
    "scoped_allocator",
    "stack",
    "stdexcept",
    "streambuf",
    "string",
    "string_view",
    "system_error",
    "tuple",
    "type_traits",
    "typeindex",
    "typeinfo",
    "unordered_map",
    "unordered_set",
    "utility",
    "variant",
    # "vector", # TODO(b/248542210): include vector when we can conditionally import methods.
]

LIBC_HEADERS = []

_CPU = {
    "arm": "//third_party/bazel_platforms/cpu:aarch64",
    "x86": "//third_party/bazel_platforms/cpu:x86_64",
}

_GRTE = {
    "v4": "//third_party/grte:grte_v4",
    "v5": "//third_party/grte:grte_v5",
}

[
    selects.config_setting_group(
        name = "grte_{grte_name}_cpu_{cpu_name}".format(
            cpu_name = cpu_name,
            grte_name = grte_name,
        ),
        match_all = [
            grte_config_setting,
            cpu_config_setting,
        ],
    )
    for (cpu_name, cpu_config_setting) in _CPU.items()
    for (grte_name, grte_config_setting) in _GRTE.items()
]

alias(
    name = "grte",
    actual = select({
        "grte_{grte}_cpu_{cpu}".format(
            cpu = cpu,
            grte = grte,
        ): "//third_party/grte/{grte}_{cpu}/release/usr/grte/{grte}:compile".format(
            cpu = cpu,
            grte = grte,
        )
        for cpu in _CPU.keys()
        for grte in _GRTE.keys()
    }),
)

filegroup(
    name = "toolchain_headers",
    srcs = select({
        "//third_party/crosstool:unstable_config": ["//nowhere/llvm:cc-std-headers"],
        "//conditions:default": ["//nowhere/llvm:cc-std-headers"],
    }) + [
        ":grte",
    ],
    visibility = ["//visibility:private"],
)

bindings_for_toolchain_headers(
    name = "cc_std",
    hdrs = ":toolchain_headers",
    extra_rs_srcs = [":extra_rs_srcs_for_cc_std"],
    public_libc_hdrs = LIBC_HEADERS,
    public_libcxx_hdrs = LIBCXX_HEADERS,
    visibility = [
        "//visibility:public",
    ],
)
