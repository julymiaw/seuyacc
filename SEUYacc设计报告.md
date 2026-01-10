## 功能描述

SEUYacc 是一个类似于 Yacc/Bison 的 LR(1) 语法分析器生成工具。它能够：
- 读取语法定义文件（.y），解析语法规则、终结符、非终结符和语义动作
- **完整支持语义动作（Semantic Actions）**：在每条产生式后支持用 `{}` 包裹的 C 代码块，实现语法制导翻译
- 基于文法构造 LR(1) 项目集和状态转换表
- 生成完整的 C 语言解析器代码（y.tab.c 和 y.tab.h）
- 支持优先级与结合性声明（%left, %right, %nonassoc）
- 支持语义值类型声明（%union, %type, %token）
- **生成 JSON 格式的错误诊断报告**：便于 IDE 集成和错误展示
- 提供可视化输出（PlantUML 状态图和 Markdown 分析表）

核心功能模块包括：
- **YaccParser**：解析 .y 文件，提取符号表、产生式和代码段，智能处理嵌套花括号、字符串和注释
- **LRGenerator**：构造 LR(1) 项目集族，生成 ACTION/GOTO 表
- **代码生成器**：将分析表和用户代码整合成可编译的解析器，自动处理语义动作中的 `$$` 和 `$N` 引用

## 主要特色

### 1. 语义动作支持 —— AST 构建的基石
SEUYacc 的核心特色之一是**完整支持语法规则中的语义动作**。在每条产生式后，用户可以用 `{}` 包裹任意 C 代码，这些代码会在规约时自动执行。正是这一特性，使得我们能够在语法分析过程中同步构建抽象语法树。

**智能解析语义动作块**：
```cpp
bool YaccParser::parseSemanticAction(std::string& buffer, size_t& pos, std::string& action) {
    // 智能识别花括号配对，正确处理：
    // - 字符串中的花括号："{ not a brace }"
    // - 注释中的花括号：// { or /* { */
    // - 转义字符：'\{'
    // - 嵌套花括号：{ { nested } }
    
    int brace_count = 1;
    while (brace_count > 0) {
        // 跳过字符串、注释中的假花括号
        // 正确计数真实的语义动作边界
    }
}
```

**语义值引用替换**：
在代码生成阶段，自动将语义动作中的特殊符号转换为实际的栈操作：
- `$$`：左部非终结符的语义值（如 `$$` → `yyval.node`）
- `$1, $2, $3...`：右部符号的语义值（如 `$1` → `yyvsp[-2].node`）

示例语法规则：
```yacc
expr : expr '+' expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);  // $$ 自动替换为 yyval.node
    $$->op = strdup("+");
    add_child($$, $1);  // $1 自动替换为 yyvsp[-2].node
    add_child($$, $3);  // $3 自动替换为 yyvsp[0].node
}
```

### 2. 完整的 LR(1) 分析表生成
- 精确计算每个项目的 FOLLOW 集和向前看符号
- 自动处理移进-规约冲突和规约-规约冲突
- 根据优先级和结合性规则解决冲突

### 3. IDE 友好的错误诊断系统
SEUYacc 生成的解析器具备**结构化错误报告**功能，能够输出标准 JSON 格式的错误诊断信息，方便与现代 IDE 集成：

**JSON 错误报告格式**：
```json
{
  "errors": [
    {
      "line": 5,
      "message": "syntax error",
      "actual": "IDENT",
      "expected": [";", ")", "]"]
    }
  ],
  "errorCount": 1
}
```

**生成的错误处理代码**：
```c
static void print_errors_json(void) {
    printf("{\n");
    printf("  \"errors\": [\n");
    for (int i = 0; i < error_count; i++) {
        ErrorInfo* err = &errors[i];
        printf("    {\n");
        printf("      \"line\": %d,\n", err->line);
        printf("      \"message\": \"%s\",\n", err->message);
        if (err->actual_token) {
            printf("      \"actual\": \"%s\",\n", err->actual_token);
        }
        printf("      \"expected\": [");
        for (int j = 0; j < err->expected_count; j++) {
            printf("\"%s\"", err->expected_tokens[j]);
            if (j < err->expected_count - 1) printf(", ");
        }
        printf("]\n");
        printf("    }%s\n", i < error_count - 1 ? "," : "");
    }
    printf("  ],\n");
    printf("  \"errorCount\": %d\n", error_count);
    printf("}\n");
}
```

