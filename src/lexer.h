#pragma once

#include <string>
#include <vector>

enum class TokenKind {
    End,
    Identifier,
    Number,
    Newline,
    LBrace,
    RBrace,
    LParen,
    RParen,
    Comma,
    Equal,
    Plus,
    KwSet,
    KwCir,
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string lexeme;
    double number_value = 0.0;
    int line = 1;
    int column = 1;
};

class Lexer {
public:
    explicit Lexer(const std::string &source);

    std::vector<Token> tokenize();

private:
    char peek() const;
    char advance();
    bool match(char expected);
    void skip_inline_whitespace();
    Token make_identifier_or_keyword();
    Token make_number();

    const std::string source_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;
};
