%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

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

#define ASM_OUTPUT_FILE "output.asm"
#define TEMP_REG_COUNT 8

typedef struct Symbol {
    char* name;
    char* label;
    struct Symbol* next;
} Symbol;

typedef struct StringLiteralEntry {
    char* value;
    char* label;
    struct StringLiteralEntry* next;
} StringLiteralEntry;

typedef struct FormatSplit {
    char* before;
    char* after;
    bool has_placeholder;
} FormatSplit;

static Symbol* symbol_head = NULL;
static Symbol* symbol_tail = NULL;
static StringLiteralEntry* string_head = NULL;
static StringLiteralEntry* string_tail = NULL;
static int string_literal_counter = 0;
static const char* temp_registers[TEMP_REG_COUNT] = {"t0", "t1", "t2", "t3", "t4", "t5", "t6"};
static int temp_reg_top = 0;

static void reset_codegen_counters(void);
static void free_symbol_table(void);
static void free_string_table(void);
static Symbol* find_symbol(const char* name);
static const char* ensure_symbol(const char* name);
static const char* ensure_string_literal(const char* literal);
static void collect_symbols(ASTNode* node);
static void collect_string_literals(ASTNode* node);
static FormatSplit split_format_literal(const char* literal);
static char* substring_to_quoted(const char* content, size_t start, size_t length);
static void free_format_split(FormatSplit* split);
static void emit_data_section(FILE* out);
static void emit_text_section(ASTNode* root, FILE* out);
static void emit_compound_statement(ASTNode* node, FILE* out, bool* has_return);
static void emit_declaration_list(ASTNode* node, FILE* out);
static void emit_declaration(ASTNode* node, FILE* out);
static void emit_init_list(ASTNode* node, FILE* out);
static void emit_initializer(ASTNode* node, FILE* out);
static void emit_statement_list(ASTNode* node, FILE* out, bool* has_return);
static void emit_statement(ASTNode* node, FILE* out, bool* has_return);
static void emit_expression_statement(ASTNode* node, FILE* out);
static void emit_printf_statement(ASTNode* node, FILE* out);
static void emit_return_statement(ASTNode* node, FILE* out, bool* has_return);
static void emit_print_string(const char* label, FILE* out);
static void emit_print_int(const char* reg, FILE* out);
static void store_into_symbol(const char* label, const char* reg, FILE* out);
static const char* acquire_temp_register(void);
static void release_temp_register(const char* reg);
static const char* load_identifier(const char* name, FILE* out);
static const char* load_integer_literal(const char* literal, FILE* out);
static const char* generate_expression(ASTNode* node, FILE* out);
static const char* generate_binary_expr(ASTNode* node, FILE* out);
static const char* generate_assignment_expr(ASTNode* node, FILE* out);
static void generate_assembly(ASTNode* root);

static void reset_codegen_counters(void) {
    temp_reg_top = 0;
}

static void free_symbol_table(void) {
    Symbol* current = symbol_head;
    while (current) {
        Symbol* next = current->next;
        free(current->name);
        free(current->label);
        free(current);
        current = next;
    }
    symbol_head = NULL;
    symbol_tail = NULL;
}

static void free_string_table(void) {
    StringLiteralEntry* current = string_head;
    while (current) {
        StringLiteralEntry* next = current->next;
        free(current->value);
        free(current->label);
        free(current);
        current = next;
    }
    string_head = NULL;
    string_tail = NULL;
}

static Symbol* find_symbol(const char* name) {
    Symbol* current = symbol_head;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

static const char* ensure_symbol(const char* name) {
    if (!name) return NULL;
    Symbol* existing = find_symbol(name);
    if (existing) {
        return existing->label;
    }
    Symbol* symbol = (Symbol*)malloc(sizeof(Symbol));
    if (!symbol) {
        fprintf(stderr, "符号表内存分配失败\n");
        exit(1);
    }
    symbol->name = strdup(name);
    size_t label_len = strlen(name) + 5;
    symbol->label = (char*)malloc(label_len);
    if (!symbol->label) {
        fprintf(stderr, "符号标签内存分配失败\n");
        exit(1);
    }
    snprintf(symbol->label, label_len, "var_%s", name);
    symbol->next = NULL;
    if (!symbol_head) {
        symbol_head = symbol;
        symbol_tail = symbol;
    } else {
        symbol_tail->next = symbol;
        symbol_tail = symbol;
    }
    return symbol->label;
}

static const char* ensure_string_literal(const char* literal) {
    if (!literal) return NULL;
    StringLiteralEntry* current = string_head;
    while (current) {
        if (strcmp(current->value, literal) == 0) {
            return current->label;
        }
        current = current->next;
    }
    StringLiteralEntry* entry = (StringLiteralEntry*)malloc(sizeof(StringLiteralEntry));
    if (!entry) {
        fprintf(stderr, "字符串常量表内存分配失败\n");
        exit(1);
    }
    entry->value = strdup(literal);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "str_%d", string_literal_counter++);
    entry->label = strdup(buffer);
    entry->next = NULL;
    if (!string_head) {
        string_head = entry;
        string_tail = entry;
    } else {
        string_tail->next = entry;
        string_tail = entry;
    }
    return entry->label;
}

