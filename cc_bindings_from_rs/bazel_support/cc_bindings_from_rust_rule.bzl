# Part of the Crubit project, under the Apache License v2.0 with LLVM
# Exceptions. See /LICENSE for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""`cc_bindings_from_rust` rule.

Disclaimer: This project is experimental, under heavy development, and should
not be used yet.
"""

# buildifier: disable=bzl-visibility
load(
    "@rules_rust//rust/private:providers.bzl",
    "DepInfo",
    "DepVariantInfo",
)

# buildifier: disable=bzl-visibility
load(
    "@rules_rust//rust/private:rustc.bzl",
    "collect_deps",
    "collect_inputs",
    "construct_arguments",
)

# buildifier: disable=bzl-visibility
load(
    "@rules_rust//rust/private:utils.bzl",
    "find_toolchain",
)
load(
    "@rules_rust//rust:rust_common.bzl",
    "BuildInfo",
    "CrateInfo",
)
load(
    "//rs_bindings_from_cc/bazel_support:compile_rust.bzl",
    "compile_rust",
)
load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain", "use_cpp_toolchain")
load(
    "//rs_bindings_from_cc/bazel_support:providers.bzl",
    "RustBindingsFromCcInfo",
)

# Targets which do not receive C++ bindings at all.
targets_to_remove = [
]

CcBindingsFromRustInfo = provider(
    doc = ("A provider that contains compile and linking information for the generated" +
           " `.rs` and `.h` files."),
    fields = {
        "cc_info": "A CcInfo provider for the API projection.",
        # TODO(b/271857814): A `CRATE_NAME` might not be globally unique - the
        # key needs to also cover a "hash" of the crate version and compilation
        # flags.
        "crate_key": "String with a crate key to use in --other-crate-bindings",
        "h_out_file": "File object representing the generated ..._cc_api.h.",
    },
)

def _get_dep_bindings_infos(ctx):
    """Returns `CcBindingsFromRustInfo`s of direct, non-transitive dependencies.

    Only information about direct, non-transitive dependencies is needed,
    because bindings for the public APIs may need to refer to types from
    such dependencies (e.g. `fn foo(param: TypeFromDirectDependency)`),
    but they cannot refer to types from transitive dependencies.

    Args:
      ctx: The rule context.

    Returns:
      A list of `CcBindingsFromRustInfo`s of all the direct, non-transitive Rust
      dependencies (dependencies of the Rust crate being used as input for
      `cc_bindings_from_rs`).
    """
    return [
        dep[CcBindingsFromRustInfo]
        for dep in ctx.rule.attr.deps
        if CcBindingsFromRustInfo in dep
    ]

def _generate_bindings(ctx, basename, inputs, rustc_args, rustc_env):
    """Invokes the `cc_bindings_from_rs` tool to generate C++ bindings for a Rust crate.

    Args:
      ctx: The rule context.
      basename: The basename for the generated files
      inputs: `cc_bindings_from_rs` inputs specific to the target `crate`
      rustc_args: `rustc` flags to pass to `cc_bindings_from_rs`
      rustc_env: `rustc` environment to use when running `cc_bindings_from_rs`

    Returns:
      A pair of files:
      - h_out_file (named "<basename>_cc_api.h")
      - rs_out_file (named "<basename>_cc_api_impl.rs")
    """
    h_out_file = ctx.actions.declare_file(basename + "_cc_api.h")
    rs_out_file = ctx.actions.declare_file(basename + "_cc_api_impl.rs")

    crubit_args = ctx.actions.args()
    crubit_args.add("--h-out", h_out_file)
    crubit_args.add("--rs-out", rs_out_file)

    crubit_args.add("--crubit-support-path", "support")

    crubit_args.add("--clang-format-exe-path", ctx.file._clang_format)
    crubit_args.add("--rustfmt-exe-path", ctx.file._rustfmt)
    crubit_args.add("--rustfmt-config-path", ctx.file._rustfmt_cfg)

    for dep_bindings_info in _get_dep_bindings_infos(ctx):
        arg = dep_bindings_info.crate_key + "=" + dep_bindings_info.h_out_file.short_path
        crubit_args.add("--bindings-from-dependency", arg)

    ctx.actions.run(
        outputs = [h_out_file, rs_out_file],
        inputs = depset(
            [ctx.file._clang_format, ctx.file._rustfmt, ctx.file._rustfmt_cfg],
            transitive = [inputs],
        ),
        env = rustc_env,
        executable = ctx.executable._cc_bindings_from_rs_tool,
        mnemonic = "CcBindingsFromRust",
        progress_message = "Generating C++ bindings from Rust: %s" % h_out_file,
        # TODO(lukasza): Figure out why we need a '-Cpanic=abort' here.
        arguments = [crubit_args, "--", rustc_args, "-Cpanic=abort"],
    )

    return (h_out_file, rs_out_file)

def _make_cc_info_for_h_out_file(ctx, h_out_file, linking_contexts):
    """Creates and returns CcInfo for the generated ..._cc_api.h header file.

    Args:
      ctx: The rule context.
      h_out_file: The generated "..._cc_api.h" header file
      linking_contexts: Linking contexts - should include both:
          1) the target `crate` and
          2) the compiled Rust glue crate (`..._cc_api_impl.rs` file).

    Returns:
      A CcInfo provider.
    """
    dep_cc_infos = [
        dep[CcInfo]
        for dep in ctx.attr._cc_deps_for_bindings
    ] + [
        dep_bindings_info.cc_info
        for dep_bindings_info in _get_dep_bindings_infos(ctx)
    ]
    cc_deps_compilation_contexts = [
        dep_cc_info.compilation_context
        for dep_cc_info in dep_cc_infos
    ]
    cc_deps_linking_contexts = [
        dep_cc_info.linking_context
        for dep_cc_info in dep_cc_infos
    ]
    cc_toolchain = find_cpp_toolchain(ctx)
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
    )
    (compilation_context, compilation_outputs) = cc_common.compile(
        name = ctx.label.name,
        actions = ctx.actions,
        feature_configuration = feature_configuration,
        cc_toolchain = cc_toolchain,
        public_hdrs = [h_out_file],
        compilation_contexts = cc_deps_compilation_contexts,
    )
    (linking_context, _) = cc_common.create_linking_context_from_compilation_outputs(
        name = ctx.label.name,
        actions = ctx.actions,
        feature_configuration = feature_configuration,
        cc_toolchain = cc_toolchain,
        compilation_outputs = compilation_outputs,
        linking_contexts = linking_contexts + cc_deps_linking_contexts,
    )
    return CcInfo(
        compilation_context = compilation_context,
        linking_context = linking_context,
    )

def _compile_rs_out_file(ctx, rs_out_file, target):
    """Compiles the generated "..._cc_api_impl.rs" file.

    Args:
      ctx: The rule context.
      rs_out_file: The generated "..._cc_api_impl.rs" file
      target: The target crate, e.g. as provided to `ctx.attr.crate`.

    Returns:
      LinkingContext for linking in the generated "..._cc_api_impl.rs".
    """
    deps = [
        DepVariantInfo(
            crate_info = dep[CrateInfo],
            dep_info = dep[DepInfo],
            cc_info = dep[CcInfo],
            build_info = None,
        )
        for dep in ctx.attr._rs_deps_for_bindings + [target]
    ]
    dep_variant_info = compile_rust(
        ctx,
        attr = ctx.rule.attr,
        src = rs_out_file,
        extra_srcs = [],
        deps = deps,
    )
    return dep_variant_info.cc_info.linking_context

def _cc_bindings_from_rust_aspect_impl(target, ctx):
    basename = target.label.name

    if CrateInfo not in target:
        return []
    if str(target.label) in targets_to_remove:
        return []

    toolchain = find_toolchain(ctx)
    crate_info = target[CrateInfo]
    cc_toolchain = find_cpp_toolchain(ctx)
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
    )

    dep_info, build_info, linkstamps = collect_deps(
        deps = crate_info.deps,
        proc_macro_deps = crate_info.proc_macro_deps,
        aliases = crate_info.aliases,
    )

    compile_inputs, out_dir, build_env_files, build_flags_files, linkstamp_outs, ambiguous_libs = collect_inputs(
        ctx = ctx,
        file = ctx.file,
        files = ctx.files,
        linkstamps = linkstamps,
        toolchain = toolchain,
        cc_toolchain = cc_toolchain,
        feature_configuration = feature_configuration,
        crate_info = crate_info,
        dep_info = dep_info,
        build_info = build_info,
        stamp = False,
        experimental_use_cc_common_link = False,
    )

    # TODO(b/282958841): The `collect_inputs` call above should take the `data`
    # dependency into account.
    data_files = [target.files for target in ctx.rule.attr.data]
    compile_inputs = depset(transitive = [compile_inputs] + data_files)

    args, env = construct_arguments(
        ctx = ctx,
        attr = ctx.rule.attr,
        file = ctx.file,
        toolchain = toolchain,
        tool_path = toolchain.rustc.path,
        cc_toolchain = cc_toolchain,
        emit = [],
        feature_configuration = feature_configuration,
        crate_info = crate_info,
        dep_info = dep_info,
        linkstamp_outs = linkstamp_outs,
        ambiguous_libs = ambiguous_libs,
        # TODO(lukasza): Do we need to pass an output_hash here?
        # b/254690602 suggests that we want to include a hash in
        # the names of namespaces generated by cc_bindings_from_rs.
        output_hash = "",
        rust_flags = [],
        out_dir = out_dir,
        build_env_files = build_env_files,
        build_flags_files = build_flags_files,
        force_all_deps_direct = False,
        stamp = False,
        use_json_output = False,
    )

    (h_out_file, rs_out_file) = _generate_bindings(
        ctx,
        basename,
        compile_inputs,
        args.rustc_flags,
        env,
    )

    impl_linking_context = _compile_rs_out_file(ctx, rs_out_file, target)

    target_cc_info = target[CcInfo]
    target_crate_linking_context = target_cc_info.linking_context
    cc_info = _make_cc_info_for_h_out_file(
        ctx,
        h_out_file,
        [target_crate_linking_context, impl_linking_context],
    )
    return [
        CcBindingsFromRustInfo(
            cc_info = cc_info,
            crate_key = crate_info.name,
            h_out_file = h_out_file,
        ),
        OutputGroupInfo(out = depset([h_out_file, rs_out_file])),
    ]

cc_bindings_from_rust_aspect = aspect(
    implementation = _cc_bindings_from_rust_aspect_impl,
    doc = "Aspect for generating C++ bindings for a Rust library.",
    attr_aspects = ["deps"],
    attrs = {
        "_cc_bindings_from_rs_tool": attr.label(
            default = Label("//cc_bindings_from_rs:cc_bindings_from_rs"),
            executable = True,
            cfg = "exec",
            allow_single_file = True,
        ),
        "_cc_toolchain": attr.label(
            default = "@bazel_tools//tools/cpp:current_cc_toolchain",
        ),
        "_clang_format": attr.label(
            default = "//third_party/crosstool/google3_users:stable_clang-format",
            executable = True,
            allow_single_file = True,
            cfg = "exec",
        ),
        "_cc_deps_for_bindings": attr.label_list(
            doc = "Dependencies needed to build the C++ sources generated by cc_bindings_from_rs.",
            default = [
                "//support/internal:bindings_support",
                "//support/rs_std:rs_char",
            ],
        ),
        "_process_wrapper": attr.label(
            default = "@rules_rust//util/process_wrapper",
            executable = True,
            allow_single_file = True,
            cfg = "exec",
        ),
        "_rs_deps_for_bindings": attr.label_list(
            doc = "Dependencies needed to build the Rust sources generated by cc_bindings_from_rs.",
            default = [
                "@crate_index//:memoffset",
            ],
        ),
        "_rustfmt": attr.label(
            default = "//nowhere/llvm/rust:genrustfmt_for_crubit_aspects",
            executable = True,
            allow_single_file = True,
            cfg = "exec",
        ),
        "_rustfmt_cfg": attr.label(
            default = "//nowhere:rustfmt.toml",
            allow_single_file = True,
        ),
    },
    toolchains = [
        "@rules_rust//rust:toolchain",
    ] + use_cpp_toolchain(),
    fragments = ["cpp"],
)

def _cc_bindings_from_rust_rule_impl(ctx):
    crate = ctx.attr.crate
    return [
        crate[CcBindingsFromRustInfo].cc_info,
        # If we try to generate rust bindings of c++ bindings of this rust crate, we get back
        # the original rust crate again.
        RustBindingsFromCcInfo(
            cc_info = None,
            dep_variant_info = DepVariantInfo(
                crate_info = crate[CrateInfo] if CrateInfo in crate else None,
                dep_info = crate[DepInfo] if DepInfo in crate else None,
                build_info = crate[BuildInfo] if BuildInfo in crate else None,
                cc_info = crate[CcInfo] if CcInfo in crate else None,
            ),
            target_args = depset([]),
            namespaces = None,
        ),
    ]

cc_bindings_from_rust = rule(
    implementation = _cc_bindings_from_rust_rule_impl,
    doc = "Rule for generating C++ bindings for a Rust library.",
    attrs = {
        "crate": attr.label(
            doc = "Rust library to generate C++ bindings for",
            allow_files = False,
            mandatory = True,
            providers = [CcBindingsFromRustInfo],
            aspects = [cc_bindings_from_rust_aspect],
        ),
    },
)
