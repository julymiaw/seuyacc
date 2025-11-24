# SeuYacc

SeuYacc 是一个高效的 LR(1) 语法分析器生成工具，类似于 Yacc/Bison。它能够读取语法定义文件（.y），并生成相应的 C 语言解析器代码。

## 环境要求

- C++ 编译器 (支持 C++17)
- [xmake](https://xmake.io/) 构建工具
- (可选) Flex (用于处理词法分析示例)

## 构建项目

本项目使用 xmake 进行构建管理。

在项目根目录下运行：

```bash
xmake
```

构建成功后，可执行文件 `seuyacc` 将生成在 `build/` 目录下。

## 如何运行

### 1. 快速运行示例 (推荐)

项目中提供了一个辅助脚本 `run_examples.sh`，可以一键完成"生成解析器 -> 编译 -> 运行"的全过程。

以 C99 语法为例：

```bash
./run_examples.sh c99
```

该命令会自动执行以下步骤：

1. 调用 `seuyacc` 读取 `examples/c99.y`，生成 `y.tab.c` 和 `y.tab.h`。
2. 调用 `flex` 处理 `examples/c99.l` 生成词法分析器。
3. 使用 `gcc` 编译所有生成的 C 代码。
4. 运行最终生成的解析器程序。

如果你想查看生成的中间文件（`y.tab.c`, `y.tab.h` 等），可以加上 `--keep` 参数：

```bash
./run_examples.sh c99 --keep
```

### 2. 手动运行 Seuyacc

你也可以直接调用构建好的 `seuyacc` 程序来处理你的语法文件：

```bash
# 使用 xmake run 直接运行
xmake run seuyacc <path/to/your_grammar.y>

# 或者直接调用二进制文件 (路径可能因平台而异)
./build/macosx/arm64/release/seuyacc examples/c99.y
```

默认情况下，这将在当前目录生成：

- `y.tab.c`: 包含 LR(1) 分析表和 `yyparse` 函数实现的 C 源码。
- `y.tab.h`: 包含 Token 类型定义和 `YYSTYPE` 定义的头文件。

## 对接说明 (For Developers)

如果你正在开发后续阶段（如中间代码生成）：

1. **接口文件**: 关注生成的 `y.tab.h`，其中定义了 `yytokentype` 枚举和 `YYSTYPE` 联合体，这是与词法分析器(SeuLex)交互的接口。
2. **解析入口**: 生成的代码提供了 `int yyparse(void)` 函数作为解析入口。
3. **语义动作**: 你可以在 `.y` 文件的产生式中编写 C 代码（语义动作），这些代码会被原样嵌入到 `y.tab.c` 的 `yy_reduce` 函数中，用于构建语法树或生成中间代码。

## 项目结构

- `src/`: SeuYacc 核心源代码 (LR表生成算法等)
- `include/`: 头文件
- `examples/`: 语法定义示例
- `xmake.lua`: 构建配置
