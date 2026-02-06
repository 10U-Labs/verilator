// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Expression width calculations - internal header
//
// Code available from: https://verilator.org
//
//*************************************************************************
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2003-2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************
// Internal header for V3Width family of files.
// Not for use outside the V3Width* compilation units.
//*************************************************************************

#ifndef VERILATOR_V3WIDTHVISITOR_H_
#define VERILATOR_V3WIDTHVISITOR_H_

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3Ast.h"
#include "V3Begin.h"
#include "V3Const.h"
#include "V3Error.h"
#include "V3Global.h"
#include "V3LinkLValue.h"
#include "V3MemberMap.h"
#include "V3Number.h"
#include "V3Randomize.h"
#include "V3String.h"
#include "V3Task.h"
#include "V3UniqueNames.h"
#include "V3Width.h"
#include "V3WidthCommit.h"

//######################################################################

enum Stage : uint8_t {
    PRELIM = 1,
    FINAL = 2,
    BOTH = 3
};  // Numbers are a bitmask <0>=prelim, <1>=final
inline std::ostream& operator<<(std::ostream& str, const Stage& rhs) {
    return str << ("-PFB"[static_cast<int>(rhs)]);
}

enum Determ : uint8_t {
    SELF,  // Self-determined
    CONTEXT_DET,  // Context-determined
    ASSIGN  // Assignment-like where sign comes from RHS only
};
inline std::ostream& operator<<(std::ostream& str, const Determ& rhs) {
    static const char* const s_det[] = {"SELF", "CNTX", "ASSN"};
    return str << s_det[rhs];
}

#define v3widthWarn(lhs, rhs, msg) \
    v3warnCode(((lhs) < (rhs)   ? V3ErrorCode::WIDTHTRUNC \
                : (lhs) > (rhs) ? V3ErrorCode::WIDTHEXPAND \
                                : V3ErrorCode::WIDTH), \
               msg)

//######################################################################
// Width state, as a visitor of each AstNode

class WidthVP final {
    // Parameters to pass down hierarchy with visit functions.
    AstNodeDType* const m_dtypep;  // Parent's data type to resolve to
    const Stage m_stage;  // If true, report errors
public:
    WidthVP(AstNodeDType* dtypep, Stage stage)
        : m_dtypep{dtypep}
        , m_stage{stage} {
        // Prelim doesn't look at assignments, so shouldn't need a dtype,
        // however AstPattern uses them
    }
    WidthVP(Determ determ, Stage stage)
        : m_dtypep{nullptr}
        , m_stage{stage} {
        if (determ != SELF && stage != PRELIM)
            v3fatalSrc("Context-determined width request only allowed as prelim step");
    }
    WidthVP* p() { return this; }
    bool selfDtm() const { return m_dtypep == nullptr; }
    AstNodeDType* dtypep() const {
        // Detect where overrideDType is probably the intended call
        UASSERT(m_dtypep, "Width dtype request on self-determined or preliminary VUP");
        return m_dtypep;
    }
    AstNodeDType* dtypeNullp() const { return m_dtypep; }
    AstNodeDType* dtypeNullSkipRefp() const {
        AstNodeDType* dtp = dtypeNullp();
        if (dtp) dtp = dtp->skipRefp();
        return dtp;
    }
    AstNodeDType* dtypeOverridep(AstNodeDType* defaultp) const {
        UASSERT(m_stage != PRELIM, "Parent dtype should be a final-stage action");
        return m_dtypep ? m_dtypep : defaultp;
    }
    int width() const {
        UASSERT(m_dtypep, "Width request on self-determined or preliminary VUP");
        return m_dtypep->width();
    }
    int widthMin() const {
        UASSERT(m_dtypep, "Width request on self-determined or preliminary VUP");
        return m_dtypep->widthMin();
    }
    bool prelim() const { return m_stage & PRELIM; }
    bool final() const { return m_stage & FINAL; }
    void dump(std::ostream& str) const {
        if (!m_dtypep) {
            str << "  VUP(s=" << m_stage << ",self)";
        } else {
            str << "  VUP(s=" << m_stage << ",dt=" << cvtToHex(dtypep());
            dtypep()->dumpSmall(str);
            str << ")";
        }
    }
};
inline std::ostream& operator<<(std::ostream& str, const WidthVP* vup) {
    if (vup) vup->dump(str);
    return str;
}

//######################################################################

class WidthClearVisitor final {
    // Rather than a VNVisitor, can just quickly touch every node
    void clearWidthRecurse(AstNode* nodep) {
        for (; nodep; nodep = nodep->nextp()) {
            nodep->didWidth(false);
            if (AstNode* const refp = nodep->op1p()) clearWidthRecurse(refp);
            if (AstNode* const refp = nodep->op2p()) clearWidthRecurse(refp);
            if (AstNode* const refp = nodep->op3p()) clearWidthRecurse(refp);
            if (AstNode* const refp = nodep->op4p()) clearWidthRecurse(refp);
        }
    }

public:
    // CONSTRUCTORS
    explicit WidthClearVisitor(AstNetlist* nodep) { clearWidthRecurse(nodep); }
    virtual ~WidthClearVisitor() = default;
};

//######################################################################

#define accept in_WidthVisitor_use_AstNode_iterate_instead_of_AstNode_accept

//######################################################################

