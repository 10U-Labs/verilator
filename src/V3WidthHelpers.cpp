// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Expression width calculations - iterate and utility helpers
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

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3WidthVisitor.h"

VL_DEFINE_DEBUG_FUNCTIONS;

//######################################################################
// WidthVisitor -- special iterators
// These functions save/restore the AstNUser information so it can pass to child nodes.

AstNode* WidthVisitor::userIterateSubtreeReturnEdits(AstNode* nodep, WidthVP* vup) {
    if (!nodep) return nullptr;
    AstNode* ret;
    {
        VL_RESTORER(m_vup);
        m_vup = vup;
        ret = iterateSubtreeReturnEdits(nodep);
    }
    return ret;
}
void WidthVisitor::userIterate(AstNode* nodep, WidthVP* vup) {
    if (!nodep) return;
    VL_RESTORER(m_vup);
    m_vup = vup;
    iterate(nodep);
}
void WidthVisitor::userIterateAndNext(AstNode* nodep, WidthVP* vup) {
    if (!nodep) return;
    if (nodep->didWidth()) return;  // Avoid iterating list we have already iterated
    VL_RESTORER(m_vup);
    m_vup = vup;
    iterateAndNextNull(nodep);
}
void WidthVisitor::userIterateChildren(AstNode* nodep, WidthVP* vup) {
    if (!nodep) return;
    VL_RESTORER(m_vup);
    m_vup = vup;
    iterateChildren(nodep);
}
void WidthVisitor::userIterateChildrenBackwardsConst(AstNode* nodep, WidthVP* vup) {
    if (!nodep) return;
    VL_RESTORER(m_vup);
    m_vup = vup;
    iterateChildrenBackwardsConst(nodep);
}

//######################################################################
// WidthVisitor -- iterate check convenience wrappers

