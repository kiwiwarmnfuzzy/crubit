// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "nullability/type_nullability.h"

#include <optional>

#include "absl/log/check.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTFwd.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeVisitor.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/Support/SaveAndRestore.h"

namespace clang::tidy::nullability {

std::string nullabilityToString(const TypeNullability &Nullability) {
  std::string Result = "[";
  llvm::interleave(
      Nullability,
      [&](const NullabilityKind n) {
        Result += getNullabilitySpelling(n).str();
      },
      [&] { Result += ", "; });
  Result += "]";
  return Result;
}

// Recognize aliases e.g. Nonnull<T> as equivalent to T _Nonnull, etc.
// These aliases should be annotated with [[clang::annotate("Nullable")]] etc.
//
// TODO: Ideally such aliases could apply the _Nonnull attribute themselves.
// This requires resolving compatibilty issues with clang, such as use with
// user-defined pointer-like types.
std::optional<NullabilityKind> getAliasNullability(const TemplateName &TN) {
  if (const auto *TD = TN.getAsTemplateDecl()) {
    if (!TD->getTemplatedDecl()) return std::nullopt;  // BuiltinTemplateDecl
    if (const auto *A = TD->getTemplatedDecl()->getAttr<AnnotateAttr>()) {
      if (A->getAnnotation() == "Nullable") return NullabilityKind::Nullable;
      if (A->getAnnotation() == "Nonnull") return NullabilityKind::NonNull;
      if (A->getAnnotation() == "Nullability_Unspecified")
        return NullabilityKind::Unspecified;
    }
  }
  return std::nullopt;
}

namespace {
// Traverses a Type to find the points where it might be nullable.
// This will visit the contained PointerType in the correct order to produce
// the TypeNullability vector.
//
// Subclasses must provide `void report(const PointerType*, NullabilityKind)`,
// and may override TypeVisitor Visit*Type methods to customize the traversal.
//
// Canonically-equivalent Types produce equivalent sequences of report() calls:
//  - corresponding PointerTypes are canonically-equivalent
//  - the NullabilityKind may be different, as it derives from type sugar
template <class Impl>
class NullabilityWalker : public TypeVisitor<Impl> {
  using Base = TypeVisitor<Impl>;
  Impl &derived() { return *static_cast<Impl *>(this); }

  // A nullability attribute we've seen, waiting to attach to a pointer type.
  // There may be sugar in between: Attributed -> Typedef -> Typedef -> Pointer.
  // All non-sugar types must consume nullability, most will ignore it.
  std::optional<NullabilityKind> PendingNullability;

  void sawNullability(NullabilityKind NK) {
    // If we see nullability applied twice, the outer one wins.
    if (!PendingNullability.has_value()) PendingNullability = NK;
  }

  void ignoreUnexpectedNullability() {
    // TODO: Can we upgrade this to an assert?
    // clang is pretty thorough about ensuring we can't put _Nullable on
    // non-pointers, even failing template instantiation on this basis.
    PendingNullability.reset();
  }

  // While walking types instantiated from templates, e.g.:
  //  - the underlying type of alias TemplateSpecializationTypes
  //  - type aliases inside class template instantiations
  // we see SubstTemplateTypeParmTypes where type parameters were referenced.
  // The directly-available underlying types lack sugar, but we can retrieve the
  // sugar from the arguments of the original e.g. TemplateSpecializationType.
  //
  // The "template context" associates template params with the
  // corresponding args, to allow this retrieval.
  // In general, not just the directly enclosing template params but also those
  // of outer classes are accessible.
  // So conceptually this maps (depth, index, pack_index) => TemplateArgument.
  // To avoid copying these maps, inner contexts *extend* from outer ones.
  //
  // When we start to walk a TemplateArgument (in place of a SubstTTPType), we
  // must do so in the template instantiation context where the argument was
  // written. Then when we're done, we must restore the old context.
  struct TemplateContext {
    // A decl that owns an arg list, per SubstTTPType::getAssociatedDecl.
    // For aliases: TypeAliasTemplateDecl.
    // For classes: ClassTemplateSpecializationDecl.
    const Decl *AssociatedDecl = nullptr;
    // The sugared template arguments to AssociatedDecl, as written in the code.
    // If absent, the arguments could not be reconstructed.
    std::optional<ArrayRef<TemplateArgument>> Args;
    // In general, multiple template params are in scope (nested templates).
    // These are a linked list: *this describes one, *Extends describes the
    // next. In practice, this is the enclosing class template.
    const TemplateContext *Extends = nullptr;
    // The template context in which the args were written.
    // The args may reference params visible in this context.
    const TemplateContext *ArgContext = nullptr;