static void collect_symbols(ASTNode* node) {
    if (!node) return;
    if (node->type == AST_INIT && node->child && node->child->type == AST_ID) {
        ensure_symbol(node->child->value);
    }
    collect_symbols(node->child);
    collect_symbols(node->sibling);
}

static char* substring_to_quoted(const char* content, size_t start, size_t length) {
    if (!content || length == 0) {
        return NULL;
    }
    char* result = (char*)malloc(length + 3);
    if (!result) {
        fprintf(stderr, "字符串分割内存分配失败\n");
        exit(1);
    }
    result[0] = '"';
    memcpy(result + 1, content + start, length);
    result[length + 1] = '"';
    result[length + 2] = '\0';
    return result;
}

static FormatSplit split_format_literal(const char* literal) {
    FormatSplit split = {0};
    if (!literal) {
        return split;
    }
    size_t len = strlen(literal);
    if (len < 2) {
        return split;
    }
    size_t content_len = len - 2;
    char* content = (char*)malloc(content_len + 1);
    if (!content) {
        fprintf(stderr, "格式串处理内存分配失败\n");
        exit(1);
    }
    memcpy(content, literal + 1, content_len);
    content[content_len] = '\0';
    char* placeholder = strstr(content, "%d");
    if (placeholder) {
        split.has_placeholder = true;
        size_t before_len = (size_t)(placeholder - content);
        size_t after_len = content_len - before_len - 2;
        split.before = substring_to_quoted(content, 0, before_len);
        split.after = substring_to_quoted(content, before_len + 2, after_len);
    }
    free(content);
    return split;
}

static void free_format_split(FormatSplit* split) {
    if (!split) return;
    if (split->before) {
        free(split->before);
        split->before = NULL;
    }
    if (split->after) {
        free(split->after);
        split->after = NULL;
    }
    split->has_placeholder = false;
}

static void collect_string_literals(ASTNode* node) {
    if (!node) return;
    if (node->type == AST_STRING_LITERAL && node->value) {
        ensure_string_literal(node->value);
    }
    if (node->type == AST_PRINTF_STMT && node->child && node->child->value) {
        ASTNode* expr_node = node->child->sibling;
        if (expr_node) {
            FormatSplit split = split_format_literal(node->child->value);
            if (split.has_placeholder) {
                if (split.before) {
                    ensure_string_literal(split.before);
                }
                if (split.after) {
                    ensure_string_literal(split.after);
                }
            }
            free_format_split(&split);
        }
    }
    collect_string_literals(node->child);
    collect_string_literals(node->sibling);
}

static const char* acquire_temp_register(void) {
    if (temp_reg_top >= TEMP_REG_COUNT) {
        fprintf(stderr, "错误: 临时寄存器不足\n");
        exit(1);
    }
    return temp_registers[temp_reg_top++];
}

static void release_temp_register(const char* reg) {
    if (!reg) return;
    if (temp_reg_top > 0) {
        temp_reg_top--;
    }
}

static void store_into_symbol(const char* label, const char* reg, FILE* out) {
    if (!label || !reg || !out) return;
    fprintf(out, "    la t6, %s\n", label);
    fprintf(out, "    sw %s, 0(t6)\n", reg);
}

static const char* load_identifier(const char* name, FILE* out) {
    if (!name) return NULL;
    Symbol* sym = find_symbol(name);
    if (!sym) {
        fprintf(stderr, "错误: 标识符 '%s' 未声明\n", name);
        return NULL;
    }
    const char* reg = acquire_temp_register();
    fprintf(out, "    la t6, %s\n", sym->label);
    fprintf(out, "    lw %s, 0(t6)\n", reg);
    return reg;
}

static const char* load_integer_literal(const char* literal, FILE* out) {
    if (!literal) return NULL;
    const char* reg = acquire_temp_register();
    fprintf(out, "    addi %s, zero, %s\n", reg, literal);
    return reg;
}

