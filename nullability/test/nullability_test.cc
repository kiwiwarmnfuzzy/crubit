// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// This is the test driver for a nullability_test.
//
// A test is a C++ source file that contains code to be nullability-analyzed.
// The code can include calls to special assertion functions like nullable().
// These assert details of the analysis results (nullability of expressions).
// (These functions are declared in nullability_test.h, see details there).
//
// This tool's job is to parse the file, run the nullability analysis,
// check whether the assertions pass, and report the results.
//
// It can be invoked manually, and writes textual logs to stdout, but can also
// write Bazel structured test results.
// https://bazel.build/reference/test-encyclopedia
//
// The dataflow visualizer is useful in debugging test failures:
//   When running under Bazel, pass --test_arg=-log
//   When running manually, pass -dataflow-log=/some/scratch/dir

#include <assert.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "nullability/pointer_nullability.h"
#include "nullability/pointer_nullability_analysis.h"
#include "nullability/pointer_nullability_lattice.h"
#include "nullability/type_nullability.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/CanonicalType.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/FlowSensitive/ControlFlowContext.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysisContext.h"
#include "clang/Analysis/FlowSensitive/WatchedLiteralsSolver.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/TextDiagnostic.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/StandaloneExecution.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

namespace clang::tidy::nullability {
namespace {
llvm::cl::list<std::string> Sources(llvm::cl::Positional, llvm::cl::OneOrMore);
llvm::cl::opt<bool> EmitTestLog("log");

// Deal with unexpected llvm::Errors by exiting with failure status.
void require(llvm::Error E) {
  if (E) {
    llvm::errs() << toString(std::move(E)) << "\n";
    std::exit(1);
  }
}
template <typename T>
T require(llvm::Expected<T> E) {
  require(E.takeError());
  return std::move(*E);
}

// Emit diagnostics for nullability assertion failures.
class Diagnoser {
 public:
  Diagnoser(DiagnosticsEngine &Diags)
      : Diags(Diags),
        WrongNullability(Diags.getCustomDiagID(
            DiagnosticsEngine::Error, "expression is %1, expected %0")),
        WrongTypeCanonical(Diags.getCustomDiagID(
            DiagnosticsEngine::Error,
            "argument with type %1 could never match assertion %0")),
        WrongTypeNullability(Diags.getCustomDiagID(
            DiagnosticsEngine::Error, "static nullability is %1, expected %0")),
        WrongNodeKind(Diags.getCustomDiagID(
            DiagnosticsEngine::Error, "TEST on %0 node is not supported")) {}

  void diagnoseNullability(SourceLocation Call, SourceRange Arg,
                           NullabilityKind Want, NullabilityKind Got) {
    if (Want != Got) {
      Diags.Report(Call, WrongNullability)
          << Arg << getNullabilitySpelling(Want) << getNullabilitySpelling(Got);
    }
  }

  void diagnoseType(SourceLocation Call, SourceRange Arg, CanQualType WantCanon,
                    CanQualType GotCanon,
                    llvm::ArrayRef<NullabilityKind> WantNulls,
                    llvm::ArrayRef<NullabilityKind> GotNulls) {
    if (WantCanon != GotCanon) {
      Diags.Report(Call, WrongTypeCanonical) << WantCanon << GotCanon;
    } else if (WantNulls != GotNulls) {
      Diags.Report(Call, WrongTypeNullability)
          << nullabilityToString(WantNulls) << nullabilityToString(GotNulls);
    }
  }

  void diagnoseBadTest(const DynTypedNode &N) {
    Diags.Report(N.getSourceRange().getBegin(), WrongNodeKind)
        << N.getNodeKind().asStringRef() << N.getSourceRange();
  }

