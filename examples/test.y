%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 定义抽象语法树节点类型 */
typedef enum {
    AST_PROGRAM,
    AST_MAIN_FUNC,
    AST_TYPE_SPEC,
    AST_COMPOUND_STMT,
    AST_DECL_LIST,
    AST_DECL,
    AST_INIT_LIST,
    AST_INIT,
    AST_STMT_LIST,
    AST_STMT,
    AST_EXPR_STMT,
    AST_PRINTF_STMT,
    AST_RETURN_STMT,
    AST_EXPR,
    AST_BINARY_EXPR,
    AST_ASSIGN_EXPR,
    AST_ID,
    AST_INT_LITERAL
} NodeType;

/* 抽象语法树节点 */
typedef struct ASTNode {
    NodeType type;
    char* value;
    struct ASTNode* child;    /* 第一个子节点 */
    struct ASTNode* sibling;  /* 兄弟节点 */
} ASTNode;

/* 全局变量 */
ASTNode* ast_root = NULL;
int node_count = 0;

/* 创建新节点 */
ASTNode* create_node(NodeType type, char* value) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    node->type = type;
    node->value = value ? strdup(value) : NULL;
    node->child = NULL;
    node->sibling = NULL;
    node_count++;
    return node;
}

/* 添加子节点 */
void add_child(ASTNode* parent, ASTNode* child) {
    if (!parent || !child) return;
    
    if (parent->child == NULL) {
        parent->child = child;
    } else {
        ASTNode* sibling = parent->child;
        while (sibling->sibling != NULL) {
            sibling = sibling->sibling;
        }
        sibling->sibling = child;
    }
}

/* 打印节点类型的字符串表示 */
const char* node_type_str(NodeType type) {
    switch (type) {
        case AST_PROGRAM: return "Program";
        case AST_MAIN_FUNC: return "MainFunction";
        case AST_TYPE_SPEC: return "TypeSpecifier";
        case AST_COMPOUND_STMT: return "CompoundStatement";
        case AST_DECL_LIST: return "DeclarationList";
        case AST_DECL: return "Declaration";
        case AST_INIT_LIST: return "InitDeclaratorList";
        case AST_INIT: return "InitDeclarator";
        case AST_STMT_LIST: return "StatementList";
        case AST_STMT: return "Statement";
        case AST_EXPR_STMT: return "ExpressionStatement";
        case AST_PRINTF_STMT: return "PrintfStatement";
        case AST_RETURN_STMT: return "ReturnStatement";
        case AST_EXPR: return "Expression";
        case AST_BINARY_EXPR: return "BinaryExpression";
        case AST_ASSIGN_EXPR: return "AssignmentExpression";
        case AST_ID: return "Identifier";
        case AST_INT_LITERAL: return "IntegerLiteral";
        default: return "Unknown";
    }
}

/* 打印抽象语法树 */
void print_ast(ASTNode* node, int indent) {
    if (!node) return;
    
    /* 打印缩进 */
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
    
    /* 打印节点信息 */
    printf("%s", node_type_str(node->type));
    if (node->value) {
        printf(" (%s)", node->value);
    }
    printf("\n");
    
    /* 递归打印子节点 */
    print_ast(node->child, indent + 1);
    
    /* 递归打印兄弟节点 */
    print_ast(node->sibling, indent);
}

/* 释放抽象语法树内存 */
void free_ast(ASTNode* node) {
    if (!node) return;
    
    /* 递归释放子节点 */
    if (node->child) {
        free_ast(node->child);
    }
    
    /* 递归释放兄弟节点 */
    if (node->sibling) {
        free_ast(node->sibling);
    }
    
    /* 释放节点本身 */
    if (node->value) {
        free(node->value);
    }
    free(node);
}
%}

%token INTEGER_LITERAL
%token IDENTIFIER STRING_LITERAL
%token INT CHAR VOID MAIN PRINTF RETURN
%token ADD SUB MUL DIV ASSIGN SEMICOLON LPAREN RPAREN LBRACE RBRACE COMMA

%start program

%%

program
    : main_function {
        $$ = create_node(AST_PROGRAM, NULL);
        add_child($$, $1);
        ast_root = $$;
    }
    ;

main_function
    : type_specifier MAIN LPAREN RPAREN compound_statement {
        $$ = create_node(AST_MAIN_FUNC, "main");
        add_child($$, $1);
        add_child($$, $5);
    }
    ;

type_specifier
    : INT {
        $$ = create_node(AST_TYPE_SPEC, "int");
    }
    | CHAR {
        $$ = create_node(AST_TYPE_SPEC, "char");
    }
    | VOID {
        $$ = create_node(AST_TYPE_SPEC, "void");
    }
    ;

compound_statement
    : LBRACE opt_declaration_list opt_statement_list RBRACE {
        $$ = create_node(AST_COMPOUND_STMT, NULL);
        if ($2) add_child($$, $2);
        if ($3) add_child($$, $3);
    }
    ;

opt_declaration_list
    : declaration_list {
        $$ = $1;
    }
    | /* empty */ {
        $$ = NULL;
    }
    ;