static const char* generate_binary_expr(ASTNode* node, FILE* out) {
    if (!node || !out) return NULL;
    ASTNode* left = node->child;
    ASTNode* right = left ? left->sibling : NULL;
    const char* left_reg = generate_expression(left, out);
    const char* right_reg = generate_expression(right, out);
    if (!left_reg || !right_reg) {
        return left_reg;
    }
    if (strcmp(node->value, "+") == 0) {
        fprintf(out, "    add %s, %s, %s\n", left_reg, left_reg, right_reg);
    } else if (strcmp(node->value, "-") == 0) {
        fprintf(out, "    sub %s, %s, %s\n", left_reg, left_reg, right_reg);
    } else if (strcmp(node->value, "*") == 0) {
        fprintf(out, "    mul %s, %s, %s\n", left_reg, left_reg, right_reg);
    } else if (strcmp(node->value, "/") == 0) {
        fprintf(out, "    div %s, %s, %s\n", left_reg, left_reg, right_reg);
    } else {
        fprintf(stderr, "暂不支持的二元运算符: %s\n", node->value);
    }
    release_temp_register(right_reg);
    return left_reg;
}

static const char* generate_assignment_expr(ASTNode* node, FILE* out) {
    if (!node || !out) return NULL;
    ASTNode* id_node = node->child;
    ASTNode* value_expr = id_node ? id_node->sibling : NULL;
    if (!id_node || id_node->type != AST_ID) {
        fprintf(stderr, "赋值表达式缺少标识符\n");
        return NULL;
    }
    Symbol* sym = find_symbol(id_node->value);
    if (!sym) {
        fprintf(stderr, "错误: 标识符 '%s' 未声明\n", id_node->value);
        return NULL;
    }
    const char* value_reg = generate_expression(value_expr, out);
    store_into_symbol(sym->label, value_reg, out);
    return value_reg;
}

static const char* generate_expression(ASTNode* node, FILE* out) {
    if (!node || !out) return NULL;
    ASTNode* inner = node->child;
    if (!inner) return NULL;
    switch (inner->type) {
        case AST_ID:
            return load_identifier(inner->value, out);
        case AST_INT_LITERAL:
            return load_integer_literal(inner->value, out);
        case AST_BINARY_EXPR:
            return generate_binary_expr(inner, out);
        case AST_ASSIGN_EXPR:
            return generate_assignment_expr(inner, out);
        case AST_EXPR:
            return generate_expression(inner, out);
        default:
            fprintf(stderr, "暂不支持的表达式类型: %d\n", inner->type);
            return NULL;
    }
}

static void emit_print_string(const char* label, FILE* out) {
    if (!label || !out) return;
    fprintf(out, "    la a0, %s\n", label);
    fprintf(out, "    addi a7, zero, 4\n");
    fprintf(out, "    ecall\n");
}

static void emit_print_int(const char* reg, FILE* out) {
    if (!reg || !out) return;
    fprintf(out, "    mv a0, %s\n", reg);
    fprintf(out, "    addi a7, zero, 1\n");
    fprintf(out, "    ecall\n");
}

static void emit_expression_statement(ASTNode* node, FILE* out) {
    if (!node || !out) return;
    ASTNode* expr = node->child;
    if (!expr) return;
    const char* reg = generate_expression(expr, out);
    if (reg) {
        release_temp_register(reg);
    }
}

static void emit_printf_statement(ASTNode* node, FILE* out) {
    if (!node || !out || !node->child) return;
    ASTNode* literal_node = node->child;
    ASTNode* expr_node = literal_node->sibling;
    const char* literal_label = ensure_string_literal(literal_node->value);
    if (!expr_node) {
        emit_print_string(literal_label, out);
        return;
    }
    FormatSplit split = split_format_literal(literal_node->value);
    if (split.has_placeholder) {
        if (split.before) {
            const char* before_label = ensure_string_literal(split.before);
            emit_print_string(before_label, out);
        }
        const char* value_reg = generate_expression(expr_node, out);
        if (value_reg) {
            emit_print_int(value_reg, out);
            release_temp_register(value_reg);
        }
        if (split.after) {
            const char* after_label = ensure_string_literal(split.after);
            emit_print_string(after_label, out);
        }
    } else {
        emit_print_string(literal_label, out);
        const char* value_reg = generate_expression(expr_node, out);
        if (value_reg) {
            emit_print_int(value_reg, out);
            release_temp_register(value_reg);
        }
    }
    free_format_split(&split);
}

static void emit_return_statement(ASTNode* node, FILE* out, bool* has_return) {
    if (!node || !out || !has_return) return;
    ASTNode* expr = node->child;
    if (expr) {
        const char* reg = generate_expression(expr, out);
        if (reg) {
            fprintf(out, "    mv a0, %s\n", reg);
            release_temp_register(reg);
        }
    } else {
        fprintf(out, "    addi a0, zero, 0\n");
    }
    fprintf(out, "    ret\n");
    *has_return = true;
}

