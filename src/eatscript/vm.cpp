#include "eatsbits/eatscript/vm.hpp"
#include "eatsbits/eatscript/lexer.hpp"
#include "eatsbits/eatscript/parser.hpp"
#include <cmath>
#include <numbers>
#include <iostream>

namespace eatsbits::eatscript {

VM::VM() noexcept {
    params_.fill(0.0f);
}

void VM::setParam(uint32_t id, float value) noexcept {
    if (id < PARAMS_MAX) {
        params_[id] = value;
    }
}

float VM::getParam(uint32_t id) const noexcept {
    if (id < PARAMS_MAX) {
        return params_[id];
    }
    return 0.0f;
}

bool VM::compileSource(const std::string& source) {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto prog = parser.parse();
    if (!prog) return false;
    return compileProgram(*prog);
}

bool VM::compileProgram(const Program& program) {
    hasProcess_ = false;
    processChunk_ = Chunk{};
    localSymbolTable_.clear();

    for (const auto& stmt : program.statements) {
        auto* func = ast_cast<FunctionDef>(stmt.get());
        if (func && func->name == "process") {
            if (compileFunction(func)) {
                hasProcess_ = true;
                return true;
            }
        }
    }
    return false;
}

int VM::resolveLocal(const std::string& name) {
    for (size_t i = 0; i < localSymbolTable_.size(); ++i) {
        if (localSymbolTable_[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int VM::addLocal(const std::string& name) {
    int existing = resolveLocal(name);
    if (existing != -1) return existing;
    localSymbolTable_.push_back(name);
    return static_cast<int>(localSymbolTable_.size() - 1);
}

bool VM::compileFunction(FunctionDef* func) {
    isStereoEffect_ = false;
    slotInL_ = -1;
    slotInR_ = -1;

    // Check if parameters indicate an Audio FX: e.g. input_l, input_r or in_l, in_r
    if (func->params.size() >= 2) {
        std::string p0 = func->params[0];
        std::string p1 = func->params[1];
        if (p0 == "input_l" || p0 == "in_l" || p0 == "l" || p0 == "left" ||
            p1 == "input_r" || p1 == "in_r" || p1 == "r" || p1 == "right") {
            isStereoEffect_ = true;
        }
    }

    for (const auto& p : func->params) {
        int s = addLocal(p);
        if (p == "input_l" || p == "in_l" || p == "l" || p == "left") slotInL_ = s;
        if (p == "input_r" || p == "in_r" || p == "r" || p == "right") slotInR_ = s;
    }

    // Default slots if not named identically
    addLocal("time");
    addLocal("freq");
    addLocal("note");
    int defL = addLocal("input_l");
    int defR = addLocal("input_r");
    addLocal("in_l");
    addLocal("in_r");
    if (slotInL_ == -1) slotInL_ = defL;
    if (slotInR_ == -1) slotInR_ = defR;

    for (const auto& stmt : func->body) {
        compileStmt(stmt.get());
    }

    // Ensure a return exists
    processChunk_.emitOp(OpCode::Return);
    return true;
}

void VM::compileStmt(Stmt* stmt) {
    if (!stmt) return;

    if (auto* r = ast_cast<ReturnStmt>(stmt)) {
        if (auto* list = ast_cast<ListLiteral>(r->value.get())) {
            if (list->elements.size() >= 2) {
                compileExpr(list->elements[0].get());
                compileExpr(list->elements[1].get());
                processChunk_.emitOp(OpCode::Return);
                return;
            } else if (!list->elements.empty()) {
                compileExpr(list->elements[0].get());
                processChunk_.emitOp(OpCode::Return);
                return;
            }
        }
        if (r->value) {
            compileExpr(r->value.get());
        } else {
            size_t idx = processChunk_.addConstant(0.0);
            processChunk_.emitOp(OpCode::Constant);
            processChunk_.emitByte(static_cast<uint8_t>(idx));
        }
        processChunk_.emitOp(OpCode::Return);
    } else if (auto* a = ast_cast<AssignStmt>(stmt)) {
        compileExpr(a->value.get());
        int slot = addLocal(a->variableName);
        processChunk_.emitOp(OpCode::SetLocal);
        processChunk_.emitByte(static_cast<uint8_t>(slot));
    } else if (auto* ia = ast_cast<IndexAssignStmt>(stmt)) {
        compileExpr(ia->value.get());
        int slot = addLocal(ia->variableName);
        processChunk_.emitOp(OpCode::SetLocal);
        processChunk_.emitByte(static_cast<uint8_t>(slot));
    } else if (auto* e = ast_cast<ExprStmt>(stmt)) {
        compileExpr(e->expression.get());
    } else if (auto* ifs = ast_cast<IfStmt>(stmt)) {
        compileExpr(ifs->condition.get());
        processChunk_.emitOp(OpCode::JumpIfFalse);
        size_t jumpPos = processChunk_.code.size();
        processChunk_.emitByte(0); // placeholder
        processChunk_.emitByte(0);

        for (const auto& s : ifs->thenBranch) {
            compileStmt(s.get());
        }

        processChunk_.emitOp(OpCode::Jump);
        size_t exitJumpPos = processChunk_.code.size();
        processChunk_.emitByte(0);
        processChunk_.emitByte(0);

        // Patch condition jump
        size_t elseStart = processChunk_.code.size();
        processChunk_.code[jumpPos] = static_cast<uint8_t>((elseStart >> 8) & 0xFF);
        processChunk_.code[jumpPos + 1] = static_cast<uint8_t>(elseStart & 0xFF);

        for (const auto& s : ifs->elseBranch) {
            compileStmt(s.get());
        }

        // Patch exit jump
        size_t afterElse = processChunk_.code.size();
        processChunk_.code[exitJumpPos] = static_cast<uint8_t>((afterElse >> 8) & 0xFF);
        processChunk_.code[exitJumpPos + 1] = static_cast<uint8_t>(afterElse & 0xFF);
    }
}

void VM::compileExpr(Expr* expr) {
    if (!expr) return;

    if (auto* num = ast_cast<NumberLiteral>(expr)) {
        size_t idx = processChunk_.addConstant(num->value);
        processChunk_.emitOp(OpCode::Constant);
        processChunk_.emitByte(static_cast<uint8_t>(idx));
    } else if (auto* b = ast_cast<BooleanLiteral>(expr)) {
        size_t idx = processChunk_.addConstant(b->value ? 1.0 : 0.0);
        processChunk_.emitOp(OpCode::Constant);
        processChunk_.emitByte(static_cast<uint8_t>(idx));
    } else if (auto* id = ast_cast<IdentifierExpr>(expr)) {
        int slot = resolveLocal(id->name);
        if (slot != -1) {
            processChunk_.emitOp(OpCode::GetLocal);
            processChunk_.emitByte(static_cast<uint8_t>(slot));
        } else {
            // Check known constants
            if (id->name == "pi") {
                size_t idx = processChunk_.addConstant(std::numbers::pi_v<double>);
                processChunk_.emitOp(OpCode::Constant);
                processChunk_.emitByte(static_cast<uint8_t>(idx));
            } else {
                size_t idx = processChunk_.addConstant(0.0);
                processChunk_.emitOp(OpCode::Constant);
                processChunk_.emitByte(static_cast<uint8_t>(idx));
            }
        }
    } else if (auto* mem = ast_cast<MemberExpr>(expr)) {
        // e.g. math.pi
        auto* baseId = ast_cast<IdentifierExpr>(mem->object.get());
        if (baseId && baseId->name == "math" && mem->member == "pi") {
            size_t idx = processChunk_.addConstant(std::numbers::pi_v<double>);
            processChunk_.emitOp(OpCode::Constant);
            processChunk_.emitByte(static_cast<uint8_t>(idx));
        } else {
            size_t idx = processChunk_.addConstant(0.0);
            processChunk_.emitOp(OpCode::Constant);
            processChunk_.emitByte(static_cast<uint8_t>(idx));
        }
    } else if (auto* ie = ast_cast<IndexExpr>(expr)) {
        compileExpr(ie->object.get());
    } else if (auto* bin = ast_cast<BinaryExpr>(expr)) {
        compileExpr(bin->left.get());
        compileExpr(bin->right.get());

        switch (bin->op) {
            case TokenType::Plus: processChunk_.emitOp(OpCode::Add); break;
            case TokenType::Minus: processChunk_.emitOp(OpCode::Subtract); break;
            case TokenType::Multiply: processChunk_.emitOp(OpCode::Multiply); break;
            case TokenType::Divide: processChunk_.emitOp(OpCode::Divide); break;
            case TokenType::Modulo: processChunk_.emitOp(OpCode::Modulo); break;
            case TokenType::Power: processChunk_.emitOp(OpCode::Power); break;
            case TokenType::Equals: processChunk_.emitOp(OpCode::Equal); break;
            case TokenType::NotEquals: processChunk_.emitOp(OpCode::NotEqual); break;
            case TokenType::Less: processChunk_.emitOp(OpCode::Less); break;
            case TokenType::LessEqual: processChunk_.emitOp(OpCode::LessEqual); break;
            case TokenType::Greater: processChunk_.emitOp(OpCode::Greater); break;
            case TokenType::GreaterEqual: processChunk_.emitOp(OpCode::GreaterEqual); break;
            default: break;
        }
    } else if (auto* un = ast_cast<UnaryExpr>(expr)) {
        compileExpr(un->operand.get());
        if (un->op == TokenType::Minus) {
            processChunk_.emitOp(OpCode::Negate);
        }
    } else if (auto* call = ast_cast<CallExpr>(expr)) {
        // Identify math functions: math.sin, math.cos, math.tanh, etc.
        auto* callMem = ast_cast<MemberExpr>(call->callee.get());
        if (callMem) {
            auto* objId = ast_cast<IdentifierExpr>(callMem->object.get());
            if (objId && objId->name == "math") {
                if (!call->args.empty()) {
                    compileExpr(call->args[0].get());
                }
                if (callMem->member == "sin") processChunk_.emitOp(OpCode::MathSin);
                else if (callMem->member == "cos") processChunk_.emitOp(OpCode::MathCos);
                else if (callMem->member == "tanh") processChunk_.emitOp(OpCode::MathTanh);
                else if (callMem->member == "exp") processChunk_.emitOp(OpCode::MathExp);
                else if (callMem->member == "floor") processChunk_.emitOp(OpCode::MathFloor);
                else if (callMem->member == "sqrt") processChunk_.emitOp(OpCode::MathSqrt);
                else if (callMem->member == "random") processChunk_.emitOp(OpCode::MathRandom);
                return;
            } else if (objId && objId->name == "params" && callMem->member == "get") {
                // params.get("ParamName", default)
                uint8_t paramIdx = 0;
                if (!call->args.empty()) {
                    auto* strLit = ast_cast<StringLiteral>(call->args[0].get());
                    if (strLit) {
                        if (strLit->value == "Cutoff") paramIdx = 0;
                        else if (strLit->value == "Resonance") paramIdx = 1;
                        else if (strLit->value == "Volume") paramIdx = 2;
                    }
                }
                processChunk_.emitOp(OpCode::GetParam);
                processChunk_.emitByte(paramIdx);
                return;
            }
        }

        // Check direct function call (e.g. sin(...), cos(...), tanh(...))
        auto* fnId = ast_cast<IdentifierExpr>(call->callee.get());
        if (fnId) {
            if (!call->args.empty()) {
                compileExpr(call->args[0].get());
            }
            if (fnId->name == "sin") { processChunk_.emitOp(OpCode::MathSin); return; }
            if (fnId->name == "cos") { processChunk_.emitOp(OpCode::MathCos); return; }
            if (fnId->name == "tanh") { processChunk_.emitOp(OpCode::MathTanh); return; }
            if (fnId->name == "exp") { processChunk_.emitOp(OpCode::MathExp); return; }
            if (fnId->name == "floor") { processChunk_.emitOp(OpCode::MathFloor); return; }
            if (fnId->name == "sqrt") { processChunk_.emitOp(OpCode::MathSqrt); return; }
            if (fnId->name == "random") { processChunk_.emitOp(OpCode::MathRandom); return; }
            if (fnId->name == "param") {
                uint8_t paramIdx = 0;
                if (!call->args.empty()) {
                    auto* numLit = ast_cast<NumberLiteral>(call->args[0].get());
                    if (numLit) paramIdx = static_cast<uint8_t>(numLit->value);
                }
                processChunk_.emitOp(OpCode::GetParam);
                processChunk_.emitByte(paramIdx);
                return;
            }
        }

        // Fallback: compile single argument if available
        if (!call->args.empty()) {
            compileExpr(call->args[0].get());
        } else {
            size_t idx = processChunk_.addConstant(0.0);
            processChunk_.emitOp(OpCode::Constant);
            processChunk_.emitByte(static_cast<uint8_t>(idx));
        }
    } else if (auto* list = ast_cast<ListLiteral>(expr)) {
        // For audio FX returning [l, r], compile first element for mono/left
        if (!list->elements.empty()) {
            compileExpr(list->elements[0].get());
        } else {
            size_t idx = processChunk_.addConstant(0.0);
            processChunk_.emitOp(OpCode::Constant);
            processChunk_.emitByte(static_cast<uint8_t>(idx));
        }
    }
}

double VM::executeProcess(double time, double freq, double note, const float* params, size_t numParams) noexcept {
    if (!hasProcess_ || processChunk_.code.empty()) {
        return 0.0;
    }

    // Set standard local variables
    locals_[0] = time;
    locals_[1] = freq;
    locals_[2] = note;

    // Load any input parameter array into cache
    if (params && numParams > 0) {
        const size_t count = std::min(numParams, PARAMS_MAX);
        for (size_t i = 0; i < count; ++i) {
            params_[i] = params[i];
        }
    }

    size_t ip = 0;
    size_t sp = 0;
    const uint8_t* code = processChunk_.code.data();
    const size_t codeSize = processChunk_.code.size();
    const double* constants = processChunk_.constants.data();

    while (ip < codeSize) {
        auto op = static_cast<OpCode>(code[ip++]);
        switch (op) {
            case OpCode::Constant: {
                uint8_t idx = code[ip++];
                stack_[sp++] = constants[idx];
                break;
            }
            case OpCode::GetLocal: {
                uint8_t slot = code[ip++];
                stack_[sp++] = locals_[slot];
                break;
            }
            case OpCode::SetLocal: {
                uint8_t slot = code[ip++];
                locals_[slot] = stack_[--sp];
                break;
            }
            case OpCode::GetParam: {
                uint8_t idx = code[ip++];
                stack_[sp++] = static_cast<double>(params_[idx]);
                break;
            }
            case OpCode::Add: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = a + b;
                break;
            }
            case OpCode::Subtract: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = a - b;
                break;
            }
            case OpCode::Multiply: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = a * b;
                break;
            }
            case OpCode::Divide: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (std::abs(b) > 1e-12) ? (a / b) : 0.0;
                break;
            }
            case OpCode::Modulo: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (std::abs(b) > 1e-12) ? std::fmod(a, b) : 0.0;
                break;
            }
            case OpCode::Power: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = std::pow(a, b);
                break;
            }
            case OpCode::Negate: {
                stack_[sp - 1] = -stack_[sp - 1];
                break;
            }
            case OpCode::Equal: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a == b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::NotEqual: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a != b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::Less: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a < b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::LessEqual: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a <= b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::Greater: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a > b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::GreaterEqual: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a >= b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::MathSin: {
                stack_[sp - 1] = std::sin(stack_[sp - 1]);
                break;
            }
            case OpCode::MathCos: {
                stack_[sp - 1] = std::cos(stack_[sp - 1]);
                break;
            }
            case OpCode::MathTanh: {
                stack_[sp - 1] = std::tanh(stack_[sp - 1]);
                break;
            }
            case OpCode::MathExp: {
                stack_[sp - 1] = std::exp(stack_[sp - 1]);
                break;
            }
            case OpCode::MathFloor: {
                stack_[sp - 1] = std::floor(stack_[sp - 1]);
                break;
            }
            case OpCode::MathSqrt: {
                stack_[sp - 1] = std::sqrt(std::max(0.0, stack_[sp - 1]));
                break;
            }
            case OpCode::MathRandom: {
                rngState_ = rngState_ * 1664525u + 1013904223u;
                stack_[sp++] = static_cast<double>(rngState_) / 4294967296.0;
                break;
            }
            case OpCode::Jump: {
                uint16_t offset = (static_cast<uint16_t>(code[ip]) << 8) | code[ip + 1];
                ip = offset;
                break;
            }
            case OpCode::JumpIfFalse: {
                uint16_t offset = (static_cast<uint16_t>(code[ip]) << 8) | code[ip + 1];
                ip += 2;
                double cond = stack_[--sp];
                if (std::abs(cond) < 1e-12) {
                    ip = offset;
                }
                break;
            }
            case OpCode::Return: {
                if (sp > 0) {
                    return stack_[--sp];
                }
                return 0.0;
            }
        }
    }

    return (sp > 0) ? stack_[--sp] : 0.0;
}

