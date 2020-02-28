//
//  Codegen.cpp
//  sisp
//
//  Created by 徐可 on 2020/2/19.
//  Copyright © 2020 Beibei Inc. All rights reserved.
//

#include "Codegen.hpp"

#include <vector>

static vector<DIScope *> LexicalBlocks;

DIType *DebugInfo::getDoubleTy() {
    if (DblTy)
        return DblTy;

    DblTy = DBuilder->createBasicType("double", 64, dwarf::DW_ATE_float);
    return DblTy;
}

void DebugInfo::emitLocation(ExprAST *AST) {
    if (!AST)
        return TheParser->getBuilder()->SetCurrentDebugLocation(DebugLoc());

    DIScope *Scope;
    if (LexicalBlocks.empty())
        Scope = TheCU;
    else
        Scope = LexicalBlocks.back();

    TheParser->getBuilder()->SetCurrentDebugLocation(DebugLoc::get(AST->getLine(), AST->getCol(), Scope));
}

static DISubroutineType *CreateFunctionType(unsigned long NumArgs, DIFile *Unit) {
  SmallVector<Metadata *, 8> EltTys;
  DIType *DblTy = SispDbgInfo.getDoubleTy();

  // Add the result type.
  EltTys.push_back(DblTy);

  for (unsigned long i = 0, e = NumArgs; i != e; ++i)
      EltTys.push_back(DblTy);

  return DBuilder->createSubroutineType(DBuilder->getOrCreateTypeArray(EltTys));
}

const std::string& FunctionAST::getName() const {
    return Proto->getName();
}

Value *IntegerLiteralAST::codegen() {
    SispDbgInfo.emitLocation(this);
    return ConstantInt::get(TheParser->getContext(), APInt(64, Val));
}

Value *FloatLiteralAST::codegen() {
    SispDbgInfo.emitLocation(this);
    return ConstantFP::get(TheParser->getContext(), APFloat(Val));
}

Value *VariableExprAST::codegen() {
    Value *V = scope->getVal(Name);
    if (!V)
        LogError("Unkown variable name");

    cout << "VariableExprAST::codegen()" << V << endl;
    SispDbgInfo.emitLocation(this);
    return TheParser->getBuilder()->CreateLoad(V, Name);
}

Value *BinaryExprAST::codegen() {
    SispDbgInfo.emitLocation(this);

    if (Op == tok_equal) {
        auto LHSE = static_cast<VariableExprAST *>(LHS.get());
        if (!LHSE)
            return LogErrorV("destination of '=' must be a variable");

        auto Val = RHS->codegen();
        if (!Val)
            return nullptr;

        auto Variable = scope->getVal(LHSE->getName());
        if (!Variable)
            return LogErrorV("Unkown variable name");

        TheParser->getBuilder()->CreateStore(Val, Variable);
        return Val;
    }

    auto L = LHS->codegen();
    auto R = RHS->codegen();
    if (!L || !R) {
        LogError("BinaryExpr codgen error.");
        return nullptr;
    }

    switch (Op) {
        case tok_add:
            if (L->getType()->isFloatTy() || L->getType()->isFloatTy())
                return TheParser->getBuilder()->CreateFAdd(L, R, "addtmp");
            else if (L->getType()->isIntegerTy() || R->getType()->isIntegerTy())
                return TheParser->getBuilder()->CreateAdd(L, R, "addtmp");
        case tok_sub:
            if (L->getType()->isDoubleTy() || L->getType()->isDoubleTy())
                return TheParser->getBuilder()->CreateFSub(L, R, "subtmp");
            else if (L->getType()->isIntegerTy() || R->getType()->isIntegerTy())
                return TheParser->getBuilder()->CreateSub(L, R, "subtmp");
        case tok_mul:
            if (L->getType()->isDoubleTy() || L->getType()->isDoubleTy())
                return TheParser->getBuilder()->CreateFMul(L, R, "multmp");
            else if (L->getType()->isIntegerTy() || R->getType()->isIntegerTy())
                return TheParser->getBuilder()->CreateMul(L, R, "multmp");
        case tok_less:
            if (L->getType()->isDoubleTy() || L->getType()->isDoubleTy()) {
                L = TheParser->getBuilder()->CreateFCmpULT(L, R, "lttmp");
                return TheParser->getBuilder()->CreateUIToFP(L, Type::getDoubleTy(TheParser->getContext()),
                "booltmp");
            }
            else if (L->getType()->isIntegerTy() || R->getType()->isIntegerTy()) {
                return TheParser->getBuilder()->CreateICmpSLT(L, R, "lttmp");
//                return TheParser->getBuilder()->CreateUIToFP(L, Type::getInt64Ty(TheParser->getContext()), "booltmp");
            }
            else
                assert(false && "not implemented");
        case tok_greater:
            if (L->getType()->isDoubleTy() || L->getType()->isDoubleTy()) {
                R = TheParser->getBuilder()->CreateFCmpUGT(L, R, "gttmp");
                return TheParser->getBuilder()->CreateUIToFP(R, Type::getDoubleTy(TheParser->getContext()),
                "booltmp");
            }
            else if (L->getType()->isIntegerTy() || R->getType()->isIntegerTy()) {
                return TheParser->getBuilder()->CreateICmpSGT(L, R, "gttmp");
//                return TheParser->getBuilder()->CreateUIToFP(R, Type::getInt64Ty(TheParser->getContext()), "booltmp");
            }
            else
                assert(false && "not implemented");
        default:
        {
            auto F = TheParser->getFunction(string("binary") + Op);
            assert(F && "binary operator not found!");
            auto Ops = { L, R };
            return TheParser->getBuilder()->CreateCall(F, Ops, "calltmp");
        }
    }
}

