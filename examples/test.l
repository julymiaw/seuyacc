%{
#include "test.tab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int line_number = 1;
int column = 0;
%}

%option noyywrap

DIGIT       [0-9]
NONZERO     [1-9]
LETTER      [a-zA-Z_]
IDENTIFIER  {LETTER}({LETTER}|{DIGIT})*
INTEGER     {NONZERO}{DIGIT}*|0
WHITESPACE  [ \t\r\f]+
NEWLINE     [\n]
STRING      \"([^\"\n]|\\\")*\"

%%
{WHITESPACE}  { column += yyleng; /* 忽略空白符 */ }
{NEWLINE}     { line_number++; column = 0; }
"int"         { column += yyleng; printf("第%d行: 识别到关键字: INT\n", line_number); return INT; }
"char"        { column += yyleng; printf("第%d行: 识别到关键字: CHAR\n", line_number); return CHAR; }
"void"        { column += yyleng; printf("第%d行: 识别到关键字: VOID\n", line_number); return VOID; }
"main"        { column += yyleng; printf("第%d行: 识别到关键字: MAIN\n", line_number); return MAIN; }
"printf"      { column += yyleng; printf("第%d行: 识别到关键字: PRINTF\n", line_number); return PRINTF; }
"return"      { column += yyleng; printf("第%d行: 识别到关键字: RETURN\n", line_number); return RETURN; }
{IDENTIFIER}  { column += yyleng; printf("第%d行: 识别到标识符: %s\n", line_number, yytext); yylval.sval = strdup(yytext); return IDENTIFIER; }
{INTEGER}     { column += yyleng; printf("第%d行: 识别到整数: %s\n", line_number, yytext); yylval.ival = atoi(yytext); return INTEGER_LITERAL; }
{STRING}      { column += yyleng; printf("第%d行: 识别到字符串: %s\n", line_number, yytext); yylval.sval = strdup(yytext); return STRING_LITERAL; }
"+"           { column += yyleng; printf("第%d行: 识别到操作符: +\n", line_number); return '+'; }
"-"           { column += yyleng; printf("第%d行: 识别到操作符: -\n", line_number); return '-'; }
"*"           { column += yyleng; printf("第%d行: 识别到操作符: *\n", line_number); return '*'; }
"/"           { column += yyleng; printf("第%d行: 识别到操作符: /\n", line_number); return '/'; }
"="           { column += yyleng; printf("第%d行: 识别到操作符: =\n", line_number); return '='; }
";"           { column += yyleng; printf("第%d行: 识别到分号\n", line_number); return SEMICOLON; }
"("           { column += yyleng; printf("第%d行: 识别到左括号\n", line_number); return '('; }
")"           { column += yyleng; printf("第%d行: 识别到右括号\n", line_number); return ')'; }
"{"           { column += yyleng; printf("第%d行: 识别到左大括号\n", line_number); return LBRACE; }
"}"           { column += yyleng; printf("第%d行: 识别到右大括号\n", line_number); return RBRACE; }
","           { column += yyleng; printf("第%d行: 识别到逗号\n", line_number); return ','; }
.             { column += yyleng; fprintf(stderr, "第%d行: 错误: 无效字符 '%s'\n", line_number, yytext); }
%%