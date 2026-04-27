#pragma once

#include <string>
#include <vector>

enum class SourceMode {
    Dc,
    Ac,
};

enum class ExprKind {
    Number,
    Identifier,
    Call,
    Parallel,
};

struct Expr {
    ExprKind kind = ExprKind::Identifier;
    double number_value = 0.0;
    std::string text;
    std::vector<Expr> args;
};

struct AssignAst {
    std::string lhs;
    Expr rhs;
};

struct CircuitAst {
    std::string name;
    std::vector<AssignAst> body;
};

struct ProgramAst {
    bool has_source_mode = false;
    SourceMode source_mode = SourceMode::Dc;
    bool has_input_value = false;
    double input_value = 0.0;
    std::vector<CircuitAst> circuits;
    std::vector<AssignAst> statements;
};