Value *IfExprAST::codegen() {
    SispDbgInfo.emitLocation(this);
    auto CondV = Cond->codegen();
    if (!CondV)
        return nullptr;
    CondV = TheParser->getBuilder()->CreateICmpNE(CondV, ConstantInt::get(TheParser->getContext(), APInt(1, 0)), "ifcond");

    auto F = TheParser->getBuilder()->GetInsertBlock()->getParent();

    auto ThenBlock = BasicBlock::Create(TheParser->getContext(), "then", F);
    auto ElseBlock = BasicBlock::Create(TheParser->getContext(), "else");
    auto MergeBlock = BasicBlock::Create(TheParser->getContext(), "ifcont");

    TheParser->getBuilder()->CreateCondBr(CondV, ThenBlock, ElseBlock);

    TheParser->getBuilder()->SetInsertPoint(ThenBlock);

    auto ThenV = Then->codegen();
    if (!ThenV)
        return nullptr;

    TheParser->getBuilder()->CreateBr(MergeBlock);

    ThenBlock = TheParser->getBuilder()->GetInsertBlock();

    F->getBasicBlockList().push_back(ElseBlock);
    TheParser->getBuilder()->SetInsertPoint(ElseBlock);

    auto ElseV = Else->codegen();
    if (!ElseV)
        return nullptr;

    TheParser->getBuilder()->CreateBr(MergeBlock);

    ElseBlock = TheParser->getBuilder()->GetInsertBlock();

    F->getBasicBlockList().push_back(MergeBlock);
    TheParser->getBuilder()->SetInsertPoint(MergeBlock);

    auto PN = TheParser->getBuilder()->CreatePHI(Type::getInt8Ty(TheParser->getContext()), 2, "iftmp");

    PN->addIncoming(ThenV, ThenBlock);
    PN->addIncoming(ElseV, ElseBlock);

    return PN;
}

