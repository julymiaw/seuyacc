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
    AST_INT_LITERAL,
    AST_STRING_LITERAL
} NodeType;

/* 抽象语法树节点 */
typedef struct ASTNode {
    NodeType type;
    char* value;
    struct ASTNode* child;    /* 第一个子节点 */
    struct ASTNode* sibling;  /* 兄弟节点 */
} ASTNode;

/* 函数原型声明 */
int print_ast_dot_node(ASTNode* node, FILE* dot_file, int* next_id, int parent_id);
char* escape_string(const char* str);
void print_ast_dot(ASTNode* node);

/* 全局变量 */
ASTNode* ast_root = NULL;
int node_count = 0;

/* 创建新节点 */
ASTNode* create_node(NodeType type, char* value) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "内存分配失败\n");
        exit(1);
    }
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
        case AST_STRING_LITERAL: return "StringLiteral";
        default: return "Unknown";
    }
}

/* 打印抽象语法树 */
void print_ast_dot(ASTNode* node) {
    static int node_id = 0;
    int current_node_id;
    FILE* dot_file = fopen("ast.dot", "w");
    
    if (!dot_file) {
        fprintf(stderr, "无法创建 DOT 文件\n");
        return;
    }
    
    /* 写入 DOT 文件头部 */
    fprintf(dot_file, "digraph AST {\n");
    fprintf(dot_file, "  node [shape=box, fontname=\"SimSun\", fontsize=12];\n");
    fprintf(dot_file, "  edge [fontname=\"SimSun\", fontsize=10];\n\n");
    
    /* 重置节点ID计数器 */
    node_id = 0;
    
    /* 递归生成节点和边 */
    print_ast_dot_node(node, dot_file, &node_id, -1);
    
    /* 写入 DOT 文件结尾 */
    fprintf(dot_file, "}\n");
    fclose(dot_file);
    
    printf("\n抽象语法树已保存为 'ast.dot'\n");
    printf("可以使用 Graphviz 工具将其转换为图像: dot -Tpng ast.dot -o ast.png\n");
}

/* 递归生成 DOT 格式的节点和边 */
int print_ast_dot_node(ASTNode* node, FILE* dot_file, int* next_id, int parent_id) {
    if (!node) return -1;
    
    int current_id = (*next_id)++;
    
    /* 生成当前节点 */
    fprintf(dot_file, "  node%d [label=\"%s", current_id, node_type_str(node->type));
    if (node->value) {
        /* 转义双引号以避免 DOT 格式错误 */
        char* escaped_value = escape_string(node->value);
        fprintf(dot_file, "\\n(%s)", escaped_value);
        free(escaped_value);
    }
    fprintf(dot_file, "\"];\n");
    
    /* 如果有父节点，添加一条边 */
    if (parent_id >= 0) {
        fprintf(dot_file, "  node%d -> node%d;\n", parent_id, current_id);
    }
    
    /* 处理子节点 */
    if (node->child) {
        print_ast_dot_node(node->child, dot_file, next_id, current_id);
    }
    
    /* 处理兄弟节点 */
    if (node->sibling) {
        print_ast_dot_node(node->sibling, dot_file, next_id, parent_id);
    }
    
    return current_id;
}

/* 转义字符串中的特殊字符 */
char* escape_string(const char* str) {
    if (!str) return NULL;
    
    int len = strlen(str);
    /* 分配足够的空间容纳转义后的字符串（最坏情况下每个字符都需要转义） */
    char* escaped = (char*)malloc(len * 2 + 1);
    
    int i, j = 0;
    for (i = 0; i < len; i++) {
        switch (str[i]) {
            case '\"':
                escaped[j++] = '\\';
                escaped[j++] = '"';
                break;
            case '\\':
                escaped[j++] = '\\';
                escaped[j++] = '\\';
                break;
            case '\n':
                escaped[j++] = '\\';
                escaped[j++] = 'n';
                break;
            default:
                escaped[j++] = str[i];
                break;
        }
    }
    escaped[j] = '\0';
    
    return escaped;
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

/* 前向声明以避免警告 */
int yylex();
void yyerror(const char *s);
%}

