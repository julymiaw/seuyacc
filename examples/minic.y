%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* AST 节点类型枚举 */
typedef enum {
    AST_PROGRAM,
    AST_DECL_LIST,
    AST_VAR_DECL,
    AST_FUN_DECL,
    AST_TYPE_SPEC,
    AST_PARAMS,
    AST_PARAM_LIST,
    AST_PARAM,
    AST_COMPOUND_STMT,
    AST_LOCAL_DECLS,
    AST_LOCAL_DECL,
    AST_STMT_LIST,
    AST_STMT,
    AST_EXPR_STMT,
    AST_BLOCK_STMT,
    AST_IF_STMT,
    AST_WHILE_STMT,
    AST_RETURN_STMT,
    AST_CONTINUE_STMT,
    AST_BREAK_STMT,
    AST_EXPR,
    AST_BINARY_EXPR,
    AST_UNARY_EXPR,
    AST_ASSIGN_EXPR,
    AST_ARRAY_ACCESS,
    AST_FUNC_CALL,
    AST_IDENT,
    AST_INT_LITERAL,
    AST_ARG_LIST
} NodeType;

/* AST 节点结构 */
typedef struct ASTNode {
    NodeType type;
    char* value;
    char* op;  /* 用于存储运算符 */
    struct ASTNode* child;
    struct ASTNode* sibling;
} ASTNode;

/* 全局变量 */
ASTNode* ast_root = NULL;
int node_count = 0;

/* 符号表结构 */
typedef struct Symbol {
    char* name;
    char* label;
    int array_size;  /* 0 表示普通变量，>0 表示数组 */
    struct Symbol* next;
} Symbol;

typedef struct StringLiteralEntry {
    char* value;
    char* label;
    struct StringLiteralEntry* next;
} StringLiteralEntry;

static Symbol* symbol_head = NULL;
static Symbol* symbol_tail = NULL;
static StringLiteralEntry* string_head = NULL;
static StringLiteralEntry* string_tail = NULL;
static int string_literal_counter = 0;
static const char* temp_registers[8] = {"t0", "t1", "t2", "t3", "t4", "t5", "t6", "a0"};
static int temp_reg_top = 0;
static int label_counter = 0;

/* 函数声明 */
ASTNode* create_node(NodeType type, char* value);
void add_child(ASTNode* parent, ASTNode* child);
void free_ast(ASTNode* node);
void generate_assembly(ASTNode* root);
const char* node_type_str(NodeType type);

/* 创建新节点 */
ASTNode* create_node(NodeType type, char* value) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: 内存分配失败\n");
        exit(1);
    }
    node->type = type;
    node->value = value ? strdup(value) : NULL;
    node->op = NULL;
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

/* 释放 AST */
void free_ast(ASTNode* node) {
    if (!node) return;
    if (node->child) free_ast(node->child);
    if (node->sibling) free_ast(node->sibling);
    if (node->value) free(node->value);
    if (node->op) free(node->op);
    free(node);
}

/* 符号表管理 */
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
        fprintf(stderr, "Error: 符号表内存分配失败\n");
        exit(1);
    }
    symbol->name = strdup(name);
    size_t label_len = strlen(name) + 5;
    symbol->label = (char*)malloc(label_len);
    if (!symbol->label) {
        fprintf(stderr, "Error: 标签内存分配失败\n");
        exit(1);
    }
    snprintf(symbol->label, label_len, "var_%s", name);
    symbol->array_size = 0;
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

/* 收集符号 */
static void collect_symbols(ASTNode* node) {
    if (!node) return;
    
    /* 只收集变量声明和局部变量声明 */
    if (node->type == AST_VAR_DECL || node->type == AST_LOCAL_DECL) {
        /* 找到 IDENT 节点和可能的数组大小 */
        ASTNode* child = node->child;
        ASTNode* ident_node = NULL;
        ASTNode* size_node = NULL;
        
        while (child) {
            if (child->type == AST_IDENT) {
                ident_node = child;
            } else if (child->type == AST_INT_LITERAL && ident_node) {
                size_node = child;
            }
            child = child->sibling;
        }
        
        if (ident_node) {
            ensure_symbol(ident_node->value);
            /* 如果有数组大小,更新符号表 */
            if (size_node && node->value && strcmp(node->value, "array") == 0) {
                Symbol* sym = find_symbol(ident_node->value);
                if (sym && size_node->value) {
                    sym->array_size = atoi(size_node->value);
                }
            }
        }
    }
    
    /* 递归遍历 */
    collect_symbols(node->child);
    collect_symbols(node->sibling);
}