这使得 VS Code、Vim 等编辑器可以直接解析错误信息，在源码中标注错误位置和提供修复建议。项目中的 `test_error_recovery.sh` 脚本演示了如何自动生成和使用这些 JSON 报告。

### 4. Mini-C 编译器实现
本项目的核心应用是基于 SEUYacc 实现的 Mini-C 到 RISC-V 汇编的编译器，具备：
- **抽象语法树（AST）构建**：在语法分析过程中构造完整的程序语法树
- **符号表管理**：区分全局和局部作用域，支持数组和函数参数
- **RISC-V 32I 代码生成**：直接将 AST 翻译为标准 RISC-V 汇编指令
- **寄存器分配**：使用临时寄存器池管理表达式计算
- **函数调用约定**：实现标准的栈帧管理和参数传递

### 5. 可扩展的架构
- 支持嵌入式 C 代码段，可在语法动作中执行任意操作
- 清晰的模块划分，便于扩展新的目标代码生成后端
- 提供可视化工具，便于调试语法分析表

## 设计与特色概述（含关键代码）

### 第一部分：SEUYacc 工具的功能实现

SEUYacc 作为 LR(1) 语法分析器生成器，其核心功能包括：

#### 1. 语法文件解析（YaccParser）

该模块负责读取 .y 文件，提取：
- **符号表**：终结符和非终结符的定义及其属性
- **产生式集合**：每条规则的左部、右部和语义动作代码
- **代码段**：用户定义的 C 代码（声明、函数等）

**关键功能：语义动作解析**

这是 SEUYacc 最核心的特性之一。每条产生式后可以跟随用 `{}` 包裹的 C 代码，称为语义动作。解析器需要正确识别语义动作的边界，这并不简单，因为花括号可能出现在：

```cpp
bool YaccParser::parseSemanticAction(std::string& buffer, size_t& pos, std::string& action) {
    int brace_count = 1;
    char quote_type = 0;      // 记录是否在字符串中
    bool in_line_comment = false;   // 是否在 // 注释中
    bool in_block_comment = false;  // 是否在 /* */ 注释中
    
    while (pos < buffer.size()) {
        // 跳过字符串内的花括号：printf("{ not real brace }");
        if (c == '\'' || c == '\"') {
            quote_type = (quote_type == c) ? 0 : c;
        }
        
        // 跳过注释中的花括号：// { or /* { */
        if (!quote_type && c == '/' && next_c == '/') {
            in_line_comment = true;
        }
        
        // 只在真实代码中计数花括号
        if (!quote_type && !in_line_comment && !in_block_comment) {
            if (c == '{') brace_count++;
            else if (c == '}') {
                brace_count--;
                if (brace_count == 0) {
                    // 找到匹配的右花括号，语义动作结束
                    action = buffer.substr(start_pos, pos - start_pos + 1);
                    return true;
                }
            }
        }
    }
}
```

**语义动作示例**（minic.y 中的实际代码）：

```yacc
expr : expr '+' expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("+");
    add_child($$, $1);
    add_child($$, $3);
}
```

这段代码在解析到 `expr + expr` 时会自动执行，创建一个二元表达式节点并挂载两个子节点。正是**语义动作机制**，让我们能在语法分析的同时构建完整的 AST。

#### 2. LR(1) 项目集构造（LRGenerator）
```cpp
void LRGenerator::generateTable() {
    // 计算项目集闭包
    // 构造状态转换图
    // 生成 ACTION/GOTO 表
    // 处理冲突
}
```