class WidthVisitor final : public VNVisitor {
    // TYPES
    using TableMap = std::map<std::pair<const AstNodeDType*, VAttrType>, AstVar*>;
    using PatVecMap = std::map<int, AstPatMember*>;
    using DTypeMap = std::map<const std::string, AstPatMember*>;

    // STATE
    V3UniqueNames m_insideTempNames;  // For generating unique temporary variable names for
                                      // `inside` expressions
    VMemberMap m_memberMap;  // Member names cached for fast lookup
    V3TaskConnectState m_taskConnectState;  // State to cache V3Task::taskConnects
    WidthVP* m_vup = nullptr;  // Current node state
    bool m_underFork = false;  // Visiting under a fork
    bool m_underSExpr = false;  // Visiting under a sequence expression
    bool m_underPackedArray = false;  // Visiting under a AstPackArrayDType
    bool m_hasNamedType = false;  // Packed array is defined using named type
    AstNode* m_seqUnsupp = nullptr;  // Property has unsupported node
    bool m_hasSExpr = false;  // Property has a sequence expression
    const AstCell* m_cellp = nullptr;  // Current cell for arrayed instantiations
    const AstEnumItem* m_enumItemp = nullptr;  // Current enum item
    AstNodeFTask* m_ftaskp = nullptr;  // Current function/task
    AstNodeModule* m_modep = nullptr;  // Current module
    const AstConstraint* m_constraintp = nullptr;  // Current constraint
    AstNodeProcedure* m_procedurep = nullptr;  // Current final/always
    const AstWith* m_withp = nullptr;  // Current 'with' statement
    const AstFunc* m_funcp = nullptr;  // Current function
    const AstAttrOf* m_attrp = nullptr;  // Current attribute
    const AstNodeExpr* m_randomizeFromp = nullptr;  // Current randomize method call fromp
    const bool m_paramsOnly;  // Computing parameter value; limit operation
    const bool m_doGenerate;  // Do errors later inside generate statement
    bool m_streamConcat = false;  // True if visiting arguments of stream concatenation
    int m_dtTables = 0;  // Number of created data type tables
    TableMap m_tableMap;  // Created tables so can remove duplicates
    std::map<const AstNodeDType*, AstQueueDType*>
        m_queueDTypeIndexed;  // Queues with given index type
    std::map<const AstNode*, const AstClass*>
        m_containingClassp;  // Containing class cache for containingClass() function
    std::unordered_set<AstVar*> m_aliasedVars;  // Variables referenced in alias

    static constexpr int ENUM_LOOKUP_BITS = 16;  // Maximum # bits to make enum lookup table

    // ENUMS
    enum ExtendRule : uint8_t {
        EXTEND_EXP,  // Extend if expect sign and node signed
        EXTEND_ZERO,  // Extend with zeros
        EXTEND_LHS,  // Extend with sign if node signed
        EXTEND_OFF  // No extension
    };

    // INLINE HELPERS
    int widthUnpacked(const AstNodeDType* const dtypep) {
        if (const AstUnpackArrayDType* const arrDtypep = VN_CAST(dtypep, UnpackArrayDType)) {
            return arrDtypep->subDTypep()->width() * arrDtypep->arrayUnpackedElements();
        }
        return dtypep->width();
    }
    static void packIfUnpacked(AstNodeExpr* const nodep) {
        if (AstUnpackArrayDType* const unpackDTypep = VN_CAST(nodep->dtypep(), UnpackArrayDType)) {
            const int elementsNum = unpackDTypep->arrayUnpackedElements();
            const int unpackMinBits = elementsNum * unpackDTypep->subDTypep()->widthMin();
            const int unpackBits = elementsNum * unpackDTypep->subDTypep()->width();
            VNRelinker relinker;
            nodep->unlinkFrBack(&relinker);
            relinker.relink(new AstCvtArrayToPacked{
                nodep->fileline(), nodep,
                nodep->findLogicDType(unpackBits, unpackMinBits, VSigning::UNSIGNED)});
        }
    }

    // =====================================================================
    // VISITOR METHODS - inline one-liner delegators
    // =====================================================================
    // Naming: width_O{outputtype}_L{lhstype}_R{rhstype}_W{widthing}_S{signing}
    //   _O1=boolean, _Ou=unsigned, _Os=signed, _Ous=unsigned or signed, _Or=real, _Ox=anything