std::pair<double, double> VM::executeStereoProcess(double inL, double inR, const float* params, size_t numParams) noexcept {
    if (!hasProcess_ || processChunk_.code.empty()) {
        return {inL, inR};
    }

    // Set stereo input locals
    if (slotInL_ >= 0 && slotInL_ < static_cast<int>(LOCALS_MAX)) {
        locals_[slotInL_] = inL;
    }
    if (slotInR_ >= 0 && slotInR_ < static_cast<int>(LOCALS_MAX)) {
        locals_[slotInR_] = inR;
    }

    int sInL = resolveLocal("in_l");
    if (sInL >= 0 && sInL < static_cast<int>(LOCALS_MAX)) locals_[sInL] = inL;
    int sInR = resolveLocal("in_r");
    if (sInR >= 0 && sInR < static_cast<int>(LOCALS_MAX)) locals_[sInR] = inR;

    // Load parameters
    if (params && numParams > 0) {
        const size_t count = std::min(numParams, PARAMS_MAX);
        for (size_t i = 0; i < count; ++i) {
            params_[i] = params[i];
        }
    }

    size_t ip = 0;
    size_t sp = 0;
    const uint8_t* code = processChunk_.code.data();
    const size_t codeSize = processChunk_.code.size();
    const double* constants = processChunk_.constants.data();

    while (ip < codeSize) {
        auto op = static_cast<OpCode>(code[ip++]);
        switch (op) {
            case OpCode::Constant: {
                uint8_t idx = code[ip++];
                stack_[sp++] = constants[idx];
                break;
            }
            case OpCode::GetLocal: {
                uint8_t slot = code[ip++];
                stack_[sp++] = locals_[slot];
                break;
            }
            case OpCode::SetLocal: {
                uint8_t slot = code[ip++];
                locals_[slot] = stack_[--sp];
                break;
            }
            case OpCode::GetParam: {
                uint8_t idx = code[ip++];
                stack_[sp++] = static_cast<double>(params_[idx]);
                break;
            }
            case OpCode::Add: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = a + b;
                break;
            }
            case OpCode::Subtract: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = a - b;
                break;
            }
            case OpCode::Multiply: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = a * b;
                break;
            }
            case OpCode::Divide: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (std::abs(b) > 1e-12) ? (a / b) : 0.0;
                break;
            }
            case OpCode::Modulo: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (std::abs(b) > 1e-12) ? std::fmod(a, b) : 0.0;
                break;
            }
            case OpCode::Power: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = std::pow(a, b);
                break;
            }
            case OpCode::Negate: {
                stack_[sp - 1] = -stack_[sp - 1];
                break;
            }
            case OpCode::Equal: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (std::abs(a - b) < 1e-9) ? 1.0 : 0.0;
                break;
            }
            case OpCode::NotEqual: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (std::abs(a - b) >= 1e-9) ? 1.0 : 0.0;
                break;
            }
            case OpCode::Less: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a < b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::LessEqual: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a <= b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::Greater: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a > b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::GreaterEqual: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a >= b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::MathSin: {
                stack_[sp - 1] = std::sin(stack_[sp - 1]);
                break;
            }
            case OpCode::MathCos: {
                stack_[sp - 1] = std::cos(stack_[sp - 1]);
                break;
            }
            case OpCode::MathTanh: {
                stack_[sp - 1] = std::tanh(stack_[sp - 1]);
                break;
            }
            case OpCode::MathExp: {
                stack_[sp - 1] = std::exp(stack_[sp - 1]);
                break;
            }
            case OpCode::MathFloor: {
                stack_[sp - 1] = std::floor(stack_[sp - 1]);
                break;
            }
            case OpCode::MathSqrt: {
                stack_[sp - 1] = std::sqrt(std::max(0.0, stack_[sp - 1]));
                break;
            }
            case OpCode::MathRandom: {
                rngState_ = rngState_ * 1664525u + 1013904223u;
                stack_[sp++] = static_cast<double>(rngState_) / 4294967296.0;
                break;
            }
            case OpCode::Jump: {
                uint16_t offset = (static_cast<uint16_t>(code[ip]) << 8) | code[ip + 1];
                ip = offset;
                break;
            }
            case OpCode::JumpIfFalse: {
                uint16_t offset = (static_cast<uint16_t>(code[ip]) << 8) | code[ip + 1];
                ip += 2;
                double cond = stack_[--sp];
                if (std::abs(cond) < 1e-12) {
                    ip = offset;
                }
                break;
            }
            case OpCode::Return: {
                if (sp >= 2) {
                    double r = stack_[--sp];
                    double l = stack_[--sp];
                    return {l, r};
                } else if (sp == 1) {
                    double val = stack_[--sp];
                    return {val, val};
                }
                return {0.0, 0.0};
            }
        }
    }

    if (sp >= 2) {
        double r = stack_[--sp];
        double l = stack_[--sp];
        return {l, r};
    } else if (sp == 1) {
        double val = stack_[--sp];
        return {val, val};
    }
    return {0.0, 0.0};
}

} // namespace eatsbits::eatscript
