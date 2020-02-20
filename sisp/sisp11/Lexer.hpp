//
//  Lexer.hpp
//  sisp
//
//  Created by 徐可 on 2020/2/19.
//  Copyright © 2020 Beibei Inc. All rights reserved.
//

#ifndef Lexer_hpp
#define Lexer_hpp

#include <string>

using namespace std;

typedef enum Token {
    tok_eof = -1,
    tok_def = -2,
    tok_extern = -3,
    tok_identifier = -4,
    tok_number = -5,
    tok_if = -6,
    tok_then = -7,
    tok_else = -8,
    tok_for = -9,
    tok_in = -10,
    tok_binary = -11,
    tok_unary = -12,
    tok_var = -13,

    tok_left_paren = '(',
    tok_right_paren = ')',
    tok_equal = '=',
    tok_less = '<',
    tok_great = '>',
    tok_comma = ',',
    tok_colon = ';',
    tok_hash = '#',
    tok_dot = '.',
    tok_space = ' ',
    tok_left_bracket = '{',
    tok_right_bracket = '}',
    tok_newline = '\n',
    tok_return = '\r',

    tok_add = '+',
    tok_sub = '-',
    tok_mul = '*',
    tok_div = '/',
    tok_not = '!',
    tok_or = '|',
    tok_and = '&',

} Token;

struct SourceLocation {
    int Line;
    int Col;
};
extern SourceLocation CurLoc;
extern SourceLocation LexLoc;

extern Token CurTok;
extern int getNextToken();

extern string IdentifierStr;
extern double NumVal;
extern string TheCode;

#endif /* Lexer_hpp */