核心算法包括：
- **闭包计算**：对每个 LR(1) 项目 `[A → α·Bβ, a]`，将 `B` 的所有产生式加入闭包
- **GOTO 函数**：计算状态在读入某个符号后的转移目标
- **ACTION 表**：决定每个状态下遇到终结符时的动作（移进/规约/接受/报错）
- **GOTO 表**：记录非终结符的状态转移

#### 3. 解析器代码生成

生成的 y.tab.c 包含完整的解析器实现，核心部分包括：

**语义值栈管理**：
```c
int yyparse() {
    int yystate = 0;
    YYSTYPE yylval;       // 当前符号的语义值
    YYSTYPE yyval;        // 规约后左部符号的语义值
    YYSTYPE* yyvsp;       // 语义值栈指针
    
    // 主解析循环
    while (1) {
        // 根据 ACTION 表决定动作
        if (action > 0) {
            // 移进：将当前 token 及其语义值压栈
            *++yyvsp = yylval;
        } else if (action < 0) {
            // 规约：执行对应产生式的语义动作
            switch (-action) {
                case 1:  // expr : expr '+' expr
                    yyval.node = create_node(AST_BINARY_EXPR, NULL);
                    yyval.node->op = strdup("+");
                    add_child(yyval.node, yyvsp[-2].node);  // $1
                    add_child(yyval.node, yyvsp[0].node);   // $3
                    break;
                // ... 其他规则的语义动作
            }
        }
    }
}
```

**语义动作代码处理**（LRGenerator::processSemanticAction）：

系统会自动将语义动作中的 `$$` 和 `$N` 转换为实际的栈操作：

```cpp
std::string LRGenerator::processSemanticAction(const std::string& action, 
                                               const Production& prod) const {
    std::string processed = action;
    
    // 替换 $$ 为 yyval.type
    // 例如：$$ -> yyval.node（如果声明了 %type <node> expr）
    processed = replace(processed, "$$", "yyval." + prod.left.value_type);
    
    // 替换 $N 为 yyvsp[offset].type
    // $1 -> yyvsp[-(prod.right.size()-1)].type
    // $2 -> yyvsp[-(prod.right.size()-2)].type
    // ...
    for (int i = 1; i <= prod.right.size(); i++) {
        int offset = -(prod.right.size() - i);
        std::string dollar_n = "$" + std::to_string(i);
        std::string replacement = "yyvsp[" + std::to_string(offset) + "]." 
                                + prod.right[i-1].value_type;
        processed = replace(processed, dollar_n, replacement);
    }
    
    return processed;
}
```

**错误报告生成**：

解析器还包含结构化错误报告功能，会生成 JSON 格式的诊断信息：

```c
static void print_errors_json(void) {
    printf("{\n");
    printf("  \"errors\": [\n");
    for (int i = 0; i < error_count; i++) {
        printf("    {\n");
        printf("      \"line\": %d,\n", errors[i].line);
        printf("      \"message\": \"%s\",\n", errors[i].message);
        printf("      \"actual\": \"%s\",\n", errors[i].actual_token);
        printf("      \"expected\": [");
        for (int j = 0; j < errors[i].expected_count; j++) {
            printf("\"%s\"%s", errors[i].expected_tokens[j],
                   j < errors[i].expected_count - 1 ? ", " : "");
        }
        printf("]\n");
        printf("    }%s\n", i < error_count - 1 ? "," : "");
    }
    printf("  ],\n");
    printf("  \"errorCount\": %d\n", error_count);
    printf("}\n");
}
```

这些 JSON 报告可以被 IDE 解析，用于在编辑器中高亮错误位置、显示期望的符号列表等。

### 第二部分：Mini-C 编译器的实现

在同学提供 RISC-V 32I 指令集规范和目标汇编格式后，我通过编写 `minic.y` 完成了从 Mini-C 源码到汇编代码的完整编译流程。

#### 第一阶段：抽象语法树（AST）的生成

##### 1. AST 节点类型设计
在 minic.y 的 C 代码段中定义了完整的 AST 节点类型枚举：