static void emit_statement(ASTNode* node, FILE* out, bool* has_return) {
    if (!node || !out || (has_return && *has_return)) return;
    ASTNode* inner = node->child;
    if (!inner) return;
    switch (inner->type) {
        case AST_EXPR_STMT:
            emit_expression_statement(inner, out);
            break;
        case AST_COMPOUND_STMT:
            emit_compound_statement(inner, out, has_return);
            break;
        case AST_PRINTF_STMT:
            emit_printf_statement(inner, out);
            break;
        case AST_RETURN_STMT:
            emit_return_statement(inner, out, has_return);
            break;
        default:
            fprintf(stderr, "暂不支持的语句类型: %d\n", inner->type);
            break;
    }
}

static void emit_statement_list(ASTNode* node, FILE* out, bool* has_return) {
    if (!node || !out) return;
    ASTNode* current = node->child;
    while (current && !(has_return && *has_return)) {
        emit_statement(current, out, has_return);
        current = current->sibling;
    }
}

static void emit_initializer(ASTNode* node, FILE* out) {
    if (!node || !out) return;
    ASTNode* id_node = node->child;
    ASTNode* expr_node = id_node ? id_node->sibling : NULL;
    if (!id_node || id_node->type != AST_ID) return;
    ensure_symbol(id_node->value);
    if (!expr_node) return;
    const char* reg = generate_expression(expr_node, out);
    const char* label = ensure_symbol(id_node->value);
    store_into_symbol(label, reg, out);
    release_temp_register(reg);
}

static void emit_init_list(ASTNode* node, FILE* out) {
    if (!node) return;
    ASTNode* current = node->child;
    while (current) {
        emit_initializer(current, out);
        current = current->sibling;
    }
}

static void emit_declaration(ASTNode* node, FILE* out) {
    if (!node) return;
    ASTNode* type_spec = node->child;
    ASTNode* init_list = type_spec ? type_spec->sibling : NULL;
    if (init_list) {
        emit_init_list(init_list, out);
    }
}

static void emit_declaration_list(ASTNode* node, FILE* out) {
    if (!node) return;
    ASTNode* current = node->child;
    while (current) {
        emit_declaration(current, out);
        current = current->sibling;
    }
}

static void emit_compound_statement(ASTNode* node, FILE* out, bool* has_return) {
    if (!node) return;
    ASTNode* current = node->child;
    while (current && !(has_return && *has_return)) {
        if (current->type == AST_DECL_LIST) {
            emit_declaration_list(current, out);
        } else if (current->type == AST_STMT_LIST) {
            emit_statement_list(current, out, has_return);
        }
        current = current->sibling;
    }
}

static void emit_data_section(FILE* out) {
    fprintf(out, ".data\n");
    Symbol* sym = symbol_head;
    while (sym) {
        fprintf(out, "%s: .word 0\n", sym->label);
        sym = sym->next;
    }
    StringLiteralEntry* lit = string_head;
    while (lit) {
        fprintf(out, "%s: .asciiz %s\n", lit->label, lit->value);
        lit = lit->next;
    }
    fprintf(out, "\n");
}

static void emit_text_section(ASTNode* root, FILE* out) {
    if (!root || !out) return;
    ASTNode* main_func = root->child;
    if (!main_func) {
        fprintf(stderr, "未找到主函数\n");
        return;
    }
    ASTNode* type_spec = main_func->child;
    ASTNode* body = type_spec ? type_spec->sibling : NULL;
    fprintf(out, ".text\n");
    fprintf(out, ".globl main\n");
    fprintf(out, "main:\n");
    bool has_return = false;
    emit_compound_statement(body, out, &has_return);
    if (!has_return) {
        fprintf(out, "    addi a0, zero, 0\n");
        fprintf(out, "    ret\n");
    }
}

static void generate_assembly(ASTNode* root) {
    if (!root) {
        fprintf(stderr, "AST 为空，无法生成汇编\n");
        return;
    }
    free_symbol_table();
    free_string_table();
    string_literal_counter = 0;
    reset_codegen_counters();
    collect_symbols(root);
    collect_string_literals(root);
    FILE* asm_file = fopen(ASM_OUTPUT_FILE, "w");
    if (!asm_file) {
        fprintf(stderr, "无法创建汇编文件 '%s'\n", ASM_OUTPUT_FILE);
        return;
    }
    emit_data_section(asm_file);
    emit_text_section(root, asm_file);
    fclose(asm_file);
    free_symbol_table();
    free_string_table();
    reset_codegen_counters();
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
    if (yyparse() == 0 && ast_root) {
        printf("\n=== 生成汇编代码 ===\n");
        generate_assembly(ast_root);
        printf("汇编输出已保存到 '%s'\n", ASM_OUTPUT_FILE);
        printf("\nTotal nodes: %d\n", node_count);
        free_ast(ast_root);
    }
    
    return 0;
}