/* 临时寄存器管理 */
static const char* acquire_temp_register(void) {
    if (temp_reg_top >= 8) {
        fprintf(stderr, "Error: 临时寄存器耗尽\n");
        exit(1);
    }
    return temp_registers[temp_reg_top++];
}

static void release_temp_register(const char* reg) {
    if (temp_reg_top > 0) {
        temp_reg_top--;
    }
}

/* 生成标签 */
static char* generate_label(const char* prefix) {
    char* label = (char*)malloc(32);
    snprintf(label, 32, "%s%d", prefix, label_counter++);
    return label;
}

/* 表达式代码生成 */
static const char* generate_expression(ASTNode* node, FILE* out);

static const char* load_identifier(const char* name, FILE* out) {
    const char* label = ensure_symbol(name);
    const char* reg = acquire_temp_register();
    fprintf(out, "    la %s, %s\n", reg, label);
    fprintf(out, "    lw %s, 0(%s)\n", reg, reg);
    return reg;
}

static const char* load_integer_literal(const char* literal, FILE* out) {
    const char* reg = acquire_temp_register();
    fprintf(out, "    li %s, %s\n", reg, literal);
    return reg;
}

static const char* generate_binary_expr(ASTNode* node, FILE* out) {
    if (!node->child || !node->child->sibling) return NULL;
    
    const char* left_reg = generate_expression(node->child, out);
    const char* right_reg = generate_expression(node->child->sibling, out);
    const char* result_reg = acquire_temp_register();
    
    const char* op = node->op;
    if (strcmp(op, "+") == 0) {
        fprintf(out, "    add %s, %s, %s\n", result_reg, left_reg, right_reg);
    } else if (strcmp(op, "-") == 0) {
        fprintf(out, "    sub %s, %s, %s\n", result_reg, left_reg, right_reg);
    } else if (strcmp(op, "*") == 0) {
        fprintf(out, "    mul %s, %s, %s\n", result_reg, left_reg, right_reg);
    } else if (strcmp(op, "/") == 0) {
        fprintf(out, "    div %s, %s, %s\n", result_reg, left_reg, right_reg);
    } else if (strcmp(op, "%") == 0) {
        fprintf(out, "    rem %s, %s, %s\n", result_reg, left_reg, right_reg);
    } else if (strcmp(op, "&") == 0) {
        fprintf(out, "    and %s, %s, %s\n", result_reg, left_reg, right_reg);
    } else if (strcmp(op, "|") == 0) {
        fprintf(out, "    or %s, %s, %s\n", result_reg, left_reg, right_reg);
    } else if (strcmp(op, "^") == 0) {
        fprintf(out, "    xor %s, %s, %s\n", result_reg, left_reg, right_reg);
    } else if (strcmp(op, "<<") == 0) {
        fprintf(out, "    sll %s, %s, %s\n", result_reg, left_reg, right_reg);
    } else if (strcmp(op, ">>") == 0) {
        fprintf(out, "    sra %s, %s, %s\n", result_reg, left_reg, right_reg);
    } else if (strcmp(op, "<") == 0) {
        fprintf(out, "    slt %s, %s, %s\n", result_reg, left_reg, right_reg);
    } else if (strcmp(op, ">") == 0) {
        fprintf(out, "    slt %s, %s, %s\n", result_reg, right_reg, left_reg);
    } else if (strcmp(op, "==") == 0) {
        fprintf(out, "    sub %s, %s, %s\n", result_reg, left_reg, right_reg);
        fprintf(out, "    seqz %s, %s\n", result_reg, result_reg);
    } else if (strcmp(op, "!=") == 0) {
        fprintf(out, "    sub %s, %s, %s\n", result_reg, left_reg, right_reg);
        fprintf(out, "    snez %s, %s\n", result_reg, result_reg);
    } else if (strcmp(op, "<=") == 0) {
        fprintf(out, "    slt %s, %s, %s\n", result_reg, right_reg, left_reg);
        fprintf(out, "    xori %s, %s, 1\n", result_reg, result_reg);
    } else if (strcmp(op, ">=") == 0) {
        fprintf(out, "    slt %s, %s, %s\n", result_reg, left_reg, right_reg);
        fprintf(out, "    xori %s, %s, 1\n", result_reg, result_reg);
    } else if (strcmp(op, "&&") == 0) {
        fprintf(out, "    snez %s, %s\n", left_reg, left_reg);
        fprintf(out, "    snez %s, %s\n", right_reg, right_reg);
        fprintf(out, "    and %s, %s, %s\n", result_reg, left_reg, right_reg);
    } else if (strcmp(op, "||") == 0) {
        fprintf(out, "    or %s, %s, %s\n", result_reg, left_reg, right_reg);
        fprintf(out, "    snez %s, %s\n", result_reg, result_reg);
    }
    
    release_temp_register(right_reg);
    release_temp_register(left_reg);
    
    return result_reg;
}