```c
typedef enum {
    AST_PROGRAM,        // 程序根节点
    AST_DECL_LIST,      // 声明列表
    AST_VAR_DECL,       // 变量声明
    AST_FUN_DECL,       // 函数声明
    AST_COMPOUND_STMT,  // 复合语句
    AST_EXPR_STMT,      // 表达式语句
    AST_IF_STMT,        // if语句
    AST_WHILE_STMT,     // while循环
    AST_RETURN_STMT,    // return语句
    AST_BINARY_EXPR,    // 二元表达式
    AST_UNARY_EXPR,     // 一元表达式
    AST_ASSIGN_EXPR,    // 赋值表达式
    AST_ARRAY_ACCESS,   // 数组访问
    AST_FUNC_CALL,      // 函数调用
    AST_IDENT,          // 标识符
    AST_INT_LITERAL,    // 整数字面量
    // ... 其他类型
} NodeType;
```

AST 节点结构采用**子节点-兄弟节点**表示法，便于表达任意数量的子树：

```c
typedef struct ASTNode {
    NodeType type;              // 节点类型
    char* value;                // 节点值（标识符名、字面量等）
    char* op;                   // 运算符（用于表达式节点）
    struct ASTNode* child;      // 第一个子节点
    struct ASTNode* sibling;    // 下一个兄弟节点
} ASTNode;
```

##### 2. 语法规则中的 AST 构建

在每条语法规则的动作中，通过 `create_node` 和 `add_child` 构建 AST。以二元表达式为例：

```yacc
expr : expr '+' expr {
    $$ = create_node(AST_BINARY_EXPR, NULL);
    $$->op = strdup("+");      // 记录运算符
    add_child($$, $1);         // 添加左操作数
    add_child($$, $3);         // 添加右操作数
}
```

函数声明的 AST 构建：

```yacc
fun_decl : type_spec IDENT '(' params ')' compound_stmt {
    $$ = create_node(AST_FUN_DECL, NULL);
    add_child($$, $1);                        // 返回类型
    add_child($$, create_node(AST_IDENT, $2)); // 函数名
    add_child($$, $4);                        // 参数列表
    add_child($$, $6);                        // 函数体
}
```

数组访问表达式：

```yacc
expr : IDENT '[' expr ']' {
    $$ = create_node(AST_ARRAY_ACCESS, NULL);
    add_child($$, create_node(AST_IDENT, $1)); // 数组名
    add_child($$, $3);                         // 索引表达式
}
```

##### 3. 符号表构建

在 AST 构建的同时，进行符号收集。符号分为两类：

```c
typedef enum { SCOPE_GLOBAL, SCOPE_LOCAL, SCOPE_PARAM } Scope;

typedef struct Symbol {
    char* name;          // 符号名称
    Scope scope;         // 作用域
    int offset;          // 栈偏移（局部变量）
    int array_size;      // 数组大小（0表示标量）
    bool is_param_array; // 是否为数组参数（作为指针传递）
    struct Symbol* next; // 链表连接
} Symbol;
```

全局符号的收集（遍历 AST）：

```c
static void collect_globals(ASTNode* node) {
    if (node->type == AST_VAR_DECL) {
        // 提取变量名和数组大小
        Symbol* g = add_global(id->value);
        if (node->value && strcmp(node->value, "array") == 0)
            g->array_size = atoi(size->value);
    }
    collect_globals(node->child);   // 递归子节点
    collect_globals(node->sibling); // 递归兄弟节点
}
```

#### 第二阶段：从 AST 生成 RISC-V 汇编代码

##### 1. 代码生成总体流程

汇编生成分为两个主要段：

```c
static void generate_assembly(ASTNode* root) {
    FILE* out = fopen("output.asm", "w");
    
    collect_globals(root);       // 收集全局变量
    emit_data_section(out);      // 生成 .data 段
    emit_text_section(root, out); // 生成 .text 段
    
    fclose(out);
}
```

##### 2. 数据段生成

全局变量和数组在 `.data` 段中分配：

