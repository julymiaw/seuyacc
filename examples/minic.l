%option yylineno

digit [0-9]
letter [a-zA-Z]
id {letter}({letter}|{digit})*
num {digit}+
hex 0(x|X)({letter}|[0-9])+


%{
#include "minic.tab.h"
#include <stdlib.h>
extern int Lineno;

int ConvertHexToint(char* str) {
    return (int)strtol(str, NULL, 16);
}
%}

%%

"void"      { return VOID; }
"continue"  { return CONTINUE; }
"if"        { return IF; }
"while"     { return WHILE; }
"else"      { return ELSE; }
"break"     { return BREAK; }
"int"       { return INT; }
"return"    { return RETURN; }
"||"        { return OR; }
"&&"        { return AND; }
{id}        { yylval.str_val=strdup(yytext);return IDENT; }
{hex}       { yylval.str_val=strdup(yytext);return HEXNUM; }
{num}       { yylval.str_val=strdup(yytext);return DECNUM; }
"<="        { return LE; }
">="        { return GE; }
"=="        { return EQ; }
"!="        { return NE; }
">"         { return '>'; }
"<"         { return '<'; }
","         { return ','; }
";"         { return ';'; }
"{"         { return '{'; }
"}"         { return '}'; }
"%"         { return '%'; }
"*"         { return '*';}
"+"         { return '+'; }
"-"         { return '-'; }
"/"         { return '/'; }
"="         { return '='; }
"("         { return '('; }
")"         { return ')'; }
"~"         { return '~'; }
"&"         { return '&'; }
"^"         { return '^'; }
"["         { return '['; }
"]"         { return ']'; }
"<<"        { return LSHIFT; }
">>"        { return RSHIFT; }
"|"         { return '|'; }
[ \t\v\r\n] { }
"$"         { return '$'; }

%%

int yywrap() {
    return 1;
}