    // Example showing a TemplateContext graph:
    //
    //   // (some sugar and nested templates for the example)
    //   using INT = int; using FLOAT = float;
    //   template <class T> struct Outer {
    //     template <class U> struct Inner {
    //       using Pair = std::pair<T, U>;
    //     }
    //   }
    //
    //   template <class X>
    //   struct S {
    //     using Type = typename Outer<INT>::Inner<X>::Pair;
    //   }
    //
    //   using Target = S<FLOAT>::Type;
    //
    // Per clang's AST, instantiated Type is std::pair<int, float> with only
    // SubstTemplateTypeParmTypes for sugar, we're trying to recover INT, FLOAT.
    //
    // When walking the ElaboratedType for the S<FLOAT>:: qualifier we set up:
    //
    // Current -> {Associated=S<float>, Args=<FLOAT>, Extends=null, ArgCtx=null}
    //
    // This means that when resolving ::Type:
    //   - we can resugar occurrences of X (float -> FLOAT)
    //   - ArgContext=null: the arg FLOAT may not refer to template params
    //                      (or at least we can't resugar them)
    //   - Extends=null: there are no other template params we can resugar
    //
    // Skipping up to ::Pair inside S<FLOAT>'s instantiation, we have the graph:
    //
    // Current -> {Associated=Outer<int>::Inner<float>, Args=<X>}
    //            | Extends                                  |
    // A{Associated=Outer<int>, Args=<INT>, Extends=null}    | ArgContext
    //                          | ArgContext                 |
    //       B{Associated=S<float>, Args=<FLOAT>, Extends=null, ArgContext=null}
    //
    // (Note that B here is the original TemplateContext we set up above).
    //
    // This means that when resolving ::Pair:
    //   - we can resugar instances of U (float -> X)
    //   - ArgContext=B: when resugaring U, we can resugar X (float -> FLOAT)
    //   - Extends=A: we can also resugar T (int -> INT)
    //   - A.ArgContext=B: when resugaring T, we can resugar X.
    //                     (we never do, because INT doesn't mention X)
    //   - A.Extends=null: there are no other template params te resugar
    //   - B.ArgContext=null: FLOAT may not refer to any template params
    //   - B.Extends=null: there are no other template params to resugar
    //                     (e.g. Type's definition cannot refer to T)
  };
  // The context that provides sugared args for the template params that are
  // accessible to the type we're currently walking.
  const TemplateContext *CurrentTemplateContext = nullptr;

  // Adjusts args list from those of primary template => template pattern.
  //
  // A template arg list corresponds 1:1 to primary template params.
  // In partial specializations, the correspondence may differ:
  //   template <int, class> struct S;
  //   template <class T> struct S<0, T> {
  //       using Alias = T;  // T refers to param #0
  //   };
  //   S<0, int*>::Alias X;  // T is bound to arg #1
  // or
  //   template <class> struct S;
  //   template <class T> struct S<T*> { using Alias = T; }
  //   S<int*>::Alias X;  // arg #0 is int*, param #0 is bound to int
  void translateTemplateArgsForSpecialization(TemplateContext &Ctx) {
    // Only relevant where partial specialization is used.
    // - Full specializations may not refer to template params at all.
    // - For primary templates, the input is already correct.
    const TemplateArgumentList *PartialArgs = nullptr;
    if (const ClassTemplateSpecializationDecl *CTSD =
            llvm::dyn_cast<ClassTemplateSpecializationDecl>(
                Ctx.AssociatedDecl)) {
      if (llvm::isa_and_nonnull<ClassTemplatePartialSpecializationDecl>(
              CTSD->getTemplateInstantiationPattern()))
        PartialArgs = &CTSD->getTemplateInstantiationArgs();
    } else if (const VarTemplateSpecializationDecl *VTSD =
                   llvm::dyn_cast<VarTemplateSpecializationDecl>(
                       Ctx.AssociatedDecl)) {
      if (llvm::isa_and_nonnull<VarTemplatePartialSpecializationDecl>(
              VTSD->getTemplateInstantiationPattern()))
        PartialArgs = &VTSD->getTemplateInstantiationArgs();
    }
    if (!PartialArgs) return;

    // To get from the template arg list to the partial-specialization arg list
    // means running much of the template argument deduction algorithm.
    // This is complex in general. [temp.deduct] For now, bail out.
    // In future, hopefully we can handle at least simple cases.
    Ctx.Args.reset();
  }