```c
static void emit_data_section(FILE* out) {
    fprintf(out, ".data\n");
    for (Symbol* s = global_head; s; s = s->next) {
        if (s->array_size > 0) {
            // 数组：分配连续空间
            fprintf(out, "%s: .word", s->label);
            for (int i = 0; i < s->array_size; i++)
                fprintf(out, " 0%s", (i == s->array_size - 1) ? "" : ",");
            fprintf(out, "\n");
        } else {
            // 标量变量
            fprintf(out, "%s: .word 0\n", s->label);
        }
    }
}
```

生成的汇编示例（假设有全局数组 `int arr[5]` 和全局变量 `int x`）：
```asm
.data
var_arr: .word 0, 0, 0, 0, 0
var_x: .word 0
```

##### 3. 函数代码生成

每个函数生成包括：
- **栈帧管理**：prologue 和 epilogue
- **参数传递**：将寄存器参数 `a0-a7` 存入栈
- **语句翻译**：递归处理 AST 语句节点

函数框架生成代码：

```c
static void emit_text_section(ASTNode* root, FILE* out) {
    for (ASTNode* decl = root->child->child; decl; decl = decl->sibling) {
        if (decl->type != AST_FUN_DECL) continue;
        
        const char* fname = ident_node->value;
        
        // main 函数需要全局标记
        if (strcmp(fname, "main") == 0) 
            fprintf(out, ".globl main\n");
        fprintf(out, "%s:\n", fname);
        
        // 构建局部符号表（参数+局部变量）
        int frame = build_stack_frame(params, local_decls, fname);
        
        emit_prologue(out, frame);    // 保存 ra/s0，设置栈指针
        spill_params(out);            // 存储寄存器参数
        
        // 生成函数体语句
        generate_statement_list(stmt_list, out, &has_return);
        
        // 如果没有显式 return，添加默认返回
        if (!has_return) {
            fprintf(out, "    addi a0, x0, 0\n");
            fprintf(out, "    jal x0, %s\n", ret_label);
        }
        
        // 返回标签和 epilogue
        fprintf(out, "%s:\n", ret_label);
        emit_epilogue(out, frame);    // 恢复 ra/s0，释放栈
    }
}
```

Prologue 生成（标准 RISC-V 调用约定）：

```c
static void emit_prologue(FILE* out, int frame) {
    fprintf(out, "    addi sp, sp, -%d\n", frame + 8);  // 分配栈空间
    fprintf(out, "    sw ra, %d(sp)\n", frame + 4);     // 保存返回地址
    fprintf(out, "    sw s0, %d(sp)\n", frame + 0);     // 保存帧指针
    fprintf(out, "    addi s0, sp, %d\n", frame + 8);   // 设置帧指针
}
```

##### 4. 表达式代码生成

表达式生成采用**后序遍历**，返回结果所在的临时寄存器：

**标识符加载**（区分全局/局部，标量/数组）：

```c
static const char* load_identifier(const char* name, FILE* out) {
    const char* reg = acquire_temp_register();
    
    // 查找局部变量
    Symbol* l = find_local(name);
    if (l) {
        if (l->array_size > 0 && !l->is_param_array) {
            // 局部数组：返回首地址
            fprintf(out, "    addi %s, s0, %d\n", reg, l->offset);
        } else {
            // 普通局部变量：加载值
            fprintf(out, "    lw %s, %d(s0)\n", reg, l->offset);
        }
        return reg;
    }
    
    // 查找全局变量
    Symbol* g = find_global(name);
    if (g->array_size > 0) {
        // 全局数组：使用 PC-relative 寻址获取地址
        fprintf(out, "    auipc %s, %%pcrel_hi(%s)\n", reg, g->label);
        fprintf(out, "    addi  %s, %s, %%pcrel_lo(%s)\n", reg, reg, g->label);
    } else {
        // 全局标量：先获取地址再解引用
        fprintf(out, "    auipc %s, %%pcrel_hi(%s)\n", reg, g->label);
        fprintf(out, "    addi  %s, %s, %%pcrel_lo(%s)\n", reg, reg, g->label);
        fprintf(out, "    lw %s, 0(%s)\n", reg, reg);
    }
    return reg;
}
```

