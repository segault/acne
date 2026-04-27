#include "lexer.h"

#include <cctype>
#include <stdexcept>

Lexer::Lexer(const std::string &source) : source_(source) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        skip_inline_whitespace();
        const char c = peek();
        if (c == '\0') {
            tokens.push_back({TokenKind::End, "", 0.0, line_, column_});
            return tokens;
        }

        if (c == '\n') {
            const int line = line_;
            const int column = column_;
            advance();
            tokens.push_back({TokenKind::Newline, "\\n", 0.0, line, column});
            continue;
        }

        if (c == '#') {
            while (peek() != '\0' && peek() != '\n') {
                advance();
            }
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            tokens.push_back(make_identifier_or_keyword());
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            tokens.push_back(make_number());
            continue;
        }

        const int line = line_;
        const int column = column_;
        advance();
        switch (c) {
            case '{':
                tokens.push_back({TokenKind::LBrace, "{", 0.0, line, column});
                break;
            case '}':
                tokens.push_back({TokenKind::RBrace, "}", 0.0, line, column});
                break;
            case '(':
                tokens.push_back({TokenKind::LParen, "(", 0.0, line, column});
                break;
            case ')':
                tokens.push_back({TokenKind::RParen, ")", 0.0, line, column});
                break;
            case ',':
                tokens.push_back({TokenKind::Comma, ",", 0.0, line, column});
                break;
            case '=':
                tokens.push_back({TokenKind::Equal, "=", 0.0, line, column});
                break;
            case '+':
                tokens.push_back({TokenKind::Plus, "+", 0.0, line, column});
                break;
            case ';':
                tokens.push_back({TokenKind::Newline, ";", 0.0, line, column});
                break;
            default:
                throw std::runtime_error(
                    "Unexpected character '" + std::string(1, c) + "' at " +
                    std::to_string(line) + ":" + std::to_string(column));
        }
    }
}

char Lexer::peek() const {
    if (pos_ >= source_.size()) {
        return '\0';
    }
    return source_[pos_];
}

char Lexer::advance() {
    if (pos_ >= source_.size()) {
        return '\0';
    }

    const char c = source_[pos_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (peek() != expected) {
        return false;
    }
    advance();
    return true;
}

void Lexer::skip_inline_whitespace() {
    while (true) {
        const char c = peek();
        if (c == ' ' || c == '\t' || c == '\r') {
            advance();
            continue;
        }
        return;
    }
}

Token Lexer::make_identifier_or_keyword() {
    const int line = line_;
    const int column = column_;
    std::string lexeme;
    while (true) {
        const char c = peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            lexeme.push_back(advance());
            continue;
        }
        break;
    }

    TokenKind kind = TokenKind::Identifier;
    if (lexeme == "set") {
        kind = TokenKind::KwSet;
    } else if (lexeme == "cir") {
        kind = TokenKind::KwCir;
    }

    return {kind, lexeme, 0.0, line, column};
}

Token Lexer::make_number() {
    const int line = line_;
    const int column = column_;
    std::string lexeme;
    bool seen_dot = false;

    if (peek() == '.') {
        seen_dot = true;
        lexeme.push_back(advance());
    }

    while (true) {
        const char c = peek();
        if (std::isdigit(static_cast<unsigned char>(c))) {
            lexeme.push_back(advance());
            continue;
        }
        if (c == '.' && !seen_dot) {
            seen_dot = true;
            lexeme.push_back(advance());
            continue;
        }
        break;
    }

    if (lexeme == ".") {
        throw std::runtime_error(
            "Invalid number '.' at " + std::to_string(line) + ":" +
            std::to_string(column));
    }

    return {TokenKind::Number, lexeme, std::stod(lexeme), line, column};
}