 public:
  void Visit(QualType T) { Base::Visit(T.getTypePtr()); }
  void Visit(const TemplateArgument &TA) {
    if (TA.getKind() == TemplateArgument::Type) Visit(TA.getAsType());
    if (TA.getKind() == TemplateArgument::Pack)
      for (const auto &PackElt : TA.getPackAsArray()) Visit(PackElt);
  }
  void Visit(const DeclContext *DC) {
    // For now, only consider enclosing classes.
    // TODO: The nullability of template functions can affect local classes too,
    // this can be relevant e.g. when instantiating templates with such types.
    if (auto *CRD = llvm::dyn_cast<CXXRecordDecl>(DC))
      Visit(DC->getParentASTContext().getRecordType(CRD));
  }

  void VisitType(const Type *T) {
    // For sugar not explicitly handled below, desugar and continue.
    // (We need to walk the full structure of the canonical type.)
    if (auto *Desugar =
            T->getLocallyUnqualifiedSingleStepDesugaredType().getTypePtr();
        Desugar != T)
      return Base::Visit(Desugar);

    // We don't expect to see any nullable non-sugar types except PointerType.
    ignoreUnexpectedNullability();
    Base::VisitType(T);
  }

  void VisitFunctionProtoType(const FunctionProtoType *FPT) {
    ignoreUnexpectedNullability();
    Visit(FPT->getReturnType());
    for (auto ParamType : FPT->getParamTypes()) Visit(ParamType);
  }

  void VisitTemplateSpecializationType(const TemplateSpecializationType *TST) {
    if (TST->isTypeAlias()) {
      if (auto NK = getAliasNullability(TST->getTemplateName()))
        sawNullability(*NK);

      // Aliases are sugar, visit the underlying type.
      // Record template args so we can resugar substituted params.
      //
      // TODO(b/281474380): `TemplateSpecializationType::template_arguments()`
      // doesn't contain defaulted arguments. Can we fetch or compute these in
      // sugared form?
      const TemplateContext Ctx{
          /*AssociatedDecl=*/TST->getTemplateName().getAsTemplateDecl(),
          /*Args=*/TST->template_arguments(),
          /*Extends=*/CurrentTemplateContext,
          /*ArgContext=*/CurrentTemplateContext,
      };
      llvm::SaveAndRestore UseAlias(CurrentTemplateContext, &Ctx);
      VisitType(TST);
      return;
    }

    auto *CRD = TST->getAsCXXRecordDecl();
    CHECK(CRD) << "Expected an alias or class specialization in concrete code";
    ignoreUnexpectedNullability();
    Visit(CRD->getDeclContext());
    for (auto TA : TST->template_arguments()) Visit(TA);
    // `TST->template_arguments()` doesn't contain any default arguments.
    // Retrieve these (though in unsugared form) from the
    // `ClassTemplateSpecializationDecl`.
    // TODO(b/281474380): Can we fetch or compute default arguments in sugared
    // form?
    if (auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(CRD)) {
      for (unsigned i = TST->template_arguments().size();
           i < CTSD->getTemplateArgs().size(); ++i)
        Visit(CTSD->getTemplateArgs()[i]);
    }
  }