opt_statement_list
    : statement_list {
        $$ = $1;
    }
    | /* empty */ {
        $$ = NULL;
    }
    ;

declaration_list
    : declaration {
        $$ = create_node(AST_DECL_LIST, NULL);
        add_child($$, $1);
    }
    | declaration_list declaration {
        $$ = $1;
        add_child($$, $2);
    }
    ;

declaration
    : type_specifier init_declarator_list SEMICOLON {
        $$ = create_node(AST_DECL, NULL);
        add_child($$, $1);
        add_child($$, $2);
    }
    ;

init_declarator_list
    : init_declarator {
        $$ = create_node(AST_INIT_LIST, NULL);
        add_child($$, $1);
    }
    | init_declarator_list COMMA init_declarator {
        $$ = $1;
        add_child($$, $3);
    }
    ;

init_declarator
    : IDENTIFIER {
        $$ = create_node(AST_INIT, NULL);
        add_child($$, create_node(AST_ID, "identifier"));
    }
    | IDENTIFIER ASSIGN initializer {
        $$ = create_node(AST_INIT, NULL);
        add_child($$, create_node(AST_ID, "identifier"));
        add_child($$, $3);
    }
    ;

initializer
    : expression {
        $$ = $1;
    }
    ;

statement_list
    : statement {
        $$ = create_node(AST_STMT_LIST, NULL);
        add_child($$, $1);
    }
    | statement_list statement {
        $$ = $1;
        add_child($$, $2);
    }
    ;

statement
    : expression_statement {
        $$ = create_node(AST_STMT, NULL);
        add_child($$, $1);
    }
    | compound_statement {
        $$ = create_node(AST_STMT, NULL);
        add_child($$, $1);
    }
    | printf_statement {
        $$ = create_node(AST_STMT, NULL);
        add_child($$, $1);
    }
    | return_statement {
        $$ = create_node(AST_STMT, NULL);
        add_child($$, $1);
    }
    ;

expression_statement
    : expression SEMICOLON {
        $$ = create_node(AST_EXPR_STMT, NULL);
        add_child($$, $1);
    }
    | SEMICOLON {
        $$ = create_node(AST_EXPR_STMT, NULL);
    }
    ;

printf_statement
    : PRINTF LPAREN STRING_LITERAL COMMA expression RPAREN SEMICOLON {
        $$ = create_node(AST_PRINTF_STMT, NULL);
        add_child($$, create_node(AST_STRING_LITERAL, "string"));
        add_child($$, $5);
    }
    | PRINTF LPAREN STRING_LITERAL RPAREN SEMICOLON {
        $$ = create_node(AST_PRINTF_STMT, NULL);
        add_child($$, create_node(AST_STRING_LITERAL, "string"));
    }
    ;

return_statement
    : RETURN expression SEMICOLON {
        $$ = create_node(AST_RETURN_STMT, NULL);
        add_child($$, $2);
    }
    | RETURN SEMICOLON {
        $$ = create_node(AST_RETURN_STMT, NULL);
    }
    ;

expression
    : IDENTIFIER {
        $$ = create_node(AST_EXPR, NULL);
        add_child($$, create_node(AST_ID, "identifier"));
    }
    | INTEGER_LITERAL {
        $$ = create_node(AST_EXPR, NULL);
        add_child($$, create_node(AST_INT_LITERAL, "integer"));
    }
    | expression ADD expression {
        $$ = create_node(AST_EXPR, NULL);
        ASTNode* binary = create_node(AST_BINARY_EXPR, "+");
        add_child(binary, $1);
        add_child(binary, $3);
        add_child($$, binary);
    }
    | expression SUB expression {
        $$ = create_node(AST_EXPR, NULL);
        ASTNode* binary = create_node(AST_BINARY_EXPR, "-");
        add_child(binary, $1);
        add_child(binary, $3);
        add_child($$, binary);
    }
    | expression MUL expression {
        $$ = create_node(AST_EXPR, NULL);
        ASTNode* binary = create_node(AST_BINARY_EXPR, "*");
        add_child(binary, $1);
        add_child(binary, $3);
        add_child($$, binary);
    }
    | expression DIV expression {
        $$ = create_node(AST_EXPR, NULL);
        ASTNode* binary = create_node(AST_BINARY_EXPR, "/");
        add_child(binary, $1);
        add_child(binary, $3);
        add_child($$, binary);
    }
    | IDENTIFIER ASSIGN expression {
        $$ = create_node(AST_EXPR, NULL);
        ASTNode* assign = create_node(AST_ASSIGN_EXPR, "=");
        add_child(assign, create_node(AST_ID, "identifier"));
        add_child(assign, $3);
        add_child($$, assign);
    }
    | LPAREN expression RPAREN {
        $$ = $2;
    }
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

int main() {
    yyparse();
    
    if (ast_root) {
        printf("\n=== Abstract Syntax Tree ===\n");
        print_ast(ast_root, 0);
        printf("\nTotal nodes: %d\n", node_count);
        free_ast(ast_root);
    }
    
    return 0;
}