    // Widths: 1 bit out, lhs 1 bit; Real: converts via compare with 0
    void visit(AstLogNot* nodep) override { visit_log_not(nodep); }
    // Widths: 1 bit out, lhs 1 bit, rhs 1 bit; Real: converts via compare with 0
    void visit(AstLogAnd* nodep) override { visit_log_and_or(nodep); }
    void visit(AstLogOr* nodep) override { visit_log_and_or(nodep); }
    void visit(AstLogEq* nodep) override { visit_log_and_or(nodep); }
    void visit(AstLogIf* nodep) override { visit_log_and_or(nodep); }
    // Widths: 1 bit out, Any width lhs
    void visit(AstRedAnd* nodep) override { visit_red_and_or(nodep); }
    void visit(AstRedOr* nodep) override { visit_red_and_or(nodep); }
    void visit(AstRedXor* nodep) override { visit_red_and_or(nodep); }
    void visit(AstOneHot* nodep) override { visit_red_and_or(nodep); }
    void visit(AstOneHot0* nodep) override { visit_red_and_or(nodep); }
    void visit(AstIsUnknown* nodep) override { visit_red_unknown(nodep); }
    // Widths: 1 bit out, lhs width == rhs width. real if lhs|rhs real
    void visit(AstEq* nodep) override { visit_cmp_eq_gt(nodep, true); }
    void visit(AstNeq* nodep) override { visit_cmp_eq_gt(nodep, true); }
    void visit(AstGt* nodep) override { visit_cmp_eq_gt(nodep, true); }
    void visit(AstGte* nodep) override { visit_cmp_eq_gt(nodep, true); }
    void visit(AstLt* nodep) override { visit_cmp_eq_gt(nodep, true); }
    void visit(AstLte* nodep) override { visit_cmp_eq_gt(nodep, true); }
    void visit(AstGtS* nodep) override { visit_cmp_eq_gt(nodep, true); }
    void visit(AstGteS* nodep) override { visit_cmp_eq_gt(nodep, true); }
    void visit(AstLtS* nodep) override { visit_cmp_eq_gt(nodep, true); }
    void visit(AstLteS* nodep) override { visit_cmp_eq_gt(nodep, true); }
    void visit(AstEqCase* nodep) override { visit_cmp_eq_gt(nodep, true); }
    void visit(AstNeqCase* nodep) override { visit_cmp_eq_gt(nodep, true); }
    void visit(AstEqWild* nodep) override { visit_cmp_eq_gt(nodep, false); }
    void visit(AstNeqWild* nodep) override { visit_cmp_eq_gt(nodep, false); }
    // Real compares
    void visit(AstEqD* nodep) override { visit_cmp_real(nodep); }
    void visit(AstNeqD* nodep) override { visit_cmp_real(nodep); }
    void visit(AstLtD* nodep) override { visit_cmp_real(nodep); }
    void visit(AstLteD* nodep) override { visit_cmp_real(nodep); }
    void visit(AstGtD* nodep) override { visit_cmp_real(nodep); }
    void visit(AstGteD* nodep) override { visit_cmp_real(nodep); }
    // String compares
    void visit(AstEqN* nodep) override { visit_cmp_string(nodep); }
    void visit(AstNeqN* nodep) override { visit_cmp_string(nodep); }
    void visit(AstLtN* nodep) override { visit_cmp_string(nodep); }
    void visit(AstLteN* nodep) override { visit_cmp_string(nodep); }
    void visit(AstGtN* nodep) override { visit_cmp_string(nodep); }
    void visit(AstGteN* nodep) override { visit_cmp_string(nodep); }
    // Data type compares
    void visit(AstEqT* nodep) override { visit_cmp_type(nodep); }
    void visit(AstNeqT* nodep) override { visit_cmp_type(nodep); }
    // Widths: out width = lhs width = rhs width
    void visit(AstAnd* nodep) override { visit_boolexpr_and_or(nodep); }
    void visit(AstOr* nodep) override { visit_boolexpr_and_or(nodep); }
    void visit(AstXor* nodep) override { visit_boolexpr_and_or(nodep); }
    void visit(AstBufIf1* nodep) override { visit_boolexpr_and_or(nodep); }
    // Width: Max(Lhs,Rhs). Real: If either side real
    void visit(AstAdd* nodep) override { visit_add_sub_replace(nodep, true); }
    void visit(AstSub* nodep) override { visit_add_sub_replace(nodep, true); }
    void visit(AstDiv* nodep) override { visit_add_sub_replace(nodep, true); }
    void visit(AstMul* nodep) override { visit_add_sub_replace(nodep, true); }
    void visit(AstModDiv* nodep) override { visit_add_sub_replace(nodep, false); }
    void visit(AstModDivS* nodep) override { visit_add_sub_replace(nodep, false); }
    void visit(AstMulS* nodep) override { visit_add_sub_replace(nodep, false); }
    void visit(AstDivS* nodep) override { visit_add_sub_replace(nodep, false); }
    // Unary: out width = lhs width
    void visit(AstNegate* nodep) override { visit_negate_not(nodep, true); }
    void visit(AstNot* nodep) override { visit_negate_not(nodep, false); }
    // Real: inputs and output real
    void visit(AstAddD* nodep) override { visit_real_add_sub(nodep); }
    void visit(AstSubD* nodep) override { visit_real_add_sub(nodep); }
    void visit(AstDivD* nodep) override { visit_real_add_sub(nodep); }
    void visit(AstMulD* nodep) override { visit_real_add_sub(nodep); }
    void visit(AstPowD* nodep) override { visit_real_add_sub(nodep); }
    void visit(AstNodeSystemBiopD* nodep) override { visit_real_add_sub(nodep); }
    void visit(AstNegateD* nodep) override { visit_real_neg_ceil(nodep); }
    void visit(AstNodeSystemUniopD* nodep) override { visit_real_neg_ceil(nodep); }
    // Signed/unsigned
    void visit(AstSigned* nodep) override { visit_signed_unsigned(nodep, VSigning::SIGNED); }
    void visit(AstUnsigned* nodep) override { visit_signed_unsigned(nodep, VSigning::UNSIGNED); }
    // Shifts
    void visit(AstShiftL* nodep) override { visit_shift(nodep); }
    void visit(AstShiftR* nodep) override { visit_shift(nodep); }
    void visit(AstShiftRS* nodep) override { visit_shift(nodep); }
    // Type conversions
    void visit(AstBitsToRealD* nodep) override { visit_Or_Lu64(nodep); }
    void visit(AstRToIS* nodep) override { visit_Os32_Lr(nodep); }
    void visit(AstRealToBits* nodep) override { visit_Ou64_Lr(nodep); }
    // Constants/terminals
    void visit(AstTime* nodep) override { nodep->dtypeSetUInt64(); }
    void visit(AstTimeD* nodep) override { nodep->dtypeSetDouble(); }
    void visit(AstTimePrecision* nodep) override { nodep->dtypeSetSigned32(); }
    void visit(AstScopeName* nodep) override { nodep->dtypeSetUInt64(); }
    // Simple one-liners
    void visit(AstRepeat* nodep) override { nodep->v3fatalSrc("'repeat' missed in LinkJump"); }
    void visit(AstCReturn* nodep) override { nodep->v3fatalSrc("Should not exist yet"); }
    void visit(AstConstraintRef* nodep) override { userIterateChildren(nodep, nullptr); }
    void visit(AstStackTraceF* nodep) override { nodep->dtypeSetString(); }
    void visit(AstReturn* nodep) override { nodep->v3fatalSrc("'return' missed in LinkJump"); }

