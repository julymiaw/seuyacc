%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ============ AST ============ */
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

typedef struct ASTNode {
    NodeType type;
    char* value;
    char* op;
    struct ASTNode* child;
    struct ASTNode* sibling;
} ASTNode;

ASTNode* ast_root = NULL;
int node_count = 0;

/* ============ Symbol Table ============ */
typedef enum { SCOPE_GLOBAL, SCOPE_LOCAL, SCOPE_PARAM } Scope;

typedef struct Symbol {
    char* name;
    char* label;          /* only for global */
    Scope scope;
    char* func;           /* function name for local/param */
    int offset;           /* relative to s0, negative */
    int array_size;       /* for real arrays; 0 means scalar or pointer */
    bool is_param_array;  /* param array treated as pointer */
    struct Symbol* next;
} Symbol;

static Symbol* global_head = NULL;
static Symbol* global_tail = NULL;

static Symbol* local_head = NULL; /* includes params + locals for current function */
static Symbol* local_tail = NULL;

/* temp regs */
static const char* temp_registers[8] = {"t0","t1","t2","t3","t4","t5","t6","a0"};
static int temp_reg_top = 0;

static int label_counter = 0;
static const char* current_ret_label = NULL;

/* ============ AST helpers ============ */
static ASTNode* create_node(NodeType type, char* value) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    if (!node) { fprintf(stderr, "Error: malloc failed\n"); exit(1); }
    node->type = type;
    node->value = value ? strdup(value) : NULL;
    node->op = NULL;
    node->child = NULL;
    node->sibling = NULL;
    node_count++;
    return node;
}

static void add_child(ASTNode* parent, ASTNode* child) {
    if (!parent || !child) return;
    if (!parent->child) parent->child = child;
    else {
        ASTNode* s = parent->child;
        while (s->sibling) s = s->sibling;
        s->sibling = child;
    }
}

static void free_ast(ASTNode* node) {
    if (!node) return;
    free_ast(node->child);
    free_ast(node->sibling);
    if (node->value) free(node->value);
    if (node->op) free(node->op);
    free(node);
}

/* ============ Symbol helpers ============ */
static void clear_locals(void) {
    Symbol* s = local_head;
    while (s) {
        Symbol* n = s->next;
        free(s->name);
        if (s->func) free(s->func);
        free(s);
        s = n;
    }
    local_head = local_tail = NULL;
}

static void free_globals(void) {
    Symbol* s = global_head;
    while (s) {
        Symbol* n = s->next;
        free(s->name);
        free(s->label);
        free(s);
        s = n;
    }
    global_head = global_tail = NULL;
}

static Symbol* find_global(const char* name) {
    for (Symbol* s = global_head; s; s = s->next)
        if (strcmp(s->name, name) == 0) return s;
    return NULL;
}

static Symbol* add_global(const char* name) {
    Symbol* s = find_global(name);
    if (s) return s;

    s = (Symbol*)malloc(sizeof(Symbol));
    if (!s) { fprintf(stderr, "Error: malloc failed\n"); exit(1); }

    s->name = strdup(name);
    s->label = (char*)malloc(strlen(name) + 5);
    sprintf(s->label, "var_%s", name);
    s->scope = SCOPE_GLOBAL;
    s->func = NULL;
    s->offset = 0;
    s->array_size = 0;
    s->is_param_array = false;
    s->next = NULL;

    if (!global_head) global_head = global_tail = s;
    else global_tail = global_tail->next = s;
    return s;
}

static Symbol* find_local(const char* name) {
    for (Symbol* s = local_head; s; s = s->next)
        if (strcmp(s->name, name) == 0) return s;
    return NULL;
}

static Symbol* add_local(const char* func, const char* name, int offset,
                         int array_size, Scope scope, bool is_param_array) {
    Symbol* s = (Symbol*)malloc(sizeof(Symbol));
    if (!s) { fprintf(stderr, "Error: malloc failed\n"); exit(1); }

    s->name = strdup(name);
    s->label = NULL;
    s->scope = scope;
    s->func = func ? strdup(func) : NULL;
    s->offset = offset;
    s->array_size = array_size;
    s->is_param_array = is_param_array;
    s->next = NULL;

    if (!local_head) local_head = local_tail = s;
    else local_tail = local_tail->next = s;
    return s;
}