Value *ForExprAST::codegen() {
//    auto StartValue = Start->codegen();
//    if (!StartValue)
//        return nullptr;

    auto F = TheParser->getBuilder()->GetInsertBlock()->getParent();

//    auto Alloca = Parser::CreateEntryBlockAlloca(F, Var.get());

    SispDbgInfo.emitLocation(this);

    auto StartVal = Var->codegen();
    if (!StartVal)
        return nullptr;

    auto Alloca = getScope()->getVal(Var->getName());

//    TheParser->getBuilder()->CreateStore(StartVal, Alloca);

    auto LoopBlock = BasicBlock::Create(TheParser->getContext(), "loop", F);
    TheParser->getBuilder()->CreateBr(LoopBlock);
    TheParser->getBuilder()->SetInsertPoint(LoopBlock);

//    getScope()->setVal(Var->getName(), Alloca);

    if (!Body->codegen())
        return nullptr;

    Value *StepVal = nullptr;
    if (Step) {
        StepVal = Step->codegen();
        if (!StepVal)
            return nullptr;
    } else {
        StepVal = ConstantInt::get(TheParser->getContext(), APInt(64, 0));
    }

    auto EndCond = End->codegen();
    if (!EndCond)
        return nullptr;

    auto CurVar = TheParser->getBuilder()->CreateLoad(Alloca, Var->getName());
    auto NextVar = TheParser->getBuilder()->CreateAdd(CurVar, StepVal, "nextvar");
    TheParser->getBuilder()->CreateStore(NextVar, Alloca);

    EndCond = TheParser->getBuilder()->CreateICmpNE(EndCond, ConstantInt::get(TheParser->getContext(), APInt(1, 0)), "loopcond");

    auto AfterBlock = BasicBlock::Create(TheParser->getContext(), "afterloop", F);
    TheParser->getBuilder()->CreateCondBr(EndCond, LoopBlock, AfterBlock);
    TheParser->getBuilder()->SetInsertPoint(AfterBlock);

    return Constant::getNullValue(Type::getInt8Ty(TheParser->getContext()));
}

Value *VarExprAST::codegen() {

    auto F = TheParser->getBuilder()->GetInsertBlock()->getParent();

    Value *InitVal;
    if (Init) {
        InitVal = Init->codegen();
        if (!InitVal)
            return nullptr;
    } else if (Type == tok_type_float) {
        InitVal = ConstantFP::get(TheParser->getContext(), APFloat(0.0));
    } else if (Type == tok_type_bool) {
        InitVal = ConstantInt::get(TheParser->getContext(), APInt(1, 0));
    } else if (Type == tok_type_int) {
        InitVal = ConstantInt::get(TheParser->getContext(), APInt(64, 0));
    } else {
        assert(false && "not implemented type");
    }

    AllocaInst *Alloca = Parser::CreateEntryBlockAlloca(F, this);
    TheParser->getBuilder()->CreateStore(InitVal, Alloca);

    scope->setVal(Name, Alloca);

    return Alloca;
}

Value *UnaryExprAST::codegen() {
    auto OperandV = Operand->codegen();
    if (!OperandV)
        return nullptr;

    auto F = TheParser->getFunction(string("unary") + Opcode);
    if (!F)
        return LogErrorV("Unkown unary operator");

    SispDbgInfo.emitLocation(this);
    return TheParser->getBuilder()->CreateCall(F, OperandV, "unop");
}

Value *CompoundExprAST::codegen() {
    int i = 0;
    Value *RetVal = nullptr;
    for (auto Expr = Exprs.begin(); Expr != Exprs.end(); Expr ++) {
        cout << "subExpr " << to_string(i++) << endl;
        RetVal = (*Expr)->codegen();
        if (!RetVal)
            return nullptr;
    }
    return RetVal ?: Constant::getNullValue(Type::getInt64Ty(TheParser->getContext()));;
}

Value *CallExprAST::codegen() {
    SispDbgInfo.emitLocation(this);
    // Look up the name in the global module table.
    Function *CalleeF = TheParser->getFunction(Callee);
    if (!CalleeF)
        return LogErrorV((string("Unknown function referenced ") + Callee));

    // If argument mismatch error.
    if (CalleeF->arg_size() != Args.size())
        return LogErrorV("Incorrect # arguments passed");

    std::vector<Value *> ArgsV;
    for (unsigned long i = 0, e = Args.size(); i != e; ++i) {
    ArgsV.push_back(Args[i]->codegen());
    if (!ArgsV.back())
        return nullptr;
    }

    return TheParser->getBuilder()->CreateCall(CalleeF, ArgsV, "calltmp");
}

