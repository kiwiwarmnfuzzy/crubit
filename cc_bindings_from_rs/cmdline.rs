// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

use anyhow::{anyhow, ensure, Result};
use clap::Parser;
use std::path::PathBuf;

#[derive(Debug, Parser)]
#[clap(name = "cc_bindings_from_rs")]
#[clap(about = "Generates C++ bindings for a Rust crate", long_about = None)]
pub struct Cmdline {
    /// Output path for C++ header file with bindings.
    #[clap(long, value_parser, value_name = "FILE")]
    pub h_out: PathBuf,

    /// Output path for Rust implementation of the bindings.
    #[clap(long, value_parser, value_name = "FILE")]
    pub rs_out: PathBuf,

    /// Path to the `crubit/support` directory in a format that should be used
    /// in the `#include` directives inside the generated C++ files.
    /// Example: "crubit/support".
    #[clap(long, value_parser, value_name = "STRING", empty_values = false)]
    // This is a `String` rather than `PathBuf`, because 1) we never open this path,
    // and 2) we want to stamp this unmodified string into the generated files
    // (not caring about path normalization, directory separator character, etc.).
    pub crubit_support_path: String,

    /// Path to a clang-format executable that will be used to format the
    /// C++ header files generated by the tool.
    #[clap(long, value_parser, value_name = "FILE")]
    pub clang_format_exe_path: PathBuf,

    /// Include paths of bindings for dependencies of the current crate
    /// (generated by previous invocations of the tool).
    /// Example: "--bindings-from-dependency=foo=some/path/foo_cc_api.h".
    #[clap(long = "bindings-from-dependency", value_parser = parse_bindings_from_dependency,
           value_name = "CRATE_NAME=INCLUDE_PATH")]
    // TODO(b/271857814): A `CRATE_NAME` might not be globally unique - the key needs to also cover
    // a "hash" of the crate version and compilation flags.
    pub bindings_from_dependencies: Vec<(String, String)>,

    /// Path to a rustfmt executable that will be used to format the
    /// Rust source files generated by the tool.
    #[clap(long, value_parser, value_name = "FILE")]
    pub rustfmt_exe_path: PathBuf,

    /// Path to a rustfmt.toml file that should replace the
    /// default formatting of the .rs files generated by the tool.
    #[clap(long, value_parser, value_name = "FILE")]
    pub rustfmt_config_path: Option<PathBuf>,

    /// Command line arguments of the Rust compiler.
    #[clap(last = true, value_parser)]
    pub rustc_args: Vec<String>,
}

impl Cmdline {
    pub fn new(args: &[String]) -> Result<Self> {
        assert_ne!(
            0,
            args.len(),
            "`args` should include the name of the executable (i.e. argsv[0])"
        );
        let exe_name = args[0].clone();

        // Ensure that `@file` expansion also covers *our* args.
        //
        // TODO(b/254688847): Decide whether to replace this with a `clap`-declared,
        // `--help`-exposed `--flagfile <path>`.
        let args = rustc_driver::args::arg_expand_all(args);

        // Parse `args` using the parser `derive`d by the `clap` crate.
        let mut cmdline = Self::try_parse_from(args)?;

        // For compatibility with `rustc_driver` expectations, we prepend `exe_name` to
        // `rustc_args.  This is needed, because `rustc_driver::RunCompiler::new`
        // expects that its `at_args` includes the name of the executable -
        // `handle_options` in `rustc_driver/src/lib.rs` throws away the first
        // element.
        cmdline.rustc_args.insert(0, exe_name);

        Ok(cmdline)
    }
}