    // =====================================================================
    // VISITOR METHODS - declared here, defined in V3Width*.cpp files
    // =====================================================================

    // --- V3WidthExpressions.cpp ---
    void visit(AstRToIRoundS* nodep) override;
    void visit(AstLenN* nodep) override;
    void visit(AstPutcN* nodep) override;
    void visit(AstGetcN* nodep) override;
    void visit(AstGetcRefN* nodep) override;
    void visit(AstSubstrN* nodep) override;
    void visit(AstCompareNN* nodep) override;
    void visit(AstAtoN* nodep) override;
    void visit(AstNToI* nodep) override;
    void visit(AstTimeUnit* nodep) override;
    void visit(AstCond* nodep) override;
    void visit(AstConcat* nodep) override;
    void visit(AstConcatN* nodep) override;
    void visit(AstReplicate* nodep) override;
    void visit(AstReplicateN* nodep) override;
    void visit(AstNodeDistBiop* nodep) override;
    void visit(AstNodeDistTriop* nodep) override;
    void visit(AstNodeStream* nodep) override;
    void visit(AstRange* nodep) override;
    void visit(AstAssocSel* nodep) override;
    void visit(AstAlias* nodep) override;
    void visit(AstWildcardSel* nodep) override;
    void visit(AstSliceSel* nodep) override;
    void visit(AstSelBit* nodep) override;
    void visit(AstSelExtract* nodep) override;
    void visit(AstSelPlus* nodep) override;
    void visit(AstSelMinus* nodep) override;
    void visit(AstExtend* nodep) override;
    void visit(AstExtendS* nodep) override;
    void visit(AstConst* nodep) override;
    void visit(AstEmptyQueue* nodep) override;
    void visit(AstToLowerN* nodep) override;
    void visit(AstToUpperN* nodep) override;

    // --- V3WidthMisc.cpp ---
    void visit(AstDefaultDisable* nodep) override;
    void visit(AstDelay* nodep) override;
    void visit(AstFireEvent* nodep) override;
    void visit(AstFork* nodep) override;
    void visit(AstDisableFork* nodep) override;
    void visit(AstWaitFork* nodep) override;
    void visit(AstFell* nodep) override;
    void visit(AstFalling* nodep) override;
    void visit(AstFuture* nodep) override;
    void visit(AstPast* nodep) override;
    void visit(AstRising* nodep) override;
    void visit(AstRose* nodep) override;
    void visit(AstSampled* nodep) override;
    void visit(AstSetuphold* nodep) override;
    void visit(AstStable* nodep) override;
    void visit(AstSteady* nodep) override;
    void visit(AstStmtExpr* nodep) override;
    void visit(AstImplication* nodep) override;
    void visit(AstRand* nodep) override;
    void visit(AstSExpr* nodep) override;
    void visit(AstURandomRange* nodep) override;
    void visit(AstUnbounded* nodep) override;
    void visit(AstInferredDisable* nodep) override;
    void visit(AstIsUnbounded* nodep) override;
    void visit(AstCExpr* nodep) override;
    void visit(AstCExprUser* nodep) override;
    void visit(AstCLog2* nodep) override;
    void visit(AstCgOptionAssign* nodep) override;
    void visit(AstPow* nodep) override;
    void visit(AstPowSU* nodep) override;
    void visit(AstPowSS* nodep) override;
    void visit(AstPowUS* nodep) override;
    void visit(AstCountBits* nodep) override;
    void visit(AstCountOnes* nodep) override;
    void visit(AstCvtPackString* nodep) override;
    void visit(AstCvtPackedToArray* nodep) override;
    void visit(AstCvtArrayToArray* nodep) override;
    void visit(AstCvtArrayToPacked* nodep) override;
    void visit(AstCvtUnpackedToQueue* nodep) override;
    void visit(AstTimeImport* nodep) override;
    void visit(AstEventControl* nodep) override;
    void visit(AstAttrOf* nodep) override;
    void visit(AstPull* nodep) override;
    void visit(AstText* nodep) override;