**整数字面量加载**（处理大立即数）：

```c
static const char* load_integer_literal(const char* lit, FILE* out) {
    const char* reg = acquire_temp_register();
    long v = parse_imm32(lit);
    
    if (fits_i12(v)) {
        // 12位立即数：单条指令
        fprintf(out, "    addi %s, x0, %ld\n", reg, v);
    } else {
        // 32位立即数：lui + addi 组合
        long hi = (v + 0x800) >> 12;
        long lo = v - (hi << 12);
        fprintf(out, "    lui  %s, %ld\n", reg, hi);
        fprintf(out, "    addi %s, %s, %ld\n", reg, reg, lo);
    }
    return reg;
}
```

**二元表达式生成**：

```c
static const char* generate_binary_expr(ASTNode* node, FILE* out) {
    // 递归计算左右操作数
    const char* left = generate_expression(node->child, out);
    const char* right = generate_expression(node->child->sibling, out);
    const char* res = acquire_temp_register();
    
    const char* op = node->op;
    
    // 根据运算符选择指令
    if (strcmp(op, "+") == 0) 
        fprintf(out, "    add %s, %s, %s\n", res, left, right);
    else if (strcmp(op, "-") == 0) 
        fprintf(out, "    sub %s, %s, %s\n", res, left, right);
    else if (strcmp(op, "*") == 0) 
        fprintf(out, "    mul %s, %s, %s\n", res, left, right);
    else if (strcmp(op, "==") == 0) {
        fprintf(out, "    sub %s, %s, %s\n", res, left, right);
        fprintf(out, "    sltiu %s, %s, 1\n", res, res); // res = (res == 0)
    }
    else if (strcmp(op, "<=") == 0) {
        fprintf(out, "    slt %s, %s, %s\n", res, right, left); // res = (right < left)
        fprintf(out, "    xori %s, %s, 1\n", res, res);         // res = !(right < left)
    }
    // ... 其他运算符
    
    release_temp_register(right);
    release_temp_register(left);
    return res;
}
```

**数组访问生成**：

```c
case AST_ARRAY_ACCESS: {
    ASTNode* array_id = node->child;
    ASTNode* index = node->child->sibling;
    
    // 计算索引
    const char* idx = generate_expression(index, out);
    
    // 获取数组基地址
    const char* base = acquire_temp_register();
    emit_array_base(array_id, out, base);
    
    // 计算字节偏移（index * 4）
    const char* off = acquire_temp_register();
    fprintf(out, "    slli %s, %s, 2\n", off, idx);
    
    // 计算实际地址
    const char* addr = acquire_temp_register();
    fprintf(out, "    add %s, %s, %s\n", addr, base, off);
    
    // 加载数组元素
    const char* res = acquire_temp_register();
    fprintf(out, "    lw %s, 0(%s)\n", res, addr);
    
    // 释放临时寄存器
    release_temp_register(addr);
    release_temp_register(off);
    release_temp_register(base);
    release_temp_register(idx);
    return res;
}
```

**函数调用生成**：

```c
case AST_FUNC_CALL: {
    // 计算参数并放入 a0-a7
    if (node->child && node->child->type == AST_ARG_LIST) {
        ASTNode* arg = node->child->child;
        int k = 0;
        while (arg && k < 8) {
            const char* r = generate_expression(arg, out);
            fprintf(out, "    addi a%d, %s, 0\n", k, r);
            release_temp_register(r);
            k++;
            arg = arg->sibling;
        }
    }
    
    // 调用函数
    fprintf(out, "    jal ra, %s\n", node->value);
    
    // 获取返回值
    const char* res = acquire_temp_register();
    fprintf(out, "    addi %s, a0, 0\n", res);
    return res;
}
```

##### 5. 语句代码生成

**IF 语句**：