void WidthVisitor::iterateCheckFileDesc(AstNode* parentp, AstNode* underp, Stage stage) {
    UASSERT_OBJ(stage == BOTH, parentp, "Bad call");
    // underp may change as a result of replacement
    underp = userIterateSubtreeReturnEdits(underp, WidthVP{SELF, PRELIM}.p());
    AstNodeDType* const expDTypep = underp->findUInt32DType();
    underp
        = iterateCheck(parentp, "file_descriptor", underp, SELF, FINAL, expDTypep, EXTEND_EXP);
    (void)underp;  // cppcheck
}
void WidthVisitor::iterateCheckReal(AstNode* parentp, const char* side, AstNode* underp,
                                    Stage stage) {
    // Coerce child to real if not already. Child is self-determined
    // e.g. parentp=ADDD, underp=ADD in ADDD(ADD(a,b), real-CONST)
    // Don't need separate PRELIM and FINAL(double) calls;
    // as if resolves to double, the BOTH correctly resolved double,
    // otherwise self-determined was correct
    iterateCheckTypedSelfPrelim(parentp, side, underp, parentp->findDoubleDType(), stage);
}
void WidthVisitor::iterateCheckSigned8(AstNode* parentp, const char* side, AstNode* underp,
                                       Stage stage) {
    // Coerce child to signed8 if not already. Child is self-determined
    iterateCheckTypedSelfPrelim(parentp, side, underp, parentp->findSigned8DType(), stage);
}
void WidthVisitor::iterateCheckSigned32(AstNode* parentp, const char* side, AstNode* underp,
                                        Stage stage) {
    // Coerce child to signed32 if not already. Child is self-determined
    iterateCheckTypedSelfPrelim(parentp, side, underp, parentp->findSigned32DType(), stage);
}
void WidthVisitor::iterateCheckUInt32(AstNode* parentp, const char* side, AstNode* underp,
                                      Stage stage) {
    // Coerce child to unsigned32 if not already. Child is self-determined
    iterateCheckTypedSelfPrelim(parentp, side, underp, parentp->findUInt32DType(), stage);
}
void WidthVisitor::iterateCheckDelay(AstNode* parentp, const char* side, AstNode* underp,
                                     Stage stage) {
    // Coerce child to 64-bit delay if not already. Child is self-determined
    // underp may change as a result of replacement
    if (stage & PRELIM) {
        underp = userIterateSubtreeReturnEdits(underp, WidthVP{SELF, PRELIM}.p());
    }
    if (stage & FINAL) {
        AstNodeDType* expDTypep;
        if (underp->dtypep()->skipRefp()->isDouble()) {  // V3Timing will later convert double
            expDTypep = parentp->findDoubleDType();
        } else {
            FileLine* const newFl = new FileLine{underp->fileline()};
            newFl->warnOff(V3ErrorCode::WIDTHEXPAND, true);
            underp->fileline(newFl);
            expDTypep = parentp->findLogicDType(64, 64, VSigning::UNSIGNED);
        }
        underp
            = iterateCheck(parentp, side, underp, SELF, FINAL, expDTypep, EXTEND_EXP, false);
    }
    (void)underp;  // cppcheck
}
void WidthVisitor::iterateCheckTypedSelfPrelim(AstNode* parentp, const char* side,
                                               AstNode* underp, AstNodeDType* expDTypep,
                                               Stage stage) {
    // underp may change as a result of replacement
    if (stage & PRELIM) {
        underp = userIterateSubtreeReturnEdits(underp, WidthVP{SELF, PRELIM}.p());
    }
    if (stage & FINAL) {
        underp = iterateCheck(parentp, side, underp, SELF, FINAL, expDTypep, EXTEND_EXP);
    }
    (void)underp;  // cppcheck
}
void WidthVisitor::iterateCheckIntegralSelf(AstNode* parentp, const char* side, AstNode* underp,
                                            Determ determ, Stage stage) {
    // Like iterateCheckSelf but with fatal conversion check for integral types
    UASSERT_OBJ(determ == SELF, parentp, "Bad call");
    UASSERT_OBJ(stage == FINAL || stage == BOTH, parentp, "Bad call");
    // underp may change as a result of replacement
    if (stage & PRELIM) {
        underp = userIterateSubtreeReturnEdits(underp, WidthVP{SELF, PRELIM}.p());
    }
    underp
        = VN_IS(underp, NodeExpr) ? checkCvtUS(VN_AS(underp, NodeExpr), true) : underp;
    AstNodeDType* const expDTypep = underp->dtypep();
    underp = iterateCheck(parentp, side, underp, SELF, FINAL, expDTypep, EXTEND_EXP);
    (void)underp;  // cppcheck
}
void WidthVisitor::iterateCheckSelf(AstNode* parentp, const char* side, AstNode* underp,
                                    Determ determ, Stage stage) {
    // Coerce child to any data type; child is self-determined
    // i.e. isolated from expected type.
    // e.g. parentp=CONCAT, underp=lhs in CONCAT(lhs,rhs)
    UASSERT_OBJ(determ == SELF, parentp, "Bad call");
    UASSERT_OBJ(stage == FINAL || stage == BOTH, parentp, "Bad call");
    // underp may change as a result of replacement
    if (stage & PRELIM) {
        underp = userIterateSubtreeReturnEdits(underp, WidthVP{SELF, PRELIM}.p());
    }
    underp
        = VN_IS(underp, NodeExpr) ? checkCvtUS(VN_AS(underp, NodeExpr), false) : underp;
    AstNodeDType* const expDTypep = underp->dtypep();
    underp = iterateCheck(parentp, side, underp, SELF, FINAL, expDTypep, EXTEND_EXP);
    (void)underp;  // cppcheck
}
void WidthVisitor::iterateCheckSizedSelf(AstNode* parentp, const char* side, AstNode* underp,
                                         Determ determ, Stage stage) {
    // Coerce child to any sized-number data type; child is self-determined
    // i.e. isolated from expected type.
    // e.g. parentp=CONCAT, underp=lhs in CONCAT(lhs,rhs)
    UASSERT_OBJ(determ == SELF, parentp, "Bad call");
    UASSERT_OBJ(stage == FINAL || stage == BOTH, parentp, "Bad call");
    // underp may change as a result of replacement
    if (stage & PRELIM) {
        underp = userIterateSubtreeReturnEdits(underp, WidthVP{SELF, PRELIM}.p());
    }
    underp = VN_IS(underp, NodeExpr) ? checkCvtUS(VN_AS(underp, NodeExpr), false) : underp;
    AstNodeDType* const expDTypep = underp->dtypep();
    underp = iterateCheck(parentp, side, underp, SELF, FINAL, expDTypep, EXTEND_EXP);
    AstNodeDType* const checkDtp = expDTypep->skipRefToEnump();
    if (!checkDtp->isIntegralOrPacked()) {
        parentp->v3error("Expected numeric type, but got a " << checkDtp->prettyDTypeNameQ()
                                                             << " data type");
    }
    (void)underp;  // cppcheck
}