    // --- V3WidthTypes.cpp ---
    void visit(AstNodeArrayDType* nodep) override;
    void visit(AstAssocArrayDType* nodep) override;
    void visit(AstBracketArrayDType* nodep) override;
    void visit(AstConstraintRefDType* nodep) override;
    void visit(AstDynArrayDType* nodep) override;
    void visit(AstQueueDType* nodep) override;
    void visit(AstVoidDType* nodep) override;
    void visit(AstUnsizedArrayDType* nodep) override;
    void visit(AstWildcardArrayDType* nodep) override;
    void visit(AstBasicDType* nodep) override;
    void visit(AstConstDType* nodep) override;
    void visit(AstRefDType* nodep) override;
    void visit(AstTypedef* nodep) override;
    void visit(AstParamTypeDType* nodep) override;
    void visit(AstRequireDType* nodep) override;
    void visit(AstCastDynamic* nodep) override;
    void visit(AstCastParse* nodep) override;
    void visit(AstCast* nodep) override;
    void visit(AstCastSize* nodep) override;
    void visit(AstCastWrap* nodep) override;

    // --- V3WidthVarEnum.cpp ---
    void visit(AstConstraintBefore* nodep) override;
    void visit(AstConstraintExpr* nodep) override;
    void visit(AstConstraintUnique* nodep) override;
    void visit(AstDistItem* nodep) override;
    void visit(AstVar* nodep) override;
    void visit(AstNodeVarRef* nodep) override;
    void visit(AstEnumDType* nodep) override;
    void visit(AstEnumItem* nodep) override;
    void visit(AstEnumItemRef* nodep) override;
    void visit(AstConsAssoc* nodep) override;
    void visit(AstSetAssoc* nodep) override;
    void visit(AstConsWildcard* nodep) override;
    void visit(AstSetWildcard* nodep) override;
    void visit(AstConsPackUOrStruct* nodep) override;
    void visit(AstConsPackMember* nodep) override;
    void visit(AstConsDynArray* nodep) override;
    void visit(AstConsQueue* nodep) override;
    void visit(AstInitItem* nodep) override;
    void visit(AstInitArray* nodep) override;
    void visit(AstNodeAssign* nodep) override;
    void visit(AstRSRule* nodep) override;
    void visit(AstRelease* nodep) override;

    // --- V3WidthPattern.cpp ---
    void visit(AstInside* nodep) override;
    void visit(AstMatches* nodep) override;
    void visit(AstPattern* nodep) override;
    void visit(AstPatMember* nodep) override;
    void visit(AstPropSpec* nodep) override;
    void visit(AstTaggedExpr* nodep) override;
    void visit(AstTaggedPattern* nodep) override;
    void visit(AstPatternVar* nodep) override;
    void visit(AstPatternStar* nodep) override;

    // --- V3WidthMember.cpp ---
    void visit(AstNodeUOrStructDType* nodep) override;
    void visit(AstClassRefDType* nodep) override;
    void visit(AstMemberSel* nodep) override;
    void visit(AstMethodCall* nodep) override;

    // --- V3WidthMethod.cpp ---
    void visit(AstNew* nodep) override;
    void visit(AstNewCopy* nodep) override;
    void visit(AstNewDynamic* nodep) override;

    // --- V3WidthFunc.cpp ---
    void visit(AstGenCase* nodep) override;
    void visit(AstGenFor* nodep) override;
    void visit(AstGenIf* nodep) override;
    void visit(AstCase* nodep) override;
    void visit(AstRandCase* nodep) override;
    void visit(AstLoop* nodep) override;
    void visit(AstLoopTest* nodep) override;
    void visit(AstNodeIf* nodep) override;
    void visit(AstExprStmt* nodep) override;
    void visit(AstNodeForeach* nodep) override;
    void visit(AstNodeFTask* nodep) override;
    void visit(AstConstraint* nodep) override;
    void visit(AstProperty* nodep) override;
    void visit(AstSequence* nodep) override;
    void visit(AstFuncRef* nodep) override;
    void visit(AstNodeFTaskRef* nodep) override;

    // --- V3WidthSystem.cpp ---
    void visit(AstPin* nodep) override;
    void visit(AstCell* nodep) override;
    void visit(AstGatePin* nodep) override;
    void visit(AstSFormat* nodep) override;
    void visit(AstToStringN* nodep) override;
    void visit(AstSFormatF* nodep) override;
    void visit(AstCReset* nodep) override;
    void visit(AstDisplay* nodep) override;
    void visit(AstElabDisplay* nodep) override;
    void visit(AstDumpCtl* nodep) override;
    void visit(AstFOpen* nodep) override;
    void visit(AstFOpenMcd* nodep) override;
    void visit(AstFClose* nodep) override;
    void visit(AstFError* nodep) override;
    void visit(AstFEof* nodep) override;
    void visit(AstFFlush* nodep) override;
    void visit(AstFRewind* nodep) override;
    void visit(AstFTell* nodep) override;
    void visit(AstFSeek* nodep) override;
    void visit(AstFGetC* nodep) override;
    void visit(AstFGetS* nodep) override;
    void visit(AstFUngetC* nodep) override;
    void visit(AstFRead* nodep) override;
    void visit(AstFScanF* nodep) override;
    void visit(AstSScanF* nodep) override;
    void visit(AstSysIgnore* nodep) override;
    void visit(AstSystemF* nodep) override;
    void visit(AstSystemT* nodep) override;
    void visit(AstNodeReadWriteMem* nodep) override;
    void visit(AstTestPlusArgs* nodep) override;
    void visit(AstValuePlusArgs* nodep) override;
    void visit(AstTimeFormat* nodep) override;
    void visit(AstCStmtUser* nodep) override;
    void visit(AstAssert* nodep) override;
    void visit(AstAssertCtl* nodep) override;
    void visit(AstAssertIntrinsic* nodep) override;
    void visit(AstCover* nodep) override;
    void visit(AstRestrict* nodep) override;
    void visit(AstWait* nodep) override;
    void visit(AstDist* nodep) override;
    void visit(AstInsideRange* nodep) override;