  void VisitSubstTemplateTypeParmType(const SubstTemplateTypeParmType *T) {
    // The underlying type of T in the AST has no sugar, as the template has
    // only one body instantiated per canonical args.
    // Instead, try to find the (sugared) template argument that T is bound to.
    for (const auto *Ctx = CurrentTemplateContext; Ctx; Ctx = Ctx->Extends) {
      if (T->getAssociatedDecl() != Ctx->AssociatedDecl) continue;
      // If args are not available, fall back to un-sugared arg.
      if (!Ctx->Args.has_value()) break;
      unsigned Index = T->getIndex();
      // Valid because pack must be the last param in non-function templates.
      // TODO: if we support function templates, we need to be smarter here.
      if (auto PackIndex = T->getPackIndex())
        Index = Ctx->Args->size() - 1 - *PackIndex;

      // TODO(b/281474380): `Args` may be too short if `Index` refers to an
      // arg that was defaulted.  We eventually want to populate
      // `CurrentAliasTemplate->Args` with the default arguments in this case,
      // but for now, we just walk the underlying type without sugar.
      if (Index < Ctx->Args->size()) {
        const TemplateArgument &Arg = (*Ctx->Args)[Index];
        // When we start to walk a sugared TemplateArgument (in place of T),
        // we must do so in the template instantiation context where the
        // argument was written.
        llvm::SaveAndRestore OriginalContext(
            CurrentTemplateContext, CurrentTemplateContext->ArgContext);
        return Visit(Arg);
      }
    }
    // Our top-level type references an unbound type param.
    // Our original input was the underlying type of an  instantiation, we
    // lack the context needed to resugar it.
    // TODO: maybe this could be an assert in some cases (alias params)?
    // We would need to trust all callers are obtaining types appropriately,
    // and that clang never partially-desugars in a problematic way.
    VisitType(T);
  }

  // If we see foo<args>::ty then we may need sugar from args to resugar ty.
  void VisitElaboratedType(const ElaboratedType *ET) {
    std::vector<TemplateContext> BoundTemplateArgs;
    // Iterate over qualifiers right-to-left, looking for template args.
    for (auto *NNS = ET->getQualifier(); NNS; NNS = NNS->getPrefix()) {
      // TODO: there are other ways a NNS could bind template args:
      //   template <typename T> foo { struct bar { using baz = T; }; };
      //   using T = foo<int * _Nullable>::bar;
      //   using U = T::baz;
      // Here T:: is not a TemplateSpecializationType (directly or indirectly).
      // Nevertheless it provides sugar that is referenced from baz.
      // Probably we need another type visitor to collect bindings in general.
      if (const auto *TST = llvm::dyn_cast_or_null<TemplateSpecializationType>(
              NNS->getAsType())) {
        TemplateContext Ctx;
        Ctx.Args = TST->template_arguments();
        Ctx.ArgContext = CurrentTemplateContext;
        // `Extends` is initialized below: we chain BoundTemplateArgs together.
        Ctx.AssociatedDecl =
            TST->isTypeAlias() ? TST->getTemplateName().getAsTemplateDecl()
                               : static_cast<Decl *>(TST->getAsCXXRecordDecl());
        translateTemplateArgsForSpecialization(Ctx);
        BoundTemplateArgs.push_back(Ctx);
      }
    }
    std::optional<llvm::SaveAndRestore<const TemplateContext *>> Restore;
    if (!BoundTemplateArgs.empty()) {
      // Wire up the inheritance chain so all the contexts are visible.
      BoundTemplateArgs.back().Extends = CurrentTemplateContext;
      for (int I = 0; I < BoundTemplateArgs.size() - 1; ++I)
        BoundTemplateArgs[I].Extends = &BoundTemplateArgs[I + 1];
      Restore.emplace(CurrentTemplateContext, &BoundTemplateArgs.front());
    }
    Visit(ET->getNamedType());
  }

  void VisitRecordType(const RecordType *RT) {
    ignoreUnexpectedNullability();
    Visit(RT->getDecl()->getDeclContext());
    if (auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RT->getDecl())) {
      // TODO: if this is an instantiation, these args lack sugar.
      // We can try to retrieve it from the current template context.
      for (auto &TA : CTSD->getTemplateArgs().asArray()) Visit(TA);
    }
  }