//######################################################################
// WidthVisitor -- type conversion helpers

void WidthVisitor::visit_Or_Lu64(AstNodeUniop* nodep) {
    // CALLER: AstBitsToRealD
    // Real: Output real
    // LHS presumed self-determined, then coerced to real
    assertAtExpr(nodep);
    if (m_vup->prelim()) {  // First stage evaluation
        nodep->dtypeSetDouble();
        AstNodeDType* const subDTypep = nodep->findLogicDType(64, 64, VSigning::UNSIGNED);
        // Self-determined operand
        userIterateAndNext(nodep->lhsp(), WidthVP{SELF, PRELIM}.p());
        iterateCheck(nodep, "LHS", nodep->lhsp(), SELF, FINAL, subDTypep, EXTEND_EXP);
    }
}
void WidthVisitor::visit(AstIToRD* nodep) {
    // Real: Output real
    // LHS presumed self-determined, then coerced to real
    assertAtExpr(nodep);
    if (m_vup->prelim()) {  // First stage evaluation
        nodep->dtypeSetDouble();
        // Self-determined operand (TODO check if numeric type)
        userIterateAndNext(nodep->lhsp(), WidthVP{SELF, PRELIM}.p());
        if (nodep->lhsp()->isSigned()) {
            nodep->replaceWith(
                new AstISToRD{nodep->fileline(), nodep->lhsp()->unlinkFrBack()});
            VL_DO_DANGLING(nodep->deleteTree(), nodep);
        }
    }
}
void WidthVisitor::visit(AstISToRD* nodep) {
    // Real: Output real
    // LHS presumed self-determined, then coerced to real
    assertAtExpr(nodep);
    if (m_vup->prelim()) {  // First stage evaluation
        nodep->dtypeSetDouble();
        // Self-determined operand (TODO check if numeric type)
        userIterateAndNext(nodep->lhsp(), WidthVP{SELF, PRELIM}.p());
    }
}
void WidthVisitor::visit_Os32_Lr(AstNodeUniop* nodep) {
    // CALLER: RToI
    // Real: LHS real
    // LHS presumed self-determined, then coerced to real
    assertAtExpr(nodep);
    if (m_vup->prelim()) {  // First stage evaluation
        iterateCheckReal(nodep, "LHS", nodep->lhsp(), BOTH);
        nodep->dtypeSetSigned32();
    }
}
void WidthVisitor::visit_Ou64_Lr(AstNodeUniop* nodep) {
    // CALLER: RealToBits
    // Real: LHS real
    // LHS presumed self-determined, then coerced to real
    assertAtExpr(nodep);
    if (m_vup->prelim()) {  // First stage evaluation
        iterateCheckReal(nodep, "LHS", nodep->lhsp(), BOTH);
        nodep->dtypeSetUInt64();
    }
}

//######################################################################
// WidthVisitor -- open array helpers

void WidthVisitor::makeOpenArrayShell(AstNodeFTaskRef* nodep) {
    UINFO(4, "Replicate openarray function " << nodep->taskp());
    AstNodeFTask* const oldTaskp = nodep->taskp();
    oldTaskp->dpiOpenParentInc();
    UASSERT_OBJ(!oldTaskp->dpiOpenChild(), oldTaskp,
                "DPI task should be parent or child, not both");
    AstNodeFTask* const newTaskp = oldTaskp->cloneTree(false);
    newTaskp->dpiOpenChild(true);
    newTaskp->dpiOpenParentClear();
    newTaskp->name(newTaskp->name() + "__Vdpioc" + cvtToStr(oldTaskp->dpiOpenParent()));
    oldTaskp->addNextHere(newTaskp);
    // Relink reference to new function
    nodep->taskp(newTaskp);
    nodep->name(nodep->taskp()->name());
    // Replace open array arguments with the callee's task
    const V3TaskConnects tconnects = V3Task::taskConnects(nodep, nodep->taskp()->stmtsp());
    for (const auto& tconnect : tconnects) {
        AstVar* const portp = tconnect.first;
        const AstArg* const argp = tconnect.second;
        const AstNode* const pinp = argp->exprp();
        if (!pinp) continue;  // Argument error we'll find later
        if (hasOpenArrayDTypeRecurse(portp->dtypep())) portp->dtypep(pinp->dtypep());
    }
}