static const char* generate_unary_expr(ASTNode* node, FILE* out) {
    if (!node->child) return NULL;
    
    const char* op = node->op;
    
    /* 特殊处理内存解引用 */
    if (strcmp(op, "$") == 0) {
        /* 内存解引用: $addr -> 读取地址处的值 */
        const char* addr_reg = generate_expression(node->child, out);
        if (!addr_reg) return NULL;
        
        const char* result_reg = acquire_temp_register();
        fprintf(out, "    lw %s, 0(%s)\n", result_reg, addr_reg);
        release_temp_register(addr_reg);
        return result_reg;
    }
    
    const char* operand_reg = generate_expression(node->child, out);
    const char* result_reg = acquire_temp_register();
    
    if (strcmp(op, "-") == 0) {
        fprintf(out, "    neg %s, %s\n", result_reg, operand_reg);
    } else if (strcmp(op, "!") == 0) {
        fprintf(out, "    seqz %s, %s\n", result_reg, operand_reg);
    } else if (strcmp(op, "~") == 0) {
        fprintf(out, "    not %s, %s\n", result_reg, operand_reg);
    } else if (strcmp(op, "+") == 0) {
        fprintf(out, "    mv %s, %s\n", result_reg, operand_reg);
    }
    
    release_temp_register(operand_reg);
    return result_reg;
}