  void VisitAttributedType(const AttributedType *AT) {
    if (auto NK = AT->getImmediateNullability()) sawNullability(*NK);
    Visit(AT->getModifiedType());
    CHECK(!PendingNullability.has_value())
        << "Should have been consumed by modified type! "
        << AT->getModifiedType().getAsString();
  }

  void VisitPointerType(const PointerType *PT) {
    derived().report(PT,
                     PendingNullability.value_or(NullabilityKind::Unspecified));
    PendingNullability.reset();
    Visit(PT->getPointeeType());
  }

  void VisitReferenceType(const ReferenceType *RT) {
    ignoreUnexpectedNullability();
    Visit(RT->getPointeeTypeAsWritten());
  }

  void VisitArrayType(const ArrayType *AT) {
    ignoreUnexpectedNullability();
    Visit(AT->getElementType());
  }
};

template <typename T>
unsigned countPointers(const T &Object) {
  struct Walker : public NullabilityWalker<Walker> {
    unsigned Count = 0;
    void report(const PointerType *, NullabilityKind) { ++Count; }
  } PointerCountWalker;
  PointerCountWalker.Visit(Object);
  return PointerCountWalker.Count;
}

}  // namespace

unsigned countPointersInType(QualType T) { return countPointers(T); }

unsigned countPointersInType(const DeclContext *DC) {
  return countPointers(DC);
}
unsigned countPointersInType(TemplateArgument TA) { return countPointers(TA); }

QualType exprType(const Expr *E) {
  if (E->hasPlaceholderType(BuiltinType::BoundMember))
    return Expr::findBoundMemberType(E);
  return E->getType();
}

unsigned countPointersInType(const Expr *E) {
  return countPointersInType(exprType(E));
}

TypeNullability getNullabilityAnnotationsFromType(
    QualType T,
    llvm::function_ref<GetTypeParamNullability> SubstituteTypeParam) {
  struct Walker : NullabilityWalker<Walker> {
    std::vector<NullabilityKind> Annotations;
    llvm::function_ref<GetTypeParamNullability> SubstituteTypeParam;

    void report(const PointerType *, NullabilityKind NK) {
      Annotations.push_back(NK);
    }

    void VisitSubstTemplateTypeParmType(const SubstTemplateTypeParmType *ST) {
      if (SubstituteTypeParam) {
        if (auto Subst = SubstituteTypeParam(ST)) {
          DCHECK_EQ(Subst->size(),
                    countPointersInType(ST->getCanonicalTypeInternal()))
              << "Substituted nullability has the wrong structure: "
              << QualType(ST, 0).getAsString();
          llvm::append_range(Annotations, *Subst);
          return;
        }
      }
      NullabilityWalker::VisitSubstTemplateTypeParmType(ST);
    }
  } AnnotationVisitor;
  AnnotationVisitor.SubstituteTypeParam = SubstituteTypeParam;
  AnnotationVisitor.Visit(T);
  return std::move(AnnotationVisitor.Annotations);
}

TypeNullability unspecifiedNullability(const Expr *E) {
  return TypeNullability(countPointersInType(E), NullabilityKind::Unspecified);
}

namespace {

// Visitor to rebuild a QualType with explicit nullability.
// Extra AttributedType nodes are added wrapping interior PointerTypes, and
// other sugar is added as needed to allow this (e.g. TypeSpecializationType).
//
// We only have to handle types that have nontrivial nullability vectors, i.e.
// those handled by NullabilityWalker.
// Additionally, we only operate on canonical types (otherwise the sugar we're
// adding could conflict with existing sugar).
//
// This needs to stay in sync with the other algorithms that manipulate
// nullability data structures for particular types: the non-flow-sensitive
// transfer and NullabilityWalker.
struct Rebuilder : public TypeVisitor<Rebuilder, QualType> {
  Rebuilder(const TypeNullability &Nullability, ASTContext &Ctx)
      : Nullability(Nullability), Ctx(Ctx) {}

  bool done() const { return Nullability.empty(); }