/* ============ temp regs ============ */
static const char* acquire_temp_register(void) {
    if (temp_reg_top >= 8) { fprintf(stderr, "Error: temp regs exhausted\n"); exit(1); }
    return temp_registers[temp_reg_top++];
}
static void release_temp_register(const char* reg) {
    (void)reg;
    if (temp_reg_top > 0) temp_reg_top--;
}

/* ============ labels ============ */
static char* generate_label(const char* prefix) {
    char* label = (char*)malloc(64);
    snprintf(label, 64, "%s%d", prefix, label_counter++);
    return label;
}

/* ============ collect globals ============ */
static void collect_globals(ASTNode* node) {
    if (!node) return;

    if (node->type == AST_VAR_DECL) {
        ASTNode* id = NULL;
        ASTNode* size = NULL;
        for (ASTNode* c = node->child; c; c = c->sibling) {
            if (c->type == AST_IDENT) id = c;
            else if (c->type == AST_INT_LITERAL) size = c;
        }
        if (id && id->value) {
            Symbol* g = add_global(id->value);
            if (node->value && strcmp(node->value, "array") == 0 && size && size->value)
                g->array_size = atoi(size->value);
        }
    }

    collect_globals(node->child);
    collect_globals(node->sibling);
}

/* ============ build params/locals on stack ============ */
/* layout: offsets are negative relative to s0 */
static int build_params(ASTNode* params, const char* func, int start_offset_bytes) {
    int offset = start_offset_bytes; /* already allocated bytes */

    if (!params) return offset;

    ASTNode* first = NULL;
    if (params->type == AST_PARAM_LIST) first = params->child;
    else if (params->type == AST_PARAMS) {
        if (params->value && strcmp(params->value, "void") == 0) return offset;
        first = params->child;
    }

    for (ASTNode* p = first; p; p = p->sibling) {
        if (p->type != AST_PARAM) continue;

        ASTNode* id = NULL;
        ASTNode* size = NULL;
        for (ASTNode* c = p->child; c; c = c->sibling) {
            if (c->type == AST_IDENT) id = c;
            else if (c->type == AST_INT_LITERAL) size = c;
        }
        if (!id || !id->value) continue;

        bool is_arr = (p->value && strcmp(p->value, "array") == 0);

        /* param scalar => 4 bytes slot
           param array => treat as pointer => 4 bytes slot */
        offset += 4;
        add_local(func, id->value, -offset, 0, SCOPE_PARAM, is_arr);
        (void)size;
    }

    return offset;
}

static int build_locals(ASTNode* local_decls, const char* func, int start_offset_bytes) {
    int offset = start_offset_bytes;

    if (!local_decls || local_decls->type != AST_LOCAL_DECLS) return offset;

    for (ASTNode* d = local_decls->child; d; d = d->sibling) {
        if (d->type != AST_LOCAL_DECL) continue;

        ASTNode* id = NULL;
        ASTNode* size = NULL;
        for (ASTNode* c = d->child; c; c = c->sibling) {
            if (c->type == AST_IDENT) id = c;
            else if (c->type == AST_INT_LITERAL) size = c;
        }
        if (!id || !id->value) continue;

        int bytes = 4;
        int arr = 0;
        if (d->value && strcmp(d->value, "array") == 0 && size && size->value) {
            arr = atoi(size->value);
            bytes = arr * 4;
        }

        offset += bytes;
        add_local(func, id->value, -offset, arr, SCOPE_LOCAL, false);
    }

    return offset;
}

static int align16(int x) { return (x + 15) & ~15; }

