#pragma once

#include "ast.h"
#include "lexer.h"

#include <vector>

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    ProgramAst parse_program();

private:
    const Token &peek() const;
    const Token &previous() const;
    bool is_at_end() const;
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    const Token &advance();
    const Token &consume(TokenKind kind, const char *message);
    void skip_newlines();

    CircuitAst parse_circuit();
    AssignAst parse_assignment();
    Expr parse_expression();
    Expr parse_term();
    Expr parse_call(const Token &name);
    bool is_component_name(const std::string &name) const;

    std::vector<Token> tokens_;
    std::size_t current_ = 0;
};