```c
static void generate_if_statement(ASTNode* node, FILE* out, bool* has_return) {
    char* else_label = generate_label(".L_else");
    char* end_label  = generate_label(".L_end_if");
    
    // 计算条件表达式
    const char* cond = generate_expression(node->child, out);
    bool has_else = (node->child->sibling && node->child->sibling->sibling);
    
    // 条件为假时跳转
    fprintf(out, "    beq %s, x0, %s\n", cond, has_else ? else_label : end_label);
    release_temp_register(cond);
    
    // then 分支
    generate_statement(node->child->sibling, out, has_return);
    
    if (has_else) {
        fprintf(out, "    jal x0, %s\n", end_label);
        fprintf(out, "%s:\n", else_label);
        // else 分支
        generate_statement(node->child->sibling->sibling, out, has_return);
    }
    
    fprintf(out, "%s:\n", end_label);
}
```

**WHILE 循环**：

```c
static void generate_while_statement(ASTNode* node, FILE* out, bool* has_return) {
    char* loop_label = generate_label(".lop");
    char* end_label  = generate_label(".end");
    
    fprintf(out, "%s:\n", loop_label);
    
    // 计算循环条件
    const char* cond = generate_expression(node->child, out);
    fprintf(out, "    beq %s, x0, %s\n", cond, end_label);
    release_temp_register(cond);
    
    // 循环体
    generate_statement(node->child->sibling, out, has_return);
    
    // 跳回循环开始
    fprintf(out, "    jal x0, %s\n", loop_label);
    fprintf(out, "%s:\n", end_label);
}
```

##### 6. 实际生成示例

以 fibonacci 函数为例，源代码：

```c
int fib(int n) {
    if(n<=0) return 0;
    if(n==1||n==2) return 1;
    else return fib(n-1)+fib(n-2);
}
```

生成的汇编代码：

```asm
fib:
    addi sp, sp, -24          # 分配栈帧
    sw ra, 20(sp)             # 保存返回地址
    sw s0, 16(sp)             # 保存帧指针
    addi s0, sp, 24           # 设置帧指针
    sw a0, -4(s0)             # 存储参数 n
    
    lw t0, -4(s0)             # 加载 n
    addi t1, x0, 0            # 常量 0
    slt t2, t1, t0            # t2 = (0 < n)
    xori t2, t2, 1            # t2 = !(0 < n) = (n <= 0)
    beq t2, x0, .L_end_if2    # 如果 n > 0，跳过
    
    addi t0, x0, 0            # return 0
    addi a0, t0, 0
    jal x0, .L_ret_fib_0
    
.L_end_if2:
    lw t0, -4(s0)             # 加载 n
    addi t1, x0, 1            # 常量 1
    sub t2, t0, t1            # t2 = n - 1
    sltiu t2, t2, 1           # t2 = (n-1 == 0) = (n == 1)
    
    lw t1, -4(s0)             # 加载 n
    addi t2, x0, 2            # 常量 2
    sub t3, t1, t2            # t3 = n - 2
    sltiu t3, t3, 1           # t3 = (n == 2)
    
    or t2, t2, t3             # t2 = (n==1) || (n==2)
    sltu t2, x0, t2           # 转换为布尔值
    beq t2, x0, .L_else3      # 如果为假，跳转到 else
    
    addi t0, x0, 1            # return 1
    addi a0, t0, 0
    jal x0, .L_ret_fib_0
    
.L_else3:
    lw t0, -4(s0)             # 加载 n
    addi t1, x0, 1
    sub t2, t0, t1            # t2 = n - 1
    addi a0, t2, 0            # 准备参数
    jal ra, fib               # 递归调用 fib(n-1)
    addi t0, a0, 0            # 保存结果
    
    lw t1, -4(s0)             # 加载 n
    addi t2, x0, 2
    sub t3, t1, t2            # t3 = n - 2
    addi a0, t3, 0            # 准备参数
    jal ra, fib               # 递归调用 fib(n-2)
    addi t1, a0, 0            # 保存结果
    
    add t2, t0, t1            # t2 = fib(n-1) + fib(n-2)
    addi a0, t2, 0            # 设置返回值
    jal x0, .L_ret_fib_0
    
.L_ret_fib_0:
    lw s0, 16(sp)             # 恢复帧指针
    lw ra, 20(sp)             # 恢复返回地址
    addi sp, sp, 24           # 释放栈帧
    jalr x0, 0(ra)            # 返回
```