/* prologue/epilogue (s0 as fp) */
static void emit_prologue(FILE* out, int frame) {
    /* reserve frame + save ra/s0 (8 bytes) */
    fprintf(out, "    addi sp, sp, -%d\n", frame + 8);
    fprintf(out, "    sw ra, %d(sp)\n", frame + 4);
    fprintf(out, "    sw s0, %d(sp)\n", frame + 0);
    fprintf(out, "    addi s0, sp, %d\n", frame + 8);
}

static void emit_epilogue(FILE* out, int frame) {
    fprintf(out, "    lw s0, %d(sp)\n", frame + 0);
    fprintf(out, "    lw ra, %d(sp)\n", frame + 4);
    fprintf(out, "    addi sp, sp, %d\n", frame + 8);
    fprintf(out, "    jalr x0, 0(ra)\n");
}

/* store incoming a0-a7 into param slots */
static void spill_params(FILE* out) {
    int idx = 0;
    for (Symbol* s = local_head; s; s = s->next) {
        if (s->scope != SCOPE_PARAM) continue;
        if (idx >= 8) break;
        fprintf(out, "    sw a%d, %d(s0)\n", idx, s->offset);
        idx++;
    }
}

/* ============ codegen expressions ============ */
static const char* generate_expression(ASTNode* node, FILE* out);

static const char* load_identifier(const char* name, FILE* out) {
    const char* reg = acquire_temp_register();

    // 1) 先查局部（含形参）
    Symbol* l = find_local(name);
    if (l) {
        // 1.1 局部“真数组”：栈上分配了一段空间，IDENT 应该给出首地址
        if (l->array_size > 0 && !l->is_param_array) {
            // reg = &local_array[0]
            fprintf(out, "    addi %s, s0, %d\n", reg, l->offset);
            return reg;
        }

        // 1.2 参数数组（指针）或普通局部变量：栈槽里存的是“值”
        // 参数数组：值就是 base pointer；普通变量：值就是 int
        fprintf(out, "    lw %s, %d(s0)\n", reg, l->offset);
        return reg;
    }

    // 2) 再查全局
    Symbol* g = find_global(name);
    if (!g) g = add_global(name); // 容错

    // 2.1 全局数组：IDENT 给出地址
    if (g->array_size > 0) {
        fprintf(out, "    auipc %s, %%pcrel_hi(%s)\n", reg, g->label);
        fprintf(out, "    addi  %s, %s, %%pcrel_lo(%s)\n", reg, reg, g->label);
        return reg;
    }

    // 2.2 普通全局变量：先取地址再解引用
    fprintf(out, "    auipc %s, %%pcrel_hi(%s)\n", reg, g->label);
    fprintf(out, "    addi  %s, %s, %%pcrel_lo(%s)\n", reg, reg, g->label);
    fprintf(out, "    lw %s, 0(%s)\n", reg, reg);
    return reg;
}


// static const char* load_identifier(const char* name, FILE* out) {
//     const char* reg = acquire_temp_register();
//     Symbol* l = find_local(name);
//     if (l) {
//         fprintf(out, "    lw %s, %d(s0)\n", reg, l->offset);
//         return reg;
//     }
//     Symbol* g = find_global(name);
//     if (!g) g = add_global(name); /* tolerate */
//     fprintf(out, "    auipc %s, %%pcrel_hi(%s)\n", reg, g->label);
//     fprintf(out, "    addi  %s, %s, %%pcrel_lo(%s)\n", reg, reg, g->label);
//     fprintf(out, "    lw %s, 0(%s)\n", reg, reg);
//     return reg;
// }

static long parse_imm32(const char* lit) {
    char* end = NULL;
    long v = strtol(lit, &end, 10);   // 你的 lit 基本是十进制字符串
    return v;
}

static int fits_i12(long v) {
    return (v >= -2048 && v <= 2047);
}