static const char* generate_expression(ASTNode* node, FILE* out) {
    if (!node) return NULL;
    
    switch (node->type) {
        case AST_IDENT:
            return load_identifier(node->value, out);
        case AST_INT_LITERAL:
            return load_integer_literal(node->value, out);
        case AST_BINARY_EXPR:
            return generate_binary_expr(node, out);
        case AST_UNARY_EXPR:
            return generate_unary_expr(node, out);
        case AST_ARRAY_ACCESS: {
            /* 数组访问: a[index] */
            if (!node->child || !node->child->sibling) return NULL;
            
            ASTNode* array_id = node->child;
            ASTNode* index_expr = node->child->sibling;
            
            /* 计算索引 */
            const char* index_reg = generate_expression(index_expr, out);
            if (!index_reg) return NULL;
            
            /* 获取数组基地址 */
            const char* label = ensure_symbol(array_id->value);
            const char* base_reg = acquire_temp_register();
            fprintf(out, "    la %s, %s\n", base_reg, label);
            
            /* 计算偏移量 (index * 4) */
            const char* offset_reg = acquire_temp_register();
            fprintf(out, "    slli %s, %s, 2\n", offset_reg, index_reg);
            
            /* 计算实际地址 */
            const char* addr_reg = acquire_temp_register();
            fprintf(out, "    add %s, %s, %s\n", addr_reg, base_reg, offset_reg);
            
            /* 从内存加载值 */
            const char* result_reg = acquire_temp_register();
            fprintf(out, "    lw %s, 0(%s)\n", result_reg, addr_reg);
            
            release_temp_register(addr_reg);
            release_temp_register(offset_reg);
            release_temp_register(base_reg);
            release_temp_register(index_reg);
            
            return result_reg;
        }
        case AST_ASSIGN_EXPR: {
            if (!node->child || !node->child->sibling) return NULL;
            
            /* 生成右侧表达式的值 */
            const char* value_reg = generate_expression(node->child->sibling, out);
            if (!value_reg) return NULL;
            
            /* 检查是否是内存赋值 ($addr = value) */
            if (node->value && strcmp(node->value, "memory") == 0) {
                /* 内存赋值: $addr = value */
                const char* addr_reg = generate_expression(node->child, out);
                if (!addr_reg) return NULL;
                
                fprintf(out, "    sw %s, 0(%s)\n", value_reg, addr_reg);
                release_temp_register(addr_reg);
                return value_reg;
            }
            
            /* 获取左侧标识符或数组访问 */
            if (node->child->type == AST_IDENT) {
                /* 简单变量赋值 */
                const char* label = ensure_symbol(node->child->value);
                fprintf(out, "    la t6, %s\n", label);
                fprintf(out, "    sw %s, 0(t6)\n", value_reg);
            } else if (node->child->type == AST_ARRAY_ACCESS) {
                /* 数组元素赋值: a[index] = value */
                ASTNode* array_access = node->child;
                if (!array_access->child || !array_access->child->sibling) return NULL;
                
                ASTNode* array_id = array_access->child;
                ASTNode* index_expr = array_access->child->sibling;
                
                /* 计算索引 */
                const char* index_reg = generate_expression(index_expr, out);
                if (!index_reg) return NULL;
                
                /* 获取数组基地址 */
                const char* label = ensure_symbol(array_id->value);
                const char* base_reg = acquire_temp_register();
                fprintf(out, "    la %s, %s\n", base_reg, label);
                
                /* 计算偏移量 (index * 4) */
                const char* offset_reg = acquire_temp_register();
                fprintf(out, "    slli %s, %s, 2\n", offset_reg, index_reg);
                
                /* 计算实际地址 */
                const char* addr_reg = acquire_temp_register();
                fprintf(out, "    add %s, %s, %s\n", addr_reg, base_reg, offset_reg);
                
                /* 存储值到内存 */
                fprintf(out, "    sw %s, 0(%s)\n", value_reg, addr_reg);
                
                release_temp_register(addr_reg);
                release_temp_register(offset_reg);
                release_temp_register(base_reg);
                release_temp_register(index_reg);
            }
            return value_reg;
        }
        case AST_FUNC_CALL: {
            /* 函数调用: func(args) */
            /* TODO: 完整的函数调用需要处理参数传递、保存寄存器等 */
            /* 这里只是一个简化实现,返回 a0 (函数返回值寄存器) */
            
            /* 如果有参数列表,需要计算参数并传递到 a0-a7 */
            if (node->child && node->child->type == AST_ARG_LIST) {
                ASTNode* arg = node->child->child;
                int arg_count = 0;
                
                /* 计算并传递参数 (最多 8 个) */
                while (arg && arg_count < 8) {
                    const char* arg_reg = generate_expression(arg, out);
                    if (arg_reg) {
                        if (strcmp(arg_reg, temp_registers[7]) != 0) {
                            fprintf(out, "    mv a%d, %s\n", arg_count, arg_reg);
                            release_temp_register(arg_reg);
                        }
                        arg_count++;
                    }
                    arg = arg->sibling;
                }
            }
            
            /* 调用函数 */
            if (node->value) {
                fprintf(out, "    call %s\n", node->value);
            }
            
            /* 函数返回值在 a0 中 */
            const char* result_reg = acquire_temp_register();
            fprintf(out, "    mv %s, a0\n", result_reg);
            return result_reg;
        }
        default:
            return NULL;
    }
}

/* 语句代码生成 */
static void generate_statement(ASTNode* node, FILE* out, bool* has_return);

static void generate_statement_list(ASTNode* node, FILE* out, bool* has_return) {
    if (!node) return;
    ASTNode* stmt = node->child;
    while (stmt) {
        generate_statement(stmt, out, has_return);
        stmt = stmt->sibling;
    }
}

static void generate_return_statement(ASTNode* node, FILE* out, bool* has_return) {
    *has_return = true;
    if (node->child) {
        const char* result_reg = generate_expression(node->child, out);
        fprintf(out, "    mv a0, %s\n", result_reg);
        release_temp_register(result_reg);
    } else {
        fprintf(out, "    li a0, 0\n");
    }
    fprintf(out, "    j .exit\n");
}

static void generate_if_statement(ASTNode* node, FILE* out, bool* has_return) {
    if (!node->child) return;
    
    char* else_label = generate_label(".L_else");
    char* end_label = generate_label(".L_end_if");
    
    /* 计算条件表达式 */
    const char* cond_reg = generate_expression(node->child, out);
    fprintf(out, "    beqz %s, %s\n", cond_reg, node->child->sibling->sibling ? else_label : end_label);
    release_temp_register(cond_reg);
    
    /* then 分支 */
    if (node->child->sibling) {
        generate_statement(node->child->sibling, out, has_return);
    }
    
    /* else 分支 */
    if (node->child->sibling && node->child->sibling->sibling) {
        fprintf(out, "    j %s\n", end_label);
        fprintf(out, "%s:\n", else_label);
        generate_statement(node->child->sibling->sibling, out, has_return);
    }
    
    fprintf(out, "%s:\n", end_label);
    free(else_label);
    free(end_label);
}

