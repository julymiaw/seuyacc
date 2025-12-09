#!/usr/bin/env bash
# test_with_official_yacc.sh - 使用官方 bison/yacc 测试语法文件

set -euo pipefail

root_dir=$(cd "$(dirname "$0")" && pwd)
build_dir="$root_dir/build_yacc_test"
rm -rf "$build_dir"
mkdir -p "$build_dir"

echo "=== 使用官方 Bison/Yacc 测试 minic.y ==="
echo ""

# 复制文件
cp "$root_dir/examples/minic.y" "$build_dir/"
cp "$root_dir/examples/minic.l" "$build_dir/"

# 检查是否有 bison 或 yacc
if command -v bison &> /dev/null; then
    YACC_CMD="bison -d -y"
    echo "使用: bison"
elif command -v yacc &> /dev/null; then
    YACC_CMD="yacc -d"
    echo "使用: yacc"
else
    echo "错误: 系统中没有找到 bison 或 yacc"
    exit 1
fi

echo ""
echo "步骤 1: 运行 $YACC_CMD..."
cd "$build_dir"
$YACC_CMD minic.y 2>&1 || {
    echo ""
    echo "=== Yacc/Bison 解析失败 ==="
    echo "这表明语法文件本身可能有问题,或者需要特殊处理"
    exit 1
}

echo "✓ Yacc/Bison 解析成功"

# 重命名生成的头文件,使其与 .l 文件的 include 匹配
if [[ -f "y.tab.h" ]]; then
    cp y.tab.h minic.tab.h
    echo "✓ 已复制 y.tab.h -> minic.tab.h"
fi

echo ""
echo "步骤 2: 运行 lex..."
lex -o minic_lex.yy.c minic.l

echo ""
echo "步骤 3: 编译..."
# 尝试用 C++ 编译 (因为有 cout)
if c++ -I. y.tab.c minic_lex.yy.c -o minic_parser 2>/dev/null; then
    echo "✓ 使用 C++ 编译成功"
elif cc -I. y.tab.c minic_lex.yy.c -ll -o minic_parser 2>/dev/null || \
     cc -I. y.tab.c minic_lex.yy.c -lfl -o minic_parser 2>/dev/null; then
    echo "✓ 使用 C 编译成功"
else
    echo "✗ 编译失败"
    exit 1
fi

echo ""
echo "步骤 4: 测试解析器..."
test_file="$root_dir/examples/fibonacci.c"
if [[ -f "$test_file" ]]; then
    echo "测试文件: fibonacci.c"
    ./minic_parser < "$test_file" && echo "✓ 解析成功" || echo "✗ 解析失败"
fi

echo ""
echo "=== 测试完成 ==="
echo "构建目录: $build_dir"