%union {
    int ival;
    char* sval;
    struct ASTNode* node;
}

/* 定义各个终结符和非终结符的类型 */
%token <ival> INTEGER_LITERAL
%token <sval> IDENTIFIER STRING_LITERAL
%token INT CHAR VOID MAIN PRINTF RETURN
/* %token ADD SUB MUL DIV ASSIGN SEMICOLON LPAREN RPAREN LBRACE RBRACE COMMA */
%token SEMICOLON LBRACE RBRACE

/* 定义各非终结符的类型 */
%type <node> program main_function type_specifier compound_statement
%type <node> opt_declaration_list opt_statement_list declaration_list declaration
%type <node> init_declarator_list init_declarator initializer statement_list statement
%type <node> expression_statement printf_statement return_statement expression

%left '='  /* 最低优先级 */
%left '+' '-'
%left '*' '/'  /* 最高优先级 */

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
    : type_specifier MAIN '(' ')' compound_statement {
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
    | init_declarator_list ',' init_declarator {
        $$ = $1;
        add_child($$, $3);
    }
    ;

init_declarator
    : IDENTIFIER {
        $$ = create_node(AST_INIT, NULL);
        add_child($$, create_node(AST_ID, $1));
    }
    | IDENTIFIER '=' initializer {
        $$ = create_node(AST_INIT, NULL);
        add_child($$, create_node(AST_ID, $1));
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
    : PRINTF '(' STRING_LITERAL ',' expression ')' SEMICOLON {
        $$ = create_node(AST_PRINTF_STMT, NULL);
        add_child($$, create_node(AST_STRING_LITERAL, $3));
        add_child($$, $5);
    }
    | PRINTF '(' STRING_LITERAL ')' SEMICOLON {
        $$ = create_node(AST_PRINTF_STMT, NULL);
        add_child($$, create_node(AST_STRING_LITERAL, $3));
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
        add_child($$, create_node(AST_ID, $1));
    }
    | INTEGER_LITERAL {
        char buffer[32];
        sprintf(buffer, "%d", $1);
        $$ = create_node(AST_EXPR, NULL);
        add_child($$, create_node(AST_INT_LITERAL, buffer));
    }
    | expression '+' expression {
        $$ = create_node(AST_EXPR, NULL);
        ASTNode* binary = create_node(AST_BINARY_EXPR, "+");
        add_child(binary, $1);
        add_child(binary, $3);
        add_child($$, binary);
    }
    | expression '-' expression {
        $$ = create_node(AST_EXPR, NULL);
        ASTNode* binary = create_node(AST_BINARY_EXPR, "-");
        add_child(binary, $1);
        add_child(binary, $3);
        add_child($$, binary);
    }
    | expression '*' expression {
        $$ = create_node(AST_EXPR, NULL);
        ASTNode* binary = create_node(AST_BINARY_EXPR, "*");
        add_child(binary, $1);
        add_child(binary, $3);
        add_child($$, binary);
    }
    | expression '/' expression {
        $$ = create_node(AST_EXPR, NULL);
        ASTNode* binary = create_node(AST_BINARY_EXPR, "/");
        add_child(binary, $1);
        add_child(binary, $3);
        add_child($$, binary);
    }
    | IDENTIFIER '=' expression {
        $$ = create_node(AST_EXPR, NULL);
        ASTNode* assign = create_node(AST_ASSIGN_EXPR, "=");
        add_child(assign, create_node(AST_ID, $1));
        add_child(assign, $3);
        add_child($$, assign);
    }
    | '(' expression ')' {
        $$ = $2;
    }
    ;

%%

extern int yylex();
extern int line_number;
extern char* yytext;

void yyerror(const char *s)
{
    fflush(stdout);
    fprintf(stderr, "错误 (行 %d): %s\n", line_number, s);
}

int main() {
    yyparse();
    
    if (ast_root) {
        printf("\n=== 生成抽象语法树 ===\n");
        print_ast_dot(ast_root);
        printf("\nTotal nodes: %d\n", node_count);
        free_ast(ast_root);
    }
    
    return 0;
}