static void generate_while_statement(ASTNode* node, FILE* out, bool* has_return) {
    if (!node->child) return;
    
    char* loop_label = generate_label(".L_while");
    char* end_label = generate_label(".L_end_while");
    
    fprintf(out, "%s:\n", loop_label);
    
    /* 计算条件表达式 */
    const char* cond_reg = generate_expression(node->child, out);
    fprintf(out, "    beqz %s, %s\n", cond_reg, end_label);
    release_temp_register(cond_reg);
    
    /* 循环体 */
    if (node->child->sibling) {
        generate_statement(node->child->sibling, out, has_return);
    }
    
    fprintf(out, "    j %s\n", loop_label);
    fprintf(out, "%s:\n", end_label);
    
    free(loop_label);
    free(end_label);
}

static void generate_statement(ASTNode* node, FILE* out, bool* has_return) {
    if (!node) return;
    
    switch (node->type) {
        case AST_EXPR_STMT:
            if (node->child) {
                const char* reg = generate_expression(node->child, out);
                if (reg) release_temp_register(reg);
            }
            break;
        case AST_RETURN_STMT:
            generate_return_statement(node, out, has_return);
            break;
        case AST_IF_STMT:
            generate_if_statement(node, out, has_return);
            break;
        case AST_WHILE_STMT:
            generate_while_statement(node, out, has_return);
            break;
        case AST_BLOCK_STMT:
        case AST_STMT_LIST:
            generate_statement_list(node, out, has_return);
            break;
        case AST_CONTINUE_STMT:
            /* TODO: 实现 continue */
            break;
        case AST_BREAK_STMT:
            /* TODO: 实现 break */
            break;
        default:
            break;
    }
}

/* 生成数据段 */
static void emit_data_section(FILE* out) {
    fprintf(out, ".data\n");
    Symbol* current = symbol_head;
    while (current) {
        if (current->array_size > 0) {
            /* 数组：分配多个字 */
            fprintf(out, "%s: .word", current->label);
            for (int i = 0; i < current->array_size; i++) {
                fprintf(out, " 0");
                if (i < current->array_size - 1) fprintf(out, ",");
            }
            fprintf(out, "\n");
        } else {
            /* 普通变量 */
            fprintf(out, "%s: .word 0\n", current->label);
        }
        current = current->next;
    }
    fprintf(out, "\n");
}

/* 生成代码段 */
static void emit_text_section(ASTNode* root, FILE* out) {
    fprintf(out, ".text\n");
    fprintf(out, ".globl main\n");
    fprintf(out, "main:\n");
    
    bool has_return = false;
    bool found_main = false;
    
    /* 遍历声明列表，找到 main 函数(或第一个函数) */
    if (root->child && root->child->type == AST_DECL_LIST) {
        ASTNode* decl = root->child->child;
        ASTNode* first_func = NULL;
        
        while (decl) {
            if (decl->type == AST_FUN_DECL) {
                /* 函数声明：type_spec -> IDENT -> params -> compound_stmt */
                ASTNode* type_node = decl->child;
                ASTNode* ident_node = type_node ? type_node->sibling : NULL;
                
                /* 记录第一个函数 */
                if (!first_func) {
                    first_func = decl;
                }
                
                if (ident_node && ident_node->type == AST_IDENT && 
                    ident_node->value && strcmp(ident_node->value, "main") == 0) {
                    /* 找到 main 函数 */
                    found_main = true;
                    ASTNode* params = ident_node->sibling;
                    ASTNode* compound = params ? params->sibling : NULL;
                    
                    if (compound && compound->type == AST_COMPOUND_STMT) {
                        /* compound_stmt 包含 local_decls 和 stmt_list */
                        ASTNode* child = compound->child;
                        while (child) {
                            if (child->type == AST_LOCAL_DECLS) {
                                /* 局部声明已经在 collect_symbols 中处理 */
                            } else if (child->type == AST_STMT_LIST) {
                                generate_statement_list(child, out, &has_return);
                            }
                            child = child->sibling;
                        }
                    }
                    break;
                }
            }
            decl = decl->sibling;
        }
        
        /* 如果没有找到 main 函数,使用第一个函数 */
        if (!found_main && first_func) {
            ASTNode* type_node = first_func->child;
            ASTNode* ident_node = type_node ? type_node->sibling : NULL;
            ASTNode* params = ident_node ? ident_node->sibling : NULL;
            ASTNode* compound = params ? params->sibling : NULL;
            
            if (compound && compound->type == AST_COMPOUND_STMT) {
                ASTNode* child = compound->child;
                while (child) {
                    if (child->type == AST_LOCAL_DECLS) {
                        /* 局部声明已经在 collect_symbols 中处理 */
                    } else if (child->type == AST_STMT_LIST) {
                        generate_statement_list(child, out, &has_return);
                    }
                    child = child->sibling;
                }
            }
        }
    }
    
    /* 如果没有显式 return，添加默认退出 */
    if (!has_return) {
        fprintf(out, "    li a0, 0\n");
    }
    
    fprintf(out, ".exit:\n");
    fprintf(out, "    li a7, 93\n");
    fprintf(out, "    ecall\n");
}

