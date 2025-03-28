%token INTEGER_LITERAL
%token IDENTIFIER STRING_LITERAL
%token INT CHAR VOID MAIN PRINTF RETURN
%token ADD SUB MUL DIV ASSIGN SEMICOLON LPAREN RPAREN LBRACE RBRACE COMMA

%start program

%%

program
    : main_function
    ;

main_function
    : type_specifier MAIN LPAREN RPAREN compound_statement
    ;

type_specifier
    : INT
    | CHAR
    | VOID
    ;

compound_statement
    : LBRACE opt_declaration_list opt_statement_list RBRACE
    ;

opt_declaration_list
    : declaration_list
    ;

opt_statement_list
    : statement_list
    ;

declaration_list
    : declaration
    | declaration_list declaration
    ;

declaration
    : type_specifier init_declarator_list SEMICOLON
    ;

init_declarator_list
    : init_declarator
    | init_declarator_list COMMA init_declarator
    ;

init_declarator
    : IDENTIFIER
    | IDENTIFIER ASSIGN initializer
    ;

initializer
    : expression
    ;

statement_list
    : statement
    | statement_list statement
    ;

statement
    : expression_statement
    | compound_statement
    | printf_statement
    | return_statement
    ;

expression_statement
    : expression SEMICOLON
    | SEMICOLON
    ;

printf_statement
    : PRINTF LPAREN STRING_LITERAL COMMA expression RPAREN SEMICOLON
    | PRINTF LPAREN STRING_LITERAL RPAREN SEMICOLON
    ;

return_statement
    : RETURN expression SEMICOLON
    | RETURN SEMICOLON
    ;

expression
    : IDENTIFIER
    | INTEGER_LITERAL
    | expression ADD expression
    | expression SUB expression
    | expression MUL expression
    | expression DIV expression
    | IDENTIFIER ASSIGN expression
    | LPAREN expression RPAREN
    ;

%%
#include <stdio.h>

extern char yytext[];
extern int column;

void yyerror(char const *s)
{
    fflush(stdout);
    printf("\n%*s\n%*s\n", column, "^", column, s);
}