 private:
  DiagnosticsEngine &Diags;
  unsigned WrongNullability;
  unsigned WrongTypeCanonical;
  unsigned WrongTypeNullability;
  unsigned WrongNodeKind;
};

// Match a nullable()/nonnull()/unknown() call, return the nullability asserted.
std::optional<NullabilityKind> getAssertedNullability(const CallExpr &Call) {
  auto *FD = Call.getDirectCallee();
  if (!FD || !FD->getDeclContext()->isTranslationUnit() ||
      !FD->getDeclName().isIdentifier())
    return std::nullopt;
  return llvm::StringSwitch<std::optional<NullabilityKind>>(FD->getName())
      .Case("nullable", NullabilityKind::Nullable)
      .Case("nonnull", NullabilityKind::NonNull)
      .Case("unknown", NullabilityKind::Unspecified)
      .Default(std::nullopt);
}

// Match a type<...>() call, return the type asserted.
std::optional<QualType> getAssertedType(const CallExpr &Call) {
  // must be a call to ::type
  auto *FD = Call.getDirectCallee();
  if (!FD || !FD->getDeclContext()->isTranslationUnit() ||
      !FD->getDeclName().isIdentifier() || FD->getName() != "type")
    return std::nullopt;

  // must have template arguments, first is an explicitly-written type
  auto *DRE = dyn_cast<DeclRefExpr>(Call.getCallee()->IgnoreImplicit());
  if (!DRE || !DRE->hasExplicitTemplateArgs() ||
      DRE->getTemplateArgs()[0].getArgument().getKind() !=
          TemplateArgument::Type)
    return std::nullopt;

  return DRE->getTemplateArgs()[0].getTypeSourceInfo()->getType();
}

using AnalysisState =
    const dataflow::DataflowAnalysisState<PointerNullabilityLattice>;
// Match any special assertions, check the condition, diagnose on failure.
void diagnoseCall(const CallExpr &CE, const ASTContext &Ctx, Diagnoser &Diags,
                  const AnalysisState &State) {
  if (auto Want = getAssertedNullability(CE); Want && CE.getNumArgs() == 1) {
    auto &Arg = *CE.getArgs()[0];
    auto Got = getNullability(&Arg, State.Env);
    Diags.diagnoseNullability(CE.getBeginLoc(), Arg.getSourceRange(), *Want,
                              Got);
  }
  if (auto Want = getAssertedType(CE); Want && CE.getNumArgs() == 1) {
    auto &Got = *CE.getArgs()[0];
    auto WantCanon = Ctx.getCanonicalType(*Want);
    auto GotCanon = Ctx.getCanonicalType(Got.getType());
    auto WantNulls = getNullabilityAnnotationsFromType(*Want);
    TypeNullability GotNulls = unspecifiedNullability(&Got);
    if (const auto *GN = State.Lattice.getExprNullability(&Got)) GotNulls = *GN;
    Diags.diagnoseType(CE.getBeginLoc(), Got.getSourceRange(), WantCanon,
                       GotCanon, WantNulls, GotNulls);
  }
}

// To run a test, we simply run the nullability analysis, and then walk the
// CFG afterwards looking for calls to our assertions - nullable() etc.
// These each assert properties attached to the analysis state at that point.
void runTest(const FunctionDecl &Func, Diagnoser &Diags,
             std::unique_ptr<llvm::raw_ostream> LogStream) {
  std::unique_ptr<dataflow::Logger> Logger;
  if (LogStream)
    Logger = dataflow::Logger::html([&] {
      CHECK(LogStream) << "analyzing multiple functions?!";
      return std::move(LogStream);
    });
  dataflow::DataflowAnalysisContext::Options Opts;
  Opts.Log = Logger.get();
  dataflow::DataflowAnalysisContext DACtx(
      std::make_unique<dataflow::WatchedLiteralsSolver>(), Opts);
  auto &Ctx = Func.getDeclContext()->getParentASTContext();
  auto CFCtx = require(dataflow::ControlFlowContext::build(Func));
  PointerNullabilityAnalysis Analysis(Ctx);
  require(
      runDataflowAnalysis(CFCtx, Analysis, dataflow::Environment(DACtx, Func),
                          [&](const CFGElement &Elt, AnalysisState &State) {
                            if (auto CS = Elt.getAs<CFGStmt>())
                              if (auto *CE = dyn_cast<CallExpr>(CS->getStmt()))
                                diagnoseCall(*CE, Ctx, Diags, State);
                          }));
}

// Absorbs test start/end events and diagnostics.
// Produces stdout output, and also Bazel test.xml report.
class TestOutput : public DiagnosticConsumer {
 public:
  TestOutput()
      : Out(llvm::outs()),
        XMLStorage(openXML()),
        XML(XMLStorage ? *XMLStorage : llvm::nulls()) {
    XML << "<testsuites>\n";
  }
  ~TestOutput() override { XML << "</testsuites>\n"; }