    // --- V3WidthHelpers.cpp ---
    void visit(AstIToRD* nodep) override;
    void visit(AstISToRD* nodep) override;

    // --- V3Width.cpp (core) ---
    void visit(AstNetlist* nodep) override;
    void visit(AstNodeExpr* nodep) override;
    void visit(AstResizeLValue* nodep) override;
    void visit(AstNodeModule* nodep) override;
    void visit(AstNode* nodep) override;
    void visit(AstSel* nodep) override;
    void visit(AstArraySel* nodep) override;
    void visit(AstIfaceRefDType* nodep) override;
    void visit(AstThisRef* nodep) override;
    void visit(AstClassOrPackageRef* nodep) override;
    void visit(AstDot* nodep) override;
    void visit(AstClassExtends* nodep) override;
    void visit(AstMemberDType* nodep) override;
    void visit(AstCMethodHard* nodep) override;
    void visit(AstStructSel* nodep) override;
    void visit(AstNodeProcedure* nodep) override;
    void visit(AstSenItem* nodep) override;
    void visit(AstClockingItem* nodep) override;
    void visit(AstWith* nodep) override;
    void visit(AstLambdaArgRef* nodep) override;

    // =====================================================================
    // HELPER METHODS - declared here, defined in V3Width*.cpp files
    // =====================================================================

    // --- V3WidthOperators.cpp ---
    void visit_log_not(AstNode* nodep);
    void visit_log_and_or(AstNodeBiop* nodep);
    void visit_red_and_or(AstNodeUniop* nodep);
    void visit_red_unknown(AstNodeUniop* nodep);
    void visit_cmp_eq_gt(AstNodeBiop* nodep, bool realok);
    void visit_cmp_real(AstNodeBiop* nodep);
    void visit_cmp_string(AstNodeBiop* nodep);
    void visit_cmp_type(AstNodeBiop* nodep);
    void visit_negate_not(AstNodeUniop* nodep, bool real_ok);
    void visit_signed_unsigned(AstNodeUniop* nodep, VSigning rs_out);
    void visit_shift(AstNodeBiop* nodep);
    void iterate_shift_prelim(AstNodeBiop* nodep);
    void visit_boolexpr_and_or(AstNodeBiop* nodep);
    void visit_add_sub_replace(AstNodeBiop* nodep, bool real_ok);
    void visit_real_add_sub(AstNodeBiop* nodep);
    void visit_real_neg_ceil(AstNodeUniop* nodep);
    AstNodeBiop* iterate_shift_final(AstNodeBiop* nodep);
    static bool isAggregateType(const AstNode* nodep);
    AstConst* dimensionValue(FileLine* fileline, AstNodeDType* nodep, VAttrType attrType, int dim);
    AstVar* dimensionVarp(AstNodeDType* nodep, VAttrType attrType, uint32_t msbdim);
    static uint64_t enumMaxValue(const AstNode* errNodep, const AstEnumDType* adtypep);
    static AstVar* enumVarp(AstEnumDType* const nodep, VAttrType attrType, bool assoc,
                            uint32_t msbdim);
    static AstNodeExpr* enumSelect(AstNodeExpr* nodep, AstEnumDType* adtypep, VAttrType attrType);
    static AstNodeExpr* enumTestValid(AstNodeExpr* valp, AstEnumDType* enumDtp);

    // --- V3WidthCheck.cpp ---
    bool widthBad(AstNode* nodep, AstNodeDType* expDTypep);
    void fixWidthExtend(AstNodeExpr* nodep, AstNodeDType* expDTypep, ExtendRule extendRule);
    void fixWidthReduce(AstNodeExpr* nodep);
    bool fixAutoExtend(AstNodeExpr*& nodepr, int expWidth);
    bool isBaseClassRecurse(const AstClass* const cls1p, const AstClass* const cls2p);
    void checkClassAssign(const AstNode* nodep, const char* side, AstNode* rhsp,
                          AstNodeDType* const lhsDTypep);
    static bool similarDTypeRecurse(const AstNodeDType* const node1p,
                                    const AstNodeDType* const node2p);
    void iterateCheckTyped(AstNode* parentp, const char* side, AstNode* underp,
                           AstNodeDType* expDTypep, Stage stage);
    void iterateCheckAssign(AstNode* parentp, const char* side, AstNode* rhsp, Stage stage,
                            AstNodeDType* lhsDTypep);
    void iterateCheckBool(AstNode* parentp, const char* side, AstNode* underp, Stage stage);
    AstNode* iterateCheck(AstNode* parentp, const char* side, AstNode* underp, Determ determ,
                          Stage stage, AstNodeDType* expDTypep, ExtendRule extendRule,
                          bool warnOn = true);
    void widthCheckSized(AstNode* parentp, const char* side, AstNodeExpr* underp,
                         AstNodeDType* expDTypep, ExtendRule extendRule, bool warnOn = true);
    AstNodeExpr* checkCvtUS(AstNodeExpr* nodep, bool fatal);
    AstNodeExpr* spliceCvtD(AstNodeExpr* nodep);
    AstNodeExpr* spliceCvtS(AstNodeExpr* nodep, bool warnOn, int width);
    AstNodeExpr* spliceCvtString(AstNodeExpr* nodep);
    AstNodeBiop* replaceWithUOrSVersion(AstNodeBiop* nodep, bool signedFlavorNeeded);
    AstNodeBiop* replaceWithDVersion(AstNodeBiop* nodep);
    AstNodeBiop* replaceWithNVersion(AstNodeBiop* nodep);
    AstNodeUniop* replaceWithDVersion(AstNodeUniop* nodep);
    void replaceWithSFormat(AstMethodCall* nodep, const string& format);