### 文件夹结构

```
seuyacc/
├── include/seuyacc/         # 头文件
│   ├── parser.h             # Yacc 文件解析器
│   ├── lr_generator.h       # LR(1) 分析表生成器
│   ├── lr_item.h            # LR(1) 项目定义
│   ├── production.h         # 产生式定义
│   └── symbol.h             # 符号定义
├── src/                     # 源代码实现
│   ├── main.cpp             # 程序入口
│   ├── parser.cpp           # Yacc 解析实现
│   ├── lr_generator.cpp     # LR(1) 生成器实现
│   └── lr_item.cpp          # 项目集操作实现
├── examples/                # 示例语法文件
│   ├── minic.y              # Mini-C 编译器语法定义
│   ├── minic.l              # Mini-C 词法定义
│   ├── fibonacci.c          # 测试用 C 源码
│   └── rv32i.js             # RISC-V 指令集文档
├── build/                   # 构建输出目录
│   ├── seuyacc              # 编译后的工具
│   ├── minic_parser         # 生成的 Mini-C 解析器
│   └── *.asm                # 生成的汇编代码
└── xmake.lua                # 构建配置文件
```

## 课程设计总结

### 设计总结

本次课程设计成功实现了一个功能完整的 LR(1) 语法分析器生成工具 SEUYacc，并基于此构建了 Mini-C 到 RISC-V 汇编的编译器。主要成果包括：

1. **理论与实践结合**：深入理解了 LR(1) 文法分析原理，包括项目集闭包计算、状态转换图构造、冲突处理等核心算法。

2. **完整的编译流程**：从词法分析（Flex）、语法分析（SEUYacc）、语义分析（AST 构建）到代码生成（RISC-V 汇编），完成了编译器的全链条实现。

3. **实用的工具设计**：SEUYacc 不仅能处理 Mini-C，还能生成可视化输出（PlantUML、Markdown），便于调试和理解分析表结构。

4. **规范的代码生成**：严格遵循 RISC-V 调用约定，生成的汇编代码结构清晰，支持递归调用、数组操作、表达式计算等复杂场景。

### 还需改进的内容

1. **错误恢复机制**：目前对语法错误的处理较为简单，可以增加错误恢复策略，生成更友好的错误信息。

2. **代码优化**：生成的汇编代码未进行优化，存在冗余指令和寄存器使用浪费，可以加入窥孔优化、死代码消除等技术。

3. **寄存器分配**：当前使用简单的临时寄存器池，复杂表达式可能导致寄存器不足，应实现寄存器溢出到栈的机制。

4. **语言特性扩展**：可以支持更多 C 语言特性，如结构体、指针运算、多维数组、类型转换等。

5. **LALR(1) 优化**：当前使用 LR(1)，状态数较多，可以实现 LALR(1) 以减少内存占用。

### 收获

1. **编译原理深化**：通过实际编码加深了对文法分析、语法制导翻译、符号表管理等概念的理解，不再停留在理论层面。

2. **工程能力提升**：学会了模块化设计、数据结构选择、代码复用等软件工程实践，提高了 C++ 编程水平。

3. **问题解决能力**：在实现过程中遇到了很多具体问题（如 RISC-V 立即数限制、数组参数传递、符号作用域管理），通过查阅资料和调试逐一解决。

4. **团队协作体验**：在同学提供 RISC-V 指令集规范后，我能够独立完成编译器后端实现，体会到了分工协作的重要性。

5. **成就感**：看到自己编写的编译器能够将高级语言代码转换为可执行的汇编程序，并正确运行（如 Fibonacci 递归计算），获得了极大的满足感。

这次课程设计不仅让我掌握了编译器构造的核心技术，更培养了系统性思维和工程实践能力，为未来深入学习编译优化、程序分析等领域打下了坚实的基础。