  void startSuite(llvm::StringRef Name) {
    XML << llvm::formatv("<testsuite name='{0}'>\n", escape(Name));
    Out << "=== Suite: " << Name << " ===\n";
  }
  void endSuite() { XML << "</testsuite>\n"; }

  void startTest(const FunctionDecl &F) {
    CurrentCase.emplace();
    CurrentCase->Name = F.getName();
    CurrentCase->Start = std::chrono::steady_clock::now();
    Out << "--- Test: " << CurrentCase->Name << " ---\n";
  }
  void endTest(llvm::StringRef LogPath) {
    assert(CurrentCase.has_value());
    Out << (CurrentCase->Failures.empty() ? "PASS" : "FAIL") << "\n";
    auto Millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - CurrentCase->Start)
                      .count();
    XML << llvm::formatv("<testcase name='{0}' time='{1}'>\n",
                         escape(CurrentCase->Name), Millis);
    for (const auto &Failure : CurrentCase->Failures)
      XML << llvm::formatv("<failure message='{0}'>{1}</failure>\n",
                           escape(Failure.first), escape(Failure.second));
    if (!LogPath.empty()) {
      XML << llvm::formatv(
          "<properties><property name='test_output1' value='{0}'>"
          "</property></properties>",
          escape(LogPath));
      Out << "Log written to " << LogPath << "\n";
    } else if (!CurrentCase->Failures.empty()) {
      XML << "<error message='Note: run with --test_arg=-log for detailed "
             "analysis logs'></error>\n";
    }
    XML << "</testcase>\n";
    CurrentCase.reset();
  }
  bool hadErrors() const { return HadErrors; }

  void BeginSourceFile(const LangOptions &LangOpts,
                       const Preprocessor *PP) override {
    this->LangOpts = LangOpts;
  }
  void HandleDiagnostic(DiagnosticsEngine::Level Level,
                        const Diagnostic &Info) override {
    llvm::SmallString<256> Message;
    Info.FormatDiagnostic(Message);

    // TODO: in the printed diagnostic, the absolute path to the file is shown.
    // This is hard to read and breaks linkification in log viewers.
    // This happens because the tooling makes input file paths absolute.
    // We should find a way to avoid this.
    std::string Rendered;
    llvm::raw_string_ostream OS(Rendered);
    TextDiagnostic(OS, LangOpts, new DiagnosticOptions())
        .emitDiagnostic(
            FullSourceLoc(Info.getLocation(), Info.getSourceManager()), Level,
            Message, Info.getRanges(), Info.getFixItHints());

    Out << Rendered;
    if (Level >= DiagnosticsEngine::Error) {
      if (CurrentCase) CurrentCase->Failures.emplace_back(Message, Rendered);
      HadErrors = true;
    }
  }

 private:
  // Create test.xml file in the right place, if running under Bazel.
  static std::unique_ptr<llvm::raw_ostream> openXML() {
    if (const char *Filename = std::getenv("XML_OUTPUT_FILE")) {
      std::error_code EC;
      auto OS = std::make_unique<llvm::raw_fd_ostream>(Filename, EC);
      if (EC) {
        llvm::errs() << "Failed to open XML output " << Filename << ": "
                     << EC.message() << "\n";
      } else {
        return OS;
      }
    }
    return nullptr;
  }

  static std::string escape(llvm::StringRef Raw) {
    std::string S;
    llvm::raw_string_ostream OS(S);
    llvm::printHTMLEscaped(Raw, OS);
    return S;
  }

  bool HadErrors = false;
  LangOptions LangOpts;

  llvm::raw_ostream &Out;  // Plain-text stdout stream.
  std::unique_ptr<llvm::raw_ostream> XMLStorage;
  llvm::raw_ostream &XML;  // test.xml output stream (or null stream if no XML).

  // Gather info about the currently running test case.
  // The <testcase> element can only be written once it's finished.
  struct TestCase {
    llvm::StringRef Name;
    std::vector<std::pair<std::string, std::string>> Failures;
    std::chrono::steady_clock::time_point Start;
  };
  std::optional<TestCase> CurrentCase;
};

// AST consumer that analyzes [[clang::annotate("test")]] functions as tests.
class Consumer : public ASTConsumer {
 public:
  Consumer(TestOutput &Output) : Output(Output) {}