    // --- V3WidthTypes.cpp ---
    bool isEquivalentDType(const AstNodeDType* lhs, const AstNodeDType* rhs);
    AstNodeDType* iterateEditMoveDTypep(AstNode* parentp, AstNodeDType* dtnodep);
    void castSized(AstNode* nodep, AstNode* underp, int width);
    void assertAtExpr(AstNode* nodep);
    void assertAtStatement(AstNode* nodep);
    void checkConstantOrReplace(AstNode* nodep, bool noFourState, const string& message);
    static AstVarRef* newVarRefDollarUnit(AstVar* nodep);
    AstNode* nodeForUnsizedWarning(AstNode* nodep);
    AstRefDType* checkRefToTypedefRecurse(AstNode* nodep, AstTypedef* typedefp);
    AstNodeDType* dtypeNotIntAtomOrVecRecurse(AstNodeDType* nodep, bool ranged = false);
    AstNodeDType* dtypeNot4StateIntegralRecurse(AstNodeDType* nodep);

    // --- V3WidthMember.cpp ---
    bool memberSelClass(AstMemberSel* nodep, AstClassRefDType* adtypep);
    AstNode* memberSelIface(AstMemberSel* nodep, AstIfaceRefDType* adtypep);
    bool memberSelStruct(AstMemberSel* nodep, AstNodeUOrStructDType* adtypep);
    void methodOkArguments(AstNodeFTaskRef* nodep, int minArg, int maxArg);
    void methodCallEnum(AstMethodCall* nodep, AstEnumDType* adtypep);
    void methodCallWildcard(AstMethodCall* nodep, AstWildcardArrayDType* adtypep);
    void methodCallAssoc(AstMethodCall* nodep, AstAssocArrayDType* adtypep);
    AstWith* methodWithArgument(AstNodeFTaskRef* nodep, bool required, bool arbReturn,
                                AstNodeDType* returnDtp, AstNodeDType* indexDtp,
                                AstNodeDType* valueDtp);
    AstCMethodHard* methodCallArray(AstMethodCall* nodep, AstNodeDType* adtypep);

    // --- V3WidthMethod.cpp ---
    AstNodeExpr* insideItem(AstNode* nodep, AstNodeExpr* exprp, AstNodeExpr* itemp);
    void methodCallDyn(AstMethodCall* nodep, AstDynArrayDType* adtypep);
    void methodCallQueue(AstMethodCall* nodep, AstQueueDType* adtypep);
    void methodCallWarnTiming(AstNodeFTaskRef* const nodep, const std::string& className);
    void methodCallIfaceRef(AstMethodCall* nodep, AstIfaceRefDType* adtypep);
    void handleRandomizeArgs(AstNodeFTaskRef* const nodep, AstClass* const classp);
    void methodCallClass(AstMethodCall* nodep, AstClassRefDType* adtypep);
    void methodCallConstraint(AstMethodCall* nodep, AstConstraintRefDType*);
    void methodCallRandMode(AstMethodCall* nodep);
    void methodCallUnpack(AstMethodCall* nodep, AstUnpackArrayDType* adtypep);
    void methodCallEvent(AstMethodCall* nodep, AstBasicDType*);
    void methodCallString(AstMethodCall* nodep, AstBasicDType*);
    AstNodeExpr* methodCallQueueIndexExpr(AstMethodCall* nodep);
    AstQueueDType* queueDTypeIndexedBy(AstNodeDType* indexDTypep);

    // --- V3WidthPattern.cpp ---
    void patternUOrStruct(AstPattern* nodep, AstNodeUOrStructDType* vdtypep,
                          AstPatMember* defaultp);
    void patternArray(AstPattern* nodep, AstNodeArrayDType* arrayDtp, AstPatMember* defaultp);
    void patternAssoc(AstPattern* nodep, AstAssocArrayDType* arrayDtp, AstPatMember* defaultp);
    void patternWildcard(AstPattern* nodep, AstWildcardArrayDType* arrayDtp,
                         AstPatMember* defaultp);
    void patternDynArray(AstPattern* nodep, AstDynArrayDType* arrayp, AstPatMember* defaultp);
    void patternQueue(AstPattern* nodep, AstQueueDType* arrayp, AstPatMember* defaultp);
    void patternBasic(AstPattern* nodep, AstNodeDType* vdtypep, AstPatMember* defaultp);
    AstNodeExpr* nestedvalueConcat_patternUOrStruct(AstNodeUOrStructDType* memp_vdtypep,
                                                    AstPatMember* defaultp, AstNodeExpr* newp,
                                                    AstPattern* nodep, const DTypeMap& dtypemap);
    AstNodeExpr* valueConcat_patternUOrStruct(AstPatMember* patp, AstNodeExpr* newp,
                                              AstMemberDType* memp, AstPattern* nodep);
    AstNodeExpr* patternMemberValueIterate(AstPatMember* patp);
    AstPatMember* defaultPatp_patternUOrStruct(AstPattern* nodep, AstMemberDType* memp,
                                               AstNodeUOrStructDType* memp_vdtypep,
                                               AstPatMember* defaultp, const DTypeMap& dtypemap);
    int visitPatMemberRep(AstPatMember* nodep);
    PatVecMap patVectorMap(AstPattern* nodep, const VNumRange& range);
    void patConcatConvertRecurse(AstPattern* patternp, AstConcat* nodep);
    static void checkEventAssignment(const AstNodeAssign* const asgnp);
    static bool usesDynamicScheduler(AstNode* nodep);

