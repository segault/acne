#include "parser.h"

#include <stdexcept>

namespace {
std::runtime_error parse_error(const Token &token, const std::string &message) {
    return std::runtime_error(
        "Parse error at " + std::to_string(token.line) + ":" +
        std::to_string(token.column) + ": " + message);
}
}

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

ProgramAst Parser::parse_program() {
    ProgramAst program;

    skip_newlines();
    while (!is_at_end()) {
        if (match(TokenKind::KwSet)) {
            const Token &mode = consume(TokenKind::Identifier, "Expected source mode after 'set'");
            if (mode.lexeme == "ac") {
                program.has_source_mode = true;
                program.source_mode = SourceMode::Ac;
            } else if (mode.lexeme == "dc") {
                program.has_source_mode = true;
                program.source_mode = SourceMode::Dc;
            } else {
                throw parse_error(mode, "Unknown source mode '" + mode.lexeme + "'");
            }
            skip_newlines();
            continue;
        }

        if (match(TokenKind::KwCir)) {
            program.circuits.push_back(parse_circuit());
            skip_newlines();
            continue;
        }

        AssignAst assign = parse_assignment();
        if (assign.lhs == "in" && assign.rhs.kind == ExprKind::Number) {
            program.has_input_value = true;
            program.input_value = assign.rhs.number_value;
        } else {
            program.statements.push_back(std::move(assign));
        }
        skip_newlines();
    }

    return program;
}

const Token &Parser::peek() const {
    return tokens_[current_];
}

const Token &Parser::previous() const {
    return tokens_[current_ - 1];
}

bool Parser::is_at_end() const {
    return peek().kind == TokenKind::End;
}

bool Parser::check(TokenKind kind) const {
    if (is_at_end()) {
        return kind == TokenKind::End;
    }
    return peek().kind == kind;
}

bool Parser::match(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }
    advance();
    return true;
}

const Token &Parser::advance() {
    if (!is_at_end()) {
        ++current_;
    }
    return previous();
}

const Token &Parser::consume(TokenKind kind, const char *message) {
    if (check(kind)) {
        return advance();
    }
    throw parse_error(peek(), message);
}

void Parser::skip_newlines() {
    while (match(TokenKind::Newline)) {}
}

CircuitAst Parser::parse_circuit() {
    CircuitAst circuit;
    circuit.name = consume(TokenKind::Identifier, "Expected circuit name").lexeme;
    consume(TokenKind::LBrace, "Expected '{' after circuit name");
    skip_newlines();

    while (!check(TokenKind::RBrace)) {
        circuit.body.push_back(parse_assignment());
        skip_newlines();
    }

    consume(TokenKind::RBrace, "Expected '}' after circuit body");
    return circuit;
}

AssignAst Parser::parse_assignment() {
    AssignAst assign;
    assign.lhs = consume(TokenKind::Identifier, "Expected assignment target").lexeme;
    consume(TokenKind::Equal, "Expected '=' after assignment target");
    assign.rhs = parse_expression();
    return assign;
}

Expr Parser::parse_expression() {
    Expr expr = parse_term();

    while (match(TokenKind::Plus)) {
        Expr rhs = parse_term();
        if (expr.kind != ExprKind::Parallel) {
            Expr combined;
            combined.kind = ExprKind::Parallel;
            combined.args.push_back(std::move(expr));
            combined.args.push_back(std::move(rhs));
            expr = std::move(combined);
        } else {
            expr.args.push_back(std::move(rhs));
        }
    }

    return expr;
}

Expr Parser::parse_term() {
    if (match(TokenKind::Number)) {
        Expr expr;
        expr.kind = ExprKind::Number;
        expr.number_value = previous().number_value;
        return expr;
    }

    if (match(TokenKind::Identifier)) {
        const Token &name = previous();
        if (match(TokenKind::LParen)) {
            return parse_call(name);
        }

        Expr expr;
        expr.kind = ExprKind::Identifier;
        expr.text = name.lexeme;
        return expr;
    }

    throw parse_error(peek(), "Expected expression");
}

Expr Parser::parse_call(const Token &name) {
    Expr expr;
    expr.kind = ExprKind::Call;
    expr.text = name.lexeme;

    if (!check(TokenKind::RParen)) {
        do {
            expr.args.push_back(parse_expression());
        } while (match(TokenKind::Comma));
    }

    consume(TokenKind::RParen, "Expected ')' after call arguments");
    return expr;
}