  using Base = TypeVisitor<Rebuilder, QualType>;
  using Base::Visit;
  QualType Visit(QualType T) {
    if (T.isNull()) return T;
    return Ctx.getQualifiedType(Visit(T.getTypePtr()), T.getLocalQualifiers());
  }
  TemplateArgument Visit(TemplateArgument TA) {
    if (TA.getKind() == TemplateArgument::Type)
      return TemplateArgument(Visit(TA.getAsType()));
    return TA;
  }

  // Default behavior for unhandled types: do not transform.
  QualType VisitType(const Type *T) { return QualType(T, 0); }

  QualType VisitPointerType(const PointerType *PT) {
    CHECK(!Nullability.empty())
        << "Nullability vector too short at " << QualType(PT, 0).getAsString();
    NullabilityKind NK = Nullability.front();
    Nullability = Nullability.drop_front();

    QualType Rebuilt = Ctx.getPointerType(Visit(PT->getPointeeType()));
    if (NK == NullabilityKind::Unspecified) return Rebuilt;
    return Ctx.getAttributedType(AttributedType::getNullabilityAttrKind(NK),
                                 Rebuilt, Rebuilt);
  }

  QualType VisitRecordType(const RecordType *RT) {
    if (const auto *CTSD =
            dyn_cast<ClassTemplateSpecializationDecl>(RT->getDecl())) {
      std::vector<TemplateArgument> TransformedArgs;
      for (const auto &Arg : CTSD->getTemplateArgs().asArray())
        TransformedArgs.push_back(Visit(Arg));
      return Ctx.getTemplateSpecializationType(
          TemplateName(CTSD->getSpecializedTemplate()), TransformedArgs,
          QualType(RT, 0));
    }
    return QualType(RT, 0);
  }

  QualType VisitFunctionProtoType(const FunctionProtoType *T) {
    QualType Ret = Visit(T->getReturnType());
    std::vector<QualType> Params;
    for (const auto &Param : T->getParamTypes()) Params.push_back(Visit(Param));
    return Ctx.getFunctionType(Ret, Params, T->getExtProtoInfo());
  }

  QualType VisitLValueReferenceType(const LValueReferenceType *T) {
    return Ctx.getLValueReferenceType(Visit(T->getPointeeType()));
  }
  QualType VisitRValueReferenceType(const RValueReferenceType *T) {
    return Ctx.getRValueReferenceType(Visit(T->getPointeeType()));
  }

  QualType VisitConstantArrayType(const ConstantArrayType *AT) {
    return Ctx.getConstantArrayType(Visit(AT->getElementType()), AT->getSize(),
                                    AT->getSizeExpr(), AT->getSizeModifier(),
                                    AT->getIndexTypeCVRQualifiers());
  }
  QualType VisitIncompleteArrayType(const IncompleteArrayType *AT) {
    return Ctx.getIncompleteArrayType(Visit(AT->getElementType()),
                                      AT->getSizeModifier(),
                                      AT->getIndexTypeCVRQualifiers());
  }
  QualType VisitVariableArrayType(const VariableArrayType *AT) {
    return Ctx.getVariableArrayType(
        Visit(AT->getElementType()), AT->getSizeExpr(), AT->getSizeModifier(),
        AT->getIndexTypeCVRQualifiers(), AT->getBracketsRange());
  }

 private:
  ArrayRef<NullabilityKind> Nullability;
  ASTContext &Ctx;
};

}  // namespace

QualType rebuildWithNullability(QualType T, const TypeNullability &Nullability,
                                ASTContext &Ctx) {
  Rebuilder V(Nullability, Ctx);
  QualType Result = V.Visit(T.getCanonicalType());
  CHECK(V.done()) << "Nullability vector[" << Nullability.size()
                  << "] too long for " << T.getAsString();
  return Result;
}

std::string printWithNullability(QualType T, const TypeNullability &Nullability,
                                 ASTContext &Ctx) {
  return rebuildWithNullability(T, Nullability, Ctx)
      .getAsString(Ctx.getPrintingPolicy());
}

}  // namespace clang::tidy::nullability