bool WidthVisitor::markHasOpenArray(AstNodeFTask* nodep) {
    bool hasOpen = false;
    for (AstNode* stmtp = nodep->stmtsp(); stmtp; stmtp = stmtp->nextp()) {
        if (AstVar* const portp = VN_CAST(stmtp, Var)) {
            if (portp->isDpiOpenArray() || hasOpenArrayDTypeRecurse(portp->dtypep())) {
                portp->isDpiOpenArray(true);
                hasOpen = true;
            }
        }
    }
    return hasOpen;
}
bool WidthVisitor::hasOpenArrayDTypeRecurse(AstNodeDType* nodep) {
    // Return true iff this datatype or child has an openarray
    if (VN_IS(nodep, UnsizedArrayDType)) return true;
    if (nodep->subDTypep()) return hasOpenArrayDTypeRecurse(nodep->subDTypep()->skipRefp());
    return false;
}

//######################################################################
// WidthVisitor -- miscellaneous core helpers

AstPackage* WidthVisitor::getItemPackage(AstNode* pkgItemp) {
    while (pkgItemp->backp() && pkgItemp->backp()->nextp() == pkgItemp) {
        pkgItemp = pkgItemp->backp();
    }
    return VN_CAST(pkgItemp->backp(), Package);
}
const AstClass* WidthVisitor::containingClass(AstNode* nodep) {
    // abovep is still needed, m_containingClassp is just a cache
    if (const AstClass* const classp = VN_CAST(nodep, Class))
        return m_containingClassp[nodep] = classp;
    if (const AstClassPackage* const packagep = VN_CAST(nodep, ClassPackage)) {
        return m_containingClassp[nodep] = packagep->classp();
    }
    if (m_containingClassp.find(nodep) != m_containingClassp.end()) {
        return m_containingClassp[nodep];
    }
    if (AstNode* const abovep = nodep->aboveLoopp()) {
        return m_containingClassp[nodep] = containingClass(abovep);
    } else {
        return m_containingClassp[nodep] = nullptr;
    }
}
bool WidthVisitor::areSameSize(AstUnpackArrayDType* dtypep0, AstUnpackArrayDType* dtypep1) {
    // Returns true if dtypep0 and dtypep1 have same dimensions
    const std::vector<AstUnpackArrayDType*> dims0 = dtypep0->unpackDimensions();
    const std::vector<AstUnpackArrayDType*> dims1 = dtypep1->unpackDimensions();
    if (dims0.size() != dims1.size()) return false;
    for (size_t i = 0; i < dims0.size(); ++i) {
        if (dims0[i]->elementsConst() != dims1[i]->elementsConst()) return false;
    }
    return true;
}
AstVar* WidthVisitor::memberSelClocking(AstMemberSel* nodep, AstClocking* clockingp) {
    // Returns node if ok
    VSpellCheck speller;

    for (AstNode* itemp = clockingp->itemsp(); itemp; itemp = itemp->nextp()) {
        if (AstClockingItem* citemp = VN_CAST(itemp, ClockingItem)) {
            if (citemp->varp()->name() == nodep->name()) return citemp->varp();
            speller.pushCandidate(citemp->varp()->prettyName());
        }
    }
    const string suggest = speller.bestCandidateMsg(nodep->prettyName());
    nodep->v3error(
        "Member " << nodep->prettyNameQ() << " not found in clocking block "
                  << clockingp->prettyNameQ() << "\n"
                  << (suggest.empty() ? "" : nodep->fileline()->warnMore() + suggest));
    return nullptr;  // Caller handles error
}