/* 生成汇编代码 */
void generate_assembly(ASTNode* root) {
    if (!root) return;
    
    FILE* out = fopen("output.asm", "w");
    if (!out) {
        fprintf(stderr, "Error: 无法打开输出文件 output.asm\n");
        return;
    }
    
    printf("开始生成 RISC-V 汇编代码...\n");
    
    /* 收集符号 */
    collect_symbols(root);
    
    /* 生成数据段 */
    emit_data_section(out);
    
    /* 生成代码段 */
    emit_text_section(root, out);
    
    fclose(out);
    
    printf("汇编代码已生成到 output.asm\n");
    
    /* 清理 */
    free_symbol_table();
}

/* 前向声明 */
extern int yylex();
extern int yylineno;
extern char* yytext;
void yyerror(const char *s) {
    fprintf(stderr, "Error at line %d: %s\n", yylineno, s);
}
int Lineno = 1;
%}

%union {
    char* str_val;
    int int_val;
    struct ASTNode* node;
}

%token <str_val> IDENT
%token <str_val> DECNUM HEXNUM
%token VOID INT WHILE IF ELSE RETURN EQ NE LE GE AND OR CONTINUE BREAK LSHIFT RSHIFT

%type <node> program decl_list decl var_decl fun_decl type_spec
%type <node> params param_list param compound_stmt local_decls local_decl
%type <node> stmt_list stmt expr_stmt block_stmt if_stmt while_stmt return_stmt
%type <node> continue_stmt break_stmt expr int_literal arg_list args

%left OR
%left AND
%left EQ NE LE GE '<' '>'
%left '+' '-'
%left '|'
%left '&' '^'
%left '*' '/' '%'
%right LSHIFT RSHIFT
%right '!'
%right '~'
%nonassoc UMINUS
%nonassoc MPR

%start program

%%

program : decl_list {
    $$ = create_node(AST_PROGRAM, NULL);
    add_child($$, $1);
    ast_root = $$;
};

decl_list : decl_list decl {
    $$ = $1;
    add_child($$, $2);
}
| decl {
    $$ = create_node(AST_DECL_LIST, NULL);
    add_child($$, $1);
};

decl : var_decl { $$ = $1; }
| fun_decl { $$ = $1; };

var_decl : type_spec IDENT ';' {
    $$ = create_node(AST_VAR_DECL, NULL);
    add_child($$, $1);
    add_child($$, create_node(AST_IDENT, $2));
}
| type_spec IDENT '[' int_literal ']' ';' {
    $$ = create_node(AST_VAR_DECL, "array");
    add_child($$, $1);
    add_child($$, create_node(AST_IDENT, $2));
    add_child($$, $4);
};

type_spec : VOID {
    $$ = create_node(AST_TYPE_SPEC, "void");
}
| INT {
    $$ = create_node(AST_TYPE_SPEC, "int");
};

fun_decl : type_spec IDENT '(' params ')' compound_stmt {
    $$ = create_node(AST_FUN_DECL, NULL);
    add_child($$, $1);
    add_child($$, create_node(AST_IDENT, $2));
    add_child($$, $4);
    add_child($$, $6);
}
| type_spec IDENT '(' params ')' ';' {
    $$ = create_node(AST_FUN_DECL, "declaration");
    add_child($$, $1);
    add_child($$, create_node(AST_IDENT, $2));
    add_child($$, $4);
};