static const char* load_integer_literal(const char* lit, FILE* out) {
    const char* reg = acquire_temp_register();
    long v = parse_imm32(lit);

    if (fits_i12(v)) {
        // 1条指令：addi
        fprintf(out, "    addi %s, x0, %ld\n", reg, v);
    } else {
        // 2条指令：lui + addi（带 0x800 修正）
        long hi = (v + 0x800) >> 12;
        long lo = v - (hi << 12);   // 保证落在 [-2048, 2047]

        fprintf(out, "    lui  %s, %ld\n", reg, hi);
        fprintf(out, "    addi %s, %s, %ld\n", reg, reg, lo);
    }

    return reg;
}

static void emit_array_base(ASTNode* array_id, FILE* out, const char* base_reg) {
    /* local array: base = s0 + offset
       param array(pointer): load pointer from slot into base_reg
       global: la base_reg, label */
    Symbol* l = find_local(array_id->value);
    if (l) {
        if (l->scope == SCOPE_PARAM && l->is_param_array) {
            fprintf(out, "    lw %s, %d(s0)\n", base_reg, l->offset);
        } else {
            fprintf(out, "    addi %s, s0, %d\n", base_reg, l->offset);
        }
        return;
    }
    Symbol* g = find_global(array_id->value);
    if (!g) g = add_global(array_id->value);
    fprintf(out, "    auipc %s, %%pcrel_hi(%s)\n", base_reg, g->label);
    fprintf(out, "    addi  %s, %s, %%pcrel_lo(%s)\n", base_reg, base_reg, g->label);
}

static const char* generate_binary_expr(ASTNode* node, FILE* out) {
    if (!node->child || !node->child->sibling) return NULL;

    const char* left = generate_expression(node->child, out);
    const char* right = generate_expression(node->child->sibling, out);
    const char* res = acquire_temp_register();

    const char* op = node->op ? node->op : "";

    if (strcmp(op, "+") == 0) fprintf(out, "    add %s, %s, %s\n", res, left, right);
    else if (strcmp(op, "-") == 0) fprintf(out, "    sub %s, %s, %s\n", res, left, right);
    else if (strcmp(op, "*") == 0) fprintf(out, "    mul %s, %s, %s\n", res, left, right);
    else if (strcmp(op, "/") == 0) fprintf(out, "    div %s, %s, %s\n", res, left, right);
    else if (strcmp(op, "%") == 0) fprintf(out, "    rem %s, %s, %s\n", res, left, right);
    else if (strcmp(op, "&") == 0) fprintf(out, "    and %s, %s, %s\n", res, left, right);
    else if (strcmp(op, "|") == 0) fprintf(out, "    or  %s, %s, %s\n", res, left, right);
    else if (strcmp(op, "^") == 0) fprintf(out, "    xor %s, %s, %s\n", res, left, right);
    else if (strcmp(op, "<<") == 0) fprintf(out, "    sll %s, %s, %s\n", res, left, right);
    else if (strcmp(op, ">>") == 0) fprintf(out, "    sra %s, %s, %s\n", res, left, right);
    else if (strcmp(op, "<") == 0) fprintf(out, "    slt %s, %s, %s\n", res, left, right);
    else if (strcmp(op, ">") == 0) fprintf(out, "    slt %s, %s, %s\n", res, right, left);
    else if (strcmp(op, "==") == 0) {
        fprintf(out, "    sub %s, %s, %s\n", res, left, right);
        fprintf(out, "    sltiu %s, %s, 1\n", res, res);
    } else if (strcmp(op, "!=") == 0) {
        fprintf(out, "    sub  %s, %s, %s\n", res, left, right);
        fprintf(out, "    sltu %s, x0, %s\n", res, res);
    } else if (strcmp(op, "<=") == 0) {
        fprintf(out, "    slt %s, %s, %s\n", res, right, left);
        fprintf(out, "    xori %s, %s, 1\n", res, res);
    } else if (strcmp(op, ">=") == 0) {
        fprintf(out, "    slt %s, %s, %s\n", res, left, right);
        fprintf(out, "    xori %s, %s, 1\n", res, res);
    } else if (strcmp(op, "&&") == 0) {
        fprintf(out, "    sltu %s, x0, %s\n", left, left);
        fprintf(out, "    sltu %s, x0, %s\n", right, right);
        fprintf(out, "    and  %s, %s, %s\n", res, left, right);
    } else if (strcmp(op, "||") == 0) {
        fprintf(out, "    or   %s, %s, %s\n", res, left, right);
        fprintf(out, "    sltu %s, x0, %s\n", res, res);
    }

    release_temp_register(right);
    release_temp_register(left);
    return res;
}