/// Parse cmdline arguments of the following form:`"crateName=includePath"`.
///
/// Adapted from
/// https://github.com/clap-rs/clap/blob/cc1474f97c78002f3d99261699114e61d70b0634/examples/typed-derive.rs#L47-L59
fn parse_bindings_from_dependency(s: &str) -> Result<(String, String)> {
    let pos = s
        .find('=')
        .ok_or_else(|| anyhow!("Expected KEY=VALUE syntax but no `=` found in `{s}`"))?;

    let crate_name = &s[..pos];
    ensure!(!crate_name.is_empty(), "Empty crate names are invalid");

    let include = &s[(pos + 1)..];
    ensure!(!include.is_empty(), "Empty include paths are invalid");

    Ok((crate_name.to_string(), include.to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;

    use itertools::Itertools;
    use std::path::Path;
    use tempfile::tempdir;

    fn new_cmdline<'a>(args: impl IntoIterator<Item = &'a str>) -> Result<Cmdline> {
        // When `Cmdline::new` is invoked from `main.rs`, it includes not only the
        // "real" cmdline arguments, but also the name of the executable.
        let args = std::iter::once("cc_bindings_from_rs_unittest_executable")
            .chain(args)
            .map(|s| s.to_string())
            .collect_vec();
        Cmdline::new(&args)
    }

    #[test]
    fn test_happy_path() {
        let cmdline = new_cmdline([
            "--h-out=foo.h",
            "--rs-out=foo_impl.rs",
            "--crubit-support-path=crubit/support/for/tests",
            "--clang-format-exe-path=clang-format.exe",
            "--rustfmt-exe-path=rustfmt.exe",
        ])
        .unwrap();

        assert_eq!(Path::new("foo.h"), cmdline.h_out);
        assert_eq!(Path::new("foo_impl.rs"), cmdline.rs_out);
        assert_eq!("crubit/support/for/tests", &*cmdline.crubit_support_path);
        assert_eq!(Path::new("clang-format.exe"), cmdline.clang_format_exe_path);
        assert_eq!(Path::new("rustfmt.exe"), cmdline.rustfmt_exe_path);
        assert!(cmdline.bindings_from_dependencies.is_empty());
        assert!(cmdline.rustfmt_config_path.is_none());
        // Ignoring `rustc_args` in this test - they are covered in a separate
        // test below: `test_rustc_args_happy_path`.
    }

    #[test]
    fn test_rustc_args_happy_path() {
        // Note that this test would fail without the `--` separator.
        let cmdline = new_cmdline([
            "--h-out=foo.h",
            "--rs-out=foo_impl.rs",
            "--crubit-support-path=crubit/support/for/tests",
            "--clang-format-exe-path=clang-format.exe",
            "--rustfmt-exe-path=rustfmt.exe",
            "--",
            "test.rs",
            "--crate-type=lib",
        ])
        .unwrap();

        let rustc_args = &cmdline.rustc_args;
        assert!(
            itertools::equal(
                ["cc_bindings_from_rs_unittest_executable", "test.rs", "--crate-type=lib"],
                rustc_args
            ),
            "rustc_args = {:?}",
            rustc_args,
        );
    }

    /// The `test_help` unit test below has multiple purposes:
    /// - Direct/obvious purpose: testing that `--help` works
    /// - Double-checking the overall shape of our cmdline "API" (i.e.
    ///   verification that the way we use `clap` attributes results in the
    ///   desired cmdline "API"). This is a good enough coverage to avoid having
    ///   flag-specifc tests (e.g. avoiding hypothetical
    ///   `test_h_out_missing_flag`, `test_h_out_missing_arg`,
    ///   `test_h_out_duplicated`).
    /// - Exhaustively checking runtime asserts (assumming that tests run in a
    ///   debug build; other tests also trigger these asserts).  See also:
    ///     - https://github.com/clap-rs/clap/issues/2740#issuecomment-907240414
    ///     - `clap::builder::App::debug_assert`
    ///
    /// To regenerate `expected_msg` do the following steps:
    /// - Run `bazel run :cc_bindings_from_rs -- --help`
    /// - Copy&paste the output of the command below
    /// - Replace the 2nd `cc_bindings_from_rs` with
    ///   `cc_bindings_from_rs_unittest_executable`
    #[test]
    fn test_help() {
        let anyhow_err = new_cmdline(["--help"]).expect_err("--help should trigger an error");
        let clap_err = anyhow_err.downcast::<clap::Error>().unwrap();
        let expected_msg = r#"cc_bindings_from_rs 
Generates C++ bindings for a Rust crate

USAGE:
    cc_bindings_from_rs_unittest_executable [OPTIONS] --h-out <FILE> --rs-out <FILE> --crubit-support-path <STRING> --clang-format-exe-path <FILE> --rustfmt-exe-path <FILE> [-- <RUSTC_ARGS>...]

ARGS:
    <RUSTC_ARGS>...    Command line arguments of the Rust compiler

OPTIONS:
        --bindings-from-dependency <CRATE_NAME=INCLUDE_PATH>
            Include paths of bindings for dependencies of the current crate (generated by
            previous invocations of the tool). Example: "--bindings-from-dependency=foo=some/path/
            foo_cc_api.h"

        --clang-format-exe-path <FILE>
            Path to a clang-format executable that will be used to format the C++ header files
            generated by the tool

        --crubit-support-path <STRING>
            Path to the `crubit/support` directory in a format that should be used in the `#include`
            directives inside the generated C++ files. Example: "crubit/support"

        --h-out <FILE>
            Output path for C++ header file with bindings

    -h, --help
            Print help information

        --rs-out <FILE>
            Output path for Rust implementation of the bindings

        --rustfmt-config-path <FILE>
            Path to a rustfmt.toml file that should replace the default formatting of the .rs files
            generated by the tool

        --rustfmt-exe-path <FILE>
            Path to a rustfmt executable that will be used to format the Rust source files generated
            by the tool
"#;
        let actual_msg = clap_err.to_string();
        assert_eq!(
            expected_msg, actual_msg,
            "Unexpected --help output\n\
                                              EXPECTED OUTPUT:\n\
                                              {expected_msg}\n\
                                              ACTUAL OUTPUT:\n\
                                              {actual_msg}"
        );
    }

    #[test]
    fn test_here_file() -> anyhow::Result<()> {
        let tmpdir = tempdir()?;
        let tmpfile = tmpdir.path().join("herefile");
        let file_lines = vec![
            "--h-out=foo.h",
            "--rs-out=foo_impl.rs",
            "--crubit-support-path=crubit/support/for/tests",
            "--clang-format-exe-path=clang-format.exe",
            "--rustfmt-exe-path=rustfmt.exe",
            "--",
            "test.rs",
            "--crate-type=lib",
        ];
        std::fs::write(&tmpfile, file_lines.as_slice().join("\n"))?;

        let flag_file_arg = format!("@{}", tmpfile.display());
        let cmdline = new_cmdline([flag_file_arg.as_str()]).unwrap();
        assert_eq!(Path::new("foo.h"), cmdline.h_out);
        assert_eq!(Path::new("foo_impl.rs"), cmdline.rs_out);
        let rustc_args = &cmdline.rustc_args;
        assert!(
            itertools::equal(
                ["cc_bindings_from_rs_unittest_executable", "test.rs", "--crate-type=lib"],
                rustc_args),
            "rustc_args = {:?}",
            rustc_args,
        );
        Ok(())
    }

    #[test]
    fn test_bindings_from_dependencies_as_multiple_separate_cmdline_args() {
        let cmdline = new_cmdline([
            "--h-out=foo.h",
            "--rs-out=foo_impl.rs",
            "--crubit-support-path=crubit/support/for/tests",
            "--clang-format-exe-path=clang-format.exe",
            "--rustfmt-exe-path=rustfmt.exe",
            "--bindings-from-dependency=dep1=path1",
            "--bindings-from-dependency=dep2=path2",
        ])
        .unwrap();

        assert_eq!(2, cmdline.bindings_from_dependencies.len());
        assert_eq!("dep1", cmdline.bindings_from_dependencies[0].0);
        assert_eq!("path1", cmdline.bindings_from_dependencies[0].1);
        assert_eq!("dep2", cmdline.bindings_from_dependencies[1].0);
        assert_eq!("path2", cmdline.bindings_from_dependencies[1].1);
    }

    #[test]
    fn test_parse_bindings_from_dependency() {
        assert_eq!(
            parse_bindings_from_dependency("foo=bar").unwrap(),
            ("foo".into(), "bar".into()),
        );
        assert_eq!(
            parse_bindings_from_dependency("").unwrap_err().to_string(),
            "Expected KEY=VALUE syntax but no `=` found in ``",
        );
        assert_eq!(
            parse_bindings_from_dependency("no-equal-char").unwrap_err().to_string(),
            "Expected KEY=VALUE syntax but no `=` found in `no-equal-char`",
        );
        assert_eq!(
            parse_bindings_from_dependency("=bar").unwrap_err().to_string(),
            "Empty crate names are invalid",
        );
        assert_eq!(
            parse_bindings_from_dependency("foo=").unwrap_err().to_string(),
            "Empty include paths are invalid",
        );
    }
}
