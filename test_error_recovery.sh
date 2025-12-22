#!/usr/bin/env bash
# test_error_recovery.sh - 测试错误恢复和诊断功能

set -euo pipefail

root_dir=$(cd "$(dirname "$0")" && pwd)
build_dir="$root_dir/build_error_test"
rm -rf "$build_dir"
mkdir -p "$build_dir"

echo "=== 测试错误恢复和诊断功能 ==="
echo ""

# 复制文件
cp "$root_dir/examples/minic.y" "$build_dir/"
cp "$root_dir/examples/minic.l" "$build_dir/"

echo "步骤 1: 使用 bison 生成 parser..."
cd "$build_dir"
bison -d minic.y

echo ""
echo "步骤 2: 使用 lex 生成 lexer..."
lex -o minic_lex.yy.c minic.l

echo ""
echo "步骤 3: 编译 parser..."
if c++ -I. minic.tab.c minic_lex.yy.c -o minic_parser 2>/dev/null; then
    echo "✓ 使用 C++ 编译成功"
elif cc -I. minic.tab.c minic_lex.yy.c -ll -o minic_parser 2>/dev/null || \
     cc -I. minic.tab.c minic_lex.yy.c -lfl -o minic_parser 2>/dev/null; then
    echo "✓ 使用 C 编译成功"
else
    echo "✗ 编译失败"
    exit 1
fi

echo ""
echo "步骤 4: 测试错误文件..."

# 查找所有 error_*.c 文件
error_files=("$root_dir"/examples/error_*.c)

if [ -e "${error_files[0]}" ]; then
    for error_file in "${error_files[@]}"; do
        filename=$(basename "$error_file")
        name="${filename%.c}"
        json_file="$build_dir/${name}.json"
        
        echo ""
        echo "测试: $filename"
        echo "---"
        
        # 运行 parser，输出重定向到 JSON 文件
        ./minic_parser < "$error_file" > "$json_file" 2>&1 || true
        
        echo "✓ 诊断信息已生成: $json_file"
        
        # 显示 JSON 内容
        if [ -f "$json_file" ]; then
            cat "$json_file"
        fi
    done
else
    echo "⚠ 没有找到 error_*.c 测试文件"
    echo "请在 examples/ 目录创建测试文件，如 error_missing_brace.c"
fi

echo ""
echo "=== 测试完成 ==="
echo "构建目录: $build_dir"
echo ""
echo "提示: JSON 文件可以直接提供给 IDE 进行错误标注"