static const char* generate_unary_expr(ASTNode* node, FILE* out) {
    if (!node->child) return NULL;
    const char* op = node->op ? node->op : "";

    /* memory deref: $addr */
    if (strcmp(op, "$") == 0) {
        const char* addr = generate_expression(node->child, out);
        const char* res = acquire_temp_register();
        fprintf(out, "    lw %s, 0(%s)\n", res, addr);
        release_temp_register(addr);
        return res;
    }

    const char* x = generate_expression(node->child, out);
    const char* res = acquire_temp_register();

    if (strcmp(op, "-") == 0) fprintf(out, "    sub %s, x0, %s\n", res, x);
    else if (strcmp(op, "!") == 0) fprintf(out, "    sltiu %s, %s, 1\n", res, x);
    else if (strcmp(op, "~") == 0) fprintf(out, "    xori %s, %s, -1\n", res, x);
    else if (strcmp(op, "+") == 0) fprintf(out, "    addi  %s, %s, 0\n", res, x);

    release_temp_register(x);
    return res;
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
            if (!node->child || !node->child->sibling) return NULL;
            ASTNode* array_id = node->child;
            ASTNode* index = node->child->sibling;

            const char* idx = generate_expression(index, out);
            const char* base = acquire_temp_register();
            emit_array_base(array_id, out, base);

            const char* off = acquire_temp_register();
            fprintf(out, "    slli %s, %s, 2\n", off, idx);

            const char* addr = acquire_temp_register();
            fprintf(out, "    add %s, %s, %s\n", addr, base, off);

            const char* res = acquire_temp_register();
            fprintf(out, "    lw %s, 0(%s)\n", res, addr);

            release_temp_register(addr);
            release_temp_register(off);
            release_temp_register(base);
            release_temp_register(idx);
            return res;
        }

        case AST_ASSIGN_EXPR: {
            if (!node->child || !node->child->sibling) return NULL;

            const char* v = generate_expression(node->child->sibling, out);
            if (!v) return NULL;

            /* memory assign: $addr = v */
            if (node->value && strcmp(node->value, "memory") == 0) {
                const char* addr = generate_expression(node->child, out);
                fprintf(out, "    sw %s, 0(%s)\n", v, addr);
                release_temp_register(addr);
                return v;
            }

            /* lhs */
            if (node->child->type == AST_IDENT) {
                const char* name = node->child->value;
                Symbol* l = find_local(name);
                if (l) {
                    fprintf(out, "    sw %s, %d(s0)\n", v, l->offset);
                } else {
                    Symbol* g = find_global(name);
                    if (!g) g = add_global(name);
                    fprintf(out, "    auipc t6, %%pcrel_hi(%s)\n", g->label);
                    fprintf(out, "    addi  t6, t6, %%pcrel_lo(%s)\n", g->label);
                    fprintf(out, "    sw %s, 0(t6)\n", v);
                }
                return v;
            }

            if (node->child->type == AST_ARRAY_ACCESS) {
                ASTNode* aa = node->child;
                if (!aa->child || !aa->child->sibling) return NULL;

                ASTNode* array_id = aa->child;
                ASTNode* index = aa->child->sibling;

                const char* idx = generate_expression(index, out);
                const char* base = acquire_temp_register();
                emit_array_base(array_id, out, base);

                const char* off = acquire_temp_register();
                fprintf(out, "    slli %s, %s, 2\n", off, idx);

                const char* addr = acquire_temp_register();
                fprintf(out, "    add %s, %s, %s\n", addr, base, off);

                fprintf(out, "    sw %s, 0(%s)\n", v, addr);

                release_temp_register(addr);
                release_temp_register(off);
                release_temp_register(base);
                release_temp_register(idx);
                return v;
            }

            return v;
        }

        case AST_FUNC_CALL: {
            /* simplified: pass up to 8 args in a0-a7 */
            if (node->child && node->child->type == AST_ARG_LIST) {
                ASTNode* arg = node->child->child;
                int k = 0;
                while (arg && k < 8) {
                    const char* r = generate_expression(arg, out);
                    if (r) {
                        if (strcmp(r, "a0") != 0) {
                            fprintf(out, "    addi a%d, %s, 0\n", k, r);
                        }
                        release_temp_register(r);
                    }
                    k++;
                    arg = arg->sibling;
                }
            }

            if (node->value) fprintf(out, "    jal ra, %s\n", node->value);

            const char* res = acquire_temp_register();
            fprintf(out, "    addi %s, a0, 0\n", res);
            return res;
        }

        default:
            return NULL;
    }
}