Function *PrototypeAST::codegen() {
    vector<llvm::Type *> ArgTypes;
    for (auto E = Args.begin(); E != Args.end(); E ++) {
        llvm::Type *ArgType = getType((*E)->getType(), TheParser->getContext());
        ArgTypes.push_back(ArgType);
    }
    llvm::Type *TheRetType = getType(RetType, TheParser->getContext());
    FunctionType *FT = FunctionType::get(TheRetType, ArgTypes, false);
    Function *F = Function::Create(FT, Function::ExternalLinkage, Name, TheParser->getModule());
    unsigned long Idx = 0;
    for (auto &Arg : F->args())
        Arg.setName(Args[Idx++]->getName());
    return F;
}

Function *FunctionAST::codegen() {
    // Transfer ownership of the prototype to the FunctionProtos map, but keep a
    // reference to it for use below.
    auto &P = *Proto;
    TheParser->AddFunctionProtos(move(Proto));
    Function *F = TheParser->getFunction(P.getName());
    if (!F)
        return nullptr;

    if (P.isBinaryOp())
        TheParser->SetBinOpPrecedence(P.getOperatorName(), P.getBinaryPrecedence());

    // Create a new basic block to start insertion into.
    auto BB = BasicBlock::Create(TheParser->getContext(), "entry", F);
    TheParser->getBuilder()->SetInsertPoint(BB);

    // Create a subprogram DIE for this function.
    DIFile *Unit = DBuilder->createFile(SispDbgInfo.TheCU->getFilename(),
                                        SispDbgInfo.TheCU->getDirectory());
    DIScope *FContext = Unit;
    unsigned LineNo = P.getLine();
    unsigned ScopeLine = LineNo;
    DISubprogram *SP =
    DBuilder->createFunction(FContext,
                             P.getName(),
                             StringRef(),
                             Unit,
                             LineNo,
                             CreateFunctionType(F->arg_size(), Unit),
                             ScopeLine,
                             DINode::FlagPrototyped,
                             DISubprogram::SPFlagDefinition);
    F->setSubprogram(SP);

    // Push the current scope.
    SispDbgInfo.LexicalBlocks.push_back(SP);

    // Unset the location for the prologue emission (leading instructions with no
    // location in a function are considered part of the prologue and the debugger
    // will run past them when breaking on a function)
    SispDbgInfo.emitLocation(nullptr);

    unsigned ArgIdx = 0;
    for (auto &Arg : F->args()) {
        auto Alloca = Parser::CreateEntryBlockAlloca(F, Arg.getType(), Arg.getName());

        // Create a debug descriptor for the variable.
        DILocalVariable *D = DBuilder->createParameterVariable(
            SP, Arg.getName(), ++ArgIdx, Unit, LineNo, SispDbgInfo.getDoubleTy(),
            true);

        DBuilder->insertDeclare(Alloca, D, DBuilder->createExpression(),
                                DebugLoc::get(LineNo, 0, SP),
                                TheParser->getBuilder()->GetInsertBlock());

        TheParser->getBuilder()->CreateStore(&Arg, Alloca);
        Body->getScope()->setVal(Arg.getName(), Alloca);
    }

    SispDbgInfo.emitLocation(Body.get());

    if (Value *RetVal = Body->codegen()) {
        // Finish off the function.
        TheParser->getBuilder()->CreateRet(RetVal);

        SispDbgInfo.LexicalBlocks.pop_back();

        // Validate the generated code, checking for consistency.
        verifyFunction(*F);

        if (TheParser->isJITEnabled()) {
            // Run the optimizer on the function.
            TheParser->RunFunction(F);
        }

        return F;
    }

    // Error reading body, remove function.
    F->eraseFromParent();

    if (P.isBinaryOp())
        TheParser->SetBinOpPrecedence(Proto->getOperatorName(), -1);

    SispDbgInfo.LexicalBlocks.pop_back();

    return nullptr;
}