 private:
  void Initialize(ASTContext &Context) override {
    Diagnoser.emplace(Context.getDiagnostics());
  }

  void HandleTranslationUnit(ASTContext &Ctx) override {
    assert(Diagnoser.has_value());
    // Walk the AST, calling runTestAt on every TEST annotation.
    struct TestVisitor : public RecursiveASTVisitor<TestVisitor> {
      Consumer &Outer;
      ASTContext &Ctx;

      bool VisitAnnotateAttr(AnnotateAttr *A) {
        if (A->getAnnotation() == "test")
          Outer.runTestAt(DynTypedNode::create(*A), Ctx);
        return true;
      }
    };
    TestVisitor{{}, *this, Ctx}.TraverseAST(Ctx);
  }
  // Starting at a TEST annotation, find the associated test and run it.
  void runTestAt(const DynTypedNode &Test, ASTContext &Ctx) {
    if (const auto *FD = Test.get<FunctionDecl>()) {
      // This is a test we can run directly.
      Output.startTest(*FD);
      auto [LogPath, LogStream] = openTestLog(FD->getName());
      runTest(*FD, *Diagnoser, std::move(LogStream));
      Output.endTest(LogPath);
    } else if (Test.get<Attr>() || Test.get<TypeLoc>()) {
      // Walk up to find out what decl etc this marker is attached to.
      auto Parents = Ctx.getParents(Test);
      CHECK(!Parents.empty());
      for (const auto &Parent : Parents) runTestAt(Parent, Ctx);
    } else {
      // Uh-oh, TEST marker was in the wrong place!
      Diagnoser->diagnoseBadTest(Test);
    }
  }
  // Decide whether to write a per-test detailed log file that Bazel can find.
  // We do this if the "-log" flag is set (--test_arg=-log).
  // If we are writing one, create it and return its path and an open stream.
  std::pair<std::string, std::unique_ptr<llvm::raw_ostream>> openTestLog(
      llvm::StringRef Name) {
    const char *RootDir = ::getenv("TEST_UNDECLARED_OUTPUTS_DIR");
    if (!EmitTestLog || !RootDir) return {"", nullptr};
    llvm::SmallString<256> PathModel(RootDir), Path;
    llvm::sys::path::append(PathModel, Name + "-%%%%%%%%.html");
    int FD;
    if (auto EC = llvm::sys::fs::createUniqueFile(PathModel, FD, Path))
      return {"", nullptr};
    llvm::StringRef RelativePath = Path;
    RelativePath.consume_front(RootDir);
    while (!RelativePath.empty() &&
           llvm::sys::path::is_separator(RelativePath.front()))
      RelativePath = RelativePath.drop_front();
    return {RelativePath.str(),
            std::make_unique<llvm::raw_fd_ostream>(FD, /*ShouldClose=*/true)};
  }

  TestOutput &Output;
  std::optional<Diagnoser> Diagnoser;
};

}  // namespace
}  // namespace clang::tidy::nullability

int main(int argc, const char **argv) {
  using namespace clang::tidy::nullability;
  struct Factory : public clang::tooling::SourceFileCallbacks {
    TestOutput Output;

    std::unique_ptr<clang::ASTConsumer> newASTConsumer() {
      return std::make_unique<Consumer>(Output);
    }
    bool handleBeginSource(clang::CompilerInstance &CI) override {
      const auto &SM = CI.getSourceManager();
      Output.startSuite(llvm::sys::path::stem(llvm::sys::path::filename(
          SM.getBufferName(SM.getLocForStartOfFile(SM.getMainFileID())))));
      CI.getDiagnostics().setClient(&Output, /*Owns=*/false);
      return true;
    }
    void handleEndSource() override { Output.endSuite(); }
  } F;
  std::string Err;
  auto CDB = clang::tooling::FixedCompilationDatabase::loadFromCommandLine(
      argc, argv, Err);
  llvm::cl::ParseCommandLineOptions(argc, argv);
  if (!CDB) {
    llvm::errs() << "Usage: nullability_test source.cc -- -Ipath/to/headers\n";
    exit(1);
  }
  clang::tooling::StandaloneToolExecutor Executor{*CDB, Sources};
  require(Executor.execute(clang::tooling::newFrontendActionFactory(&F, &F)));
  return F.Output.hadErrors() ? 1 : 0;
}