/* ============ codegen statements ============ */
static void generate_statement(ASTNode* node, FILE* out, bool* has_return);

static void generate_statement_list(ASTNode* node, FILE* out, bool* has_return) {
    if (!node) return;
    for (ASTNode* s = node->child; s; s = s->sibling)
        generate_statement(s, out, has_return);
}

static void generate_return_statement(ASTNode* node, FILE* out, bool* has_return) {
    *has_return = true;

    if (node->child) {
        const char* r = generate_expression(node->child, out);
        fprintf(out, "    addi a0, %s, 0\n", r);
        release_temp_register(r);
    } else {
        fprintf(out, "    addi a0, x0, 0\n");
    }
    fprintf(out, "    jal x0, %s\n", current_ret_label);
}

static void generate_if_statement(ASTNode* node, FILE* out, bool* has_return) {
    if (!node->child) return;

    char* else_label = generate_label(".L_else");
    char* end_label  = generate_label(".L_end_if");

    const char* cond = generate_expression(node->child, out);
    bool has_else = (node->child->sibling && node->child->sibling->sibling);

    fprintf(out, "    beq %s, x0, %s\n", cond, has_else ? else_label : end_label);
    release_temp_register(cond);

    if (node->child->sibling)
        generate_statement(node->child->sibling, out, has_return);

    if (has_else) {
        fprintf(out, "    jal x0, %s\n", end_label);
        fprintf(out, "%s:\n", else_label);
        generate_statement(node->child->sibling->sibling, out, has_return);
    }

    fprintf(out, "%s:\n", end_label);
    free(else_label);
    free(end_label);
}

static void generate_while_statement(ASTNode* node, FILE* out, bool* has_return) {
    if (!node->child) return;

    char* loop_label = generate_label(".lop");
    char* end_label  = generate_label(".end");

    fprintf(out, "%s:\n", loop_label);

    const char* cond = generate_expression(node->child, out);
    fprintf(out, "    beq %s, x0, %s\n", cond, end_label);
    release_temp_register(cond);

    if (node->child->sibling)
        generate_statement(node->child->sibling, out, has_return);

    fprintf(out, "    jal x0, %s\n", loop_label);
    fprintf(out, "%s:\n", end_label);

    free(loop_label);
    free(end_label);
}

static void generate_statement(ASTNode* node, FILE* out, bool* has_return) {
    if (!node) return;

    switch (node->type) {
        case AST_EXPR_STMT: {
            if (node->child) {
                const char* r = generate_expression(node->child, out);
                if (r) release_temp_register(r);
            }
            break;
        }
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
        case AST_BREAK_STMT:
            /* TODO */
            break;
        default:
            break;
    }
}

/* ============ emit sections ============ */
static void emit_data_section(FILE* out) {
    fprintf(out, ".data\n");
    for (Symbol* s = global_head; s; s = s->next) {
        if (s->array_size > 0) {
            fprintf(out, "%s: .word", s->label);
            for (int i = 0; i < s->array_size; i++) {
                fprintf(out, " 0%s", (i == s->array_size - 1) ? "" : ",");
            }
            fprintf(out, "\n");
        } else {
            fprintf(out, "%s: .word 0\n", s->label);
        }
    }
    fprintf(out, "\n");
}