    // --- V3WidthFunc.cpp ---
    template <typename CaseItem>
    void handleCaseType(AstNode* casep, AstNodeExpr* exprp, CaseItem* itemsp);
    template <typename CaseItem>
    void handleCase(AstNode* casep, AstNodeExpr* exprp, CaseItem* itemsp);
    void processFTaskRefArgs(AstNodeFTaskRef* nodep);
    void handleStdRandomizeArgs(AstNodeFTaskRef* const nodep);
    void visitClass(AstClass* nodep);

    // --- V3WidthSystem.cpp ---
    void checkUnpackedArrayArgs(AstVar* portp, AstNode* pinp);
    void formatNoStringArg(AstNode* argp, char ch);

    // --- V3WidthMisc.cpp ---
    AstAssignW* convertSetupholdToAssign(FileLine* const flp, AstNodeExpr* const evp,
                                         AstNodeExpr* const delp);
    void checkForceReleaseLhs(AstNode* nodep, AstNode* lhsp);
    AstNode* selectNonConstantRecurse(AstNode* nodep, bool inSel = false);

    // --- V3WidthVarEnum.cpp ---
    void methodCallLValueRecurse(AstMethodCall* nodep, AstNode* childp, const VAccess& access);
    AstNodeExpr* methodArg(AstMethodCall* nodep, int arg);
    AstNodeExpr* methodCallAssocIndexExpr(AstMethodCall* nodep, AstAssocArrayDType* adtypep);
    AstNodeExpr* methodCallWildcardIndexExpr(AstMethodCall* nodep, AstWildcardArrayDType* adtypep);

    // --- V3WidthHelpers.cpp ---
    AstNode* userIterateSubtreeReturnEdits(AstNode* nodep, WidthVP* vup);
    void userIterate(AstNode* nodep, WidthVP* vup);
    void userIterateAndNext(AstNode* nodep, WidthVP* vup);
    void userIterateChildren(AstNode* nodep, WidthVP* vup);
    void userIterateChildrenBackwardsConst(AstNode* nodep, WidthVP* vup);
    void iterateCheckFileDesc(AstNode* parentp, AstNode* underp, Stage stage);
    void iterateCheckReal(AstNode* parentp, const char* side, AstNode* underp, Stage stage);
    void iterateCheckSigned8(AstNode* parentp, const char* side, AstNode* underp, Stage stage);
    void iterateCheckSigned32(AstNode* parentp, const char* side, AstNode* underp, Stage stage);
    void iterateCheckUInt32(AstNode* parentp, const char* side, AstNode* underp, Stage stage);
    void iterateCheckDelay(AstNode* parentp, const char* side, AstNode* underp, Stage stage);
    void iterateCheckTypedSelfPrelim(AstNode* parentp, const char* side, AstNode* underp,
                                     AstNodeDType* expDTypep, Stage stage);
    void iterateCheckIntegralSelf(AstNode* parentp, const char* side, AstNode* underp,
                                  Determ determ, Stage stage);
    void iterateCheckSelf(AstNode* parentp, const char* side, AstNode* underp, Determ determ,
                          Stage stage);
    void iterateCheckSizedSelf(AstNode* parentp, const char* side, AstNode* underp, Determ determ,
                               Stage stage);
    void visit_Or_Lu64(AstNodeUniop* nodep);
    void visit_Os32_Lr(AstNodeUniop* nodep);
    void visit_Ou64_Lr(AstNodeUniop* nodep);
    AstPackage* getItemPackage(AstNode* pkgItemp);
    const AstClass* containingClass(AstNode* nodep);
    static bool areSameSize(AstUnpackArrayDType* dtypep0, AstUnpackArrayDType* dtypep1);
    void makeOpenArrayShell(AstNodeFTaskRef* nodep);
    bool markHasOpenArray(AstNodeFTask* nodep);
    bool hasOpenArrayDTypeRecurse(AstNodeDType* nodep);
    AstVar* memberSelClocking(AstMemberSel* nodep, AstClocking* clockingp);

    // Inline one-liner helpers (defined here because they're trivially short)
    void iterateCheckString(AstNode* parentp, const char* side, AstNode* underp, Stage stage) {
        iterateCheckTyped(parentp, side, underp, parentp->findStringDType(), stage);
    }
    static bool usesDynamicScheduler(AstVarRef* vrefp) {
        return VN_IS(vrefp->classOrPackagep(), Class) || vrefp->varp()->isFuncLocal();
    }

public:
    // CONSTRUCTORS
    WidthVisitor(bool paramsOnly, bool doGenerate)
        : m_insideTempNames{"__VInside"}
        , m_paramsOnly{paramsOnly}
        , m_doGenerate{doGenerate} {}
    AstNode* mainAcceptEdit(AstNode* nodep) {
        return userIterateSubtreeReturnEdits(nodep, WidthVP{SELF, BOTH}.p());
    }
    ~WidthVisitor() override = default;
};

//######################################################################

#endif  // Guard