params : param_list { $$ = $1; }
| VOID {
    $$ = create_node(AST_PARAMS, "void");
};

param_list : param_list ',' param {
    $$ = $1;
    add_child($$, $3);
}
| param {
    $$ = create_node(AST_PARAM_LIST, NULL);
    add_child($$, $1);
};

param : type_spec IDENT {
    $$ = create_node(AST_PARAM, NULL);
    add_child($$, $1);
    add_child($$, create_node(AST_IDENT, $2));
}
| type_spec IDENT '[' int_literal ']' {
    $$ = create_node(AST_PARAM, "array");
    add_child($$, $1);
    add_child($$, create_node(AST_IDENT, $2));
    add_child($$, $4);
};

stmt_list : stmt_list stmt {
    $$ = $1;
    add_child($$, $2);
}
| {
    $$ = create_node(AST_STMT_LIST, NULL);
};

stmt : expr_stmt { $$ = $1; }
| block_stmt { $$ = $1; }
| if_stmt { $$ = $1; }
| while_stmt { $$ = $1; }
| return_stmt { $$ = $1; }
| continue_stmt { $$ = $1; }
| break_stmt { $$ = $1; };

expr_stmt : IDENT '=' expr ';' {
    $$ = create_node(AST_EXPR_STMT, NULL);
    ASTNode* assign = create_node(AST_ASSIGN_EXPR, NULL);
    assign->op = strdup("=");
    add_child(assign, create_node(AST_IDENT, $1));
    add_child(assign, $3);
    add_child($$, assign);
}
| IDENT '[' expr ']' '=' expr ';' {
    $$ = create_node(AST_EXPR_STMT, NULL);
    ASTNode* assign = create_node(AST_ASSIGN_EXPR, "array");
    assign->op = strdup("=");
    ASTNode* array_access = create_node(AST_ARRAY_ACCESS, NULL);
    add_child(array_access, create_node(AST_IDENT, $1));
    add_child(array_access, $3);
    add_child(assign, array_access);
    add_child(assign, $6);
    add_child($$, assign);
}
| '$' expr '=' expr ';' {
    $$ = create_node(AST_EXPR_STMT, "memory_assign");
    ASTNode* assign = create_node(AST_ASSIGN_EXPR, "memory");
    assign->op = strdup("=");
    add_child(assign, $2);
    add_child(assign, $4);
    add_child($$, assign);
}
| IDENT '(' args ')' ';' {
    $$ = create_node(AST_EXPR_STMT, NULL);
    ASTNode* call = create_node(AST_FUNC_CALL, $1);
    add_child(call, $3);
    add_child($$, call);
};

while_stmt : WHILE '(' expr ')' stmt {
    $$ = create_node(AST_WHILE_STMT, NULL);
    add_child($$, $3);
    add_child($$, $5);
};

block_stmt : '{' stmt_list '}' {
    $$ = create_node(AST_BLOCK_STMT, NULL);
    add_child($$, $2);
};

compound_stmt : '{' local_decls stmt_list '}' {
    $$ = create_node(AST_COMPOUND_STMT, NULL);
    add_child($$, $2);
    add_child($$, $3);
};

local_decls : local_decls local_decl {
    $$ = $1;
    add_child($$, $2);
}
| {
    $$ = create_node(AST_LOCAL_DECLS, NULL);
};

local_decl : type_spec IDENT ';' {
    $$ = create_node(AST_LOCAL_DECL, NULL);
    add_child($$, $1);
    add_child($$, create_node(AST_IDENT, $2));
}
| type_spec IDENT '[' int_literal ']' ';' {
    $$ = create_node(AST_LOCAL_DECL, "array");
    add_child($$, $1);
    add_child($$, create_node(AST_IDENT, $2));
    add_child($$, $4);
};

if_stmt : IF '(' expr ')' stmt %prec UMINUS {
    $$ = create_node(AST_IF_STMT, NULL);
    add_child($$, $3);
    add_child($$, $5);
}
| IF '(' expr ')' stmt ELSE stmt %prec MPR {
    $$ = create_node(AST_IF_STMT, "else");
    add_child($$, $3);
    add_child($$, $5);
    add_child($$, $7);
};

return_stmt : RETURN ';' {
    $$ = create_node(AST_RETURN_STMT, NULL);
}
| RETURN expr ';' {
    $$ = create_node(AST_RETURN_STMT, NULL);
    add_child($$, $2);
};