static void emit_text_section(ASTNode* root, FILE* out) {
    fprintf(out, ".text\n");
    if (!root || !root->child || root->child->type != AST_DECL_LIST) return;

    for (ASTNode* decl = root->child->child; decl; decl = decl->sibling) {
        if (decl->type != AST_FUN_DECL) continue;

        ASTNode* type_node  = decl->child;
        ASTNode* ident_node = type_node ? type_node->sibling : NULL;
        ASTNode* params     = ident_node ? ident_node->sibling : NULL;
        ASTNode* compound   = params ? params->sibling : NULL;

        if (!compound || compound->type != AST_COMPOUND_STMT) continue;
        if (!ident_node || ident_node->type != AST_IDENT || !ident_node->value) continue;

        const char* fname = ident_node->value;

        if (strcmp(fname, "main") == 0) fprintf(out, ".globl main\n");
        fprintf(out, "%s:\n", fname);

        /* compound children: local_decls, stmt_list */
        ASTNode* local_decls = compound->child; /* AST_LOCAL_DECLS */
        ASTNode* stmt_list   = local_decls ? local_decls->sibling : NULL; /* AST_STMT_LIST */

        clear_locals();

        int used = 0;
        used = build_params(params, fname, used);
        used = build_locals(local_decls, fname, used);
        int frame = align16(used);

        char* ret_label = (char*)malloc(96);
        snprintf(ret_label, 96, ".L_ret_%s_%d", fname, label_counter++);
        current_ret_label = ret_label;

        emit_prologue(out, frame);
        spill_params(out);

        bool has_return = false;
        if (stmt_list && stmt_list->type == AST_STMT_LIST) {
            generate_statement_list(stmt_list, out, &has_return);
        }

        if (!has_return) {
            fprintf(out, "    addi a0, x0, 0\n");
            fprintf(out, "    jal x0, %s\n", current_ret_label);
        }

        fprintf(out, "%s:\n", current_ret_label);
        emit_epilogue(out, frame);

        fprintf(out, "\n");
        free(ret_label);
        current_ret_label = NULL;
        clear_locals();
    }
}

/* ============ entry ============ */
static void generate_assembly(ASTNode* root) {
    if (!root) return;

    FILE* out = fopen("output.asm", "w");
    if (!out) {
        fprintf(stderr, "Error: cannot open output.asm\n");
        return;
    }

    collect_globals(root);
    emit_data_section(out);
    emit_text_section(root, out);

    fclose(out);

    free_globals();
}

/* bison/flex */
extern int yylex();
extern int yylineno;
extern char* yytext;

void yyerror(const char* s) {
    fprintf(stderr, "Error at line %d: %s\n", yylineno, s);
}

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

type_spec : VOID { $$ = create_node(AST_TYPE_SPEC, "void"); }
| INT { $$ = create_node(AST_TYPE_SPEC, "int"); };

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
| VOID { $$ = create_node(AST_PARAMS, "void"); };

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
| '(' expr ')' { $$ = $2; }
| IDENT { $$ = create_node(AST_IDENT, $1); }
| IDENT '[' expr ']' {
    $$ = create_node(AST_ARRAY_ACCESS, NULL);
    add_child($$, create_node(AST_IDENT, $1));
    add_child($$, $3);
}
| IDENT '(' args ')' {
    $$ = create_node(AST_FUNC_CALL, $1);
    add_child($$, $3);
}
| int_literal { $$ = $1; }
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
| { $$ = create_node(AST_ARG_LIST, NULL); };

continue_stmt : CONTINUE ';' { $$ = create_node(AST_CONTINUE_STMT, NULL); };
break_stmt : BREAK ';' { $$ = create_node(AST_BREAK_STMT, NULL); };

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