expr : expr OR expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("||");
    add_child($$, $1);
    add_child($$, $3);
}
| expr EQ expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("==");
    add_child($$, $1);
    add_child($$, $3);
}
| expr NE expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("!=");
    add_child($$, $1);
    add_child($$, $3);
}
| expr LE expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("<=");
    add_child($$, $1);
    add_child($$, $3);
}
| expr '<' expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("<");
    add_child($$, $1);
    add_child($$, $3);
}
| expr GE expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup(">=");
    add_child($$, $1);
    add_child($$, $3);
}
| expr '>' expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup(">");
    add_child($$, $1);
    add_child($$, $3);
}
| expr AND expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("&&");
    add_child($$, $1);
    add_child($$, $3);
}
| expr '+' expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("+");
    add_child($$, $1);
    add_child($$, $3);
}
| expr '-' expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("-");
    add_child($$, $1);
    add_child($$, $3);
}
| expr '*' expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("*");
    add_child($$, $1);
    add_child($$, $3);
}
| expr '/' expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("/");
    add_child($$, $1);
    add_child($$, $3);
}
| expr '%' expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("%");
    add_child($$, $1);
    add_child($$, $3);
}
| '!' expr %prec UMINUS {
    $$ = create_node(AST_UNARY_EXPR, NULL);
    $$->op = strdup("!");
    add_child($$, $2);
}
| '-' expr %prec UMINUS {
    $$ = create_node(AST_UNARY_EXPR, NULL);
    $$->op = strdup("-");
    add_child($$, $2);
}
| '+' expr %prec UMINUS {
    $$ = create_node(AST_UNARY_EXPR, NULL);
    $$->op = strdup("+");
    add_child($$, $2);
}
| '$' expr %prec UMINUS {
    $$ = create_node(AST_UNARY_EXPR, "memory_deref");
    $$->op = strdup("$");
    add_child($$, $2);
}
| '(' expr ')' {
    $$ = $2;
}
| IDENT {
    $$ = create_node(AST_IDENT, $1);
}
| IDENT '[' expr ']' {
    $$ = create_node(AST_ARRAY_ACCESS, NULL);
    add_child($$, create_node(AST_IDENT, $1));
    add_child($$, $3);
}
| IDENT '(' args ')' {
    $$ = create_node(AST_FUNC_CALL, $1);
    add_child($$, $3);
}
| int_literal {
    $$ = $1;
}
| expr '&' expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("&");
    add_child($$, $1);
    add_child($$, $3);
}
| expr '^' expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("^");
    add_child($$, $1);
    add_child($$, $3);
}
| '~' expr {
    $$ = create_node(AST_UNARY_EXPR, NULL);
    $$->op = strdup("~");
    add_child($$, $2);
}
| expr LSHIFT expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("<<");
    add_child($$, $1);
    add_child($$, $3);
}
| expr RSHIFT expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup(">>");
    add_child($$, $1);
    add_child($$, $3);
}
| expr '|' expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("|");
    add_child($$, $1);
    add_child($$, $3);
};

int_literal : DECNUM {
    $$ = create_node(AST_INT_LITERAL, $1);
}
| HEXNUM {
    /* 将十六进制转换为十进制字符串 */
    char buffer[32];
    unsigned long val = strtoul($1, NULL, 16);
    snprintf(buffer, sizeof(buffer), "%lu", val);
    $$ = create_node(AST_INT_LITERAL, buffer);
};

arg_list : arg_list ',' expr {
    $$ = $1;
    add_child($$, $3);
}
| expr {
    $$ = create_node(AST_ARG_LIST, NULL);
    add_child($$, $1);
};

args : arg_list { $$ = $1; }
| {
    $$ = create_node(AST_ARG_LIST, NULL);
};

continue_stmt : CONTINUE ';' {
    $$ = create_node(AST_CONTINUE_STMT, NULL);
};

break_stmt : BREAK ';' {
    $$ = create_node(AST_BREAK_STMT, NULL);
};

%%

int main() {
    printf("开始解析 MiniC 程序...\n");
    
    int result = yyparse();
    
    if (result == 0) {
        printf("解析成功！\n");
        printf("AST 节点总数: %d\n\n", node_count);
        
        if (ast_root) {
            generate_assembly(ast_root);
            free_ast(ast_root);
        }
    } else {
        printf("解析失败！\n");
    }
    
    return result;
}