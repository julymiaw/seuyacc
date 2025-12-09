#!/usr/bin/env bash
set -euo pipefail

root_dir=$(cd "$(dirname "$0")" && pwd)
build_dir="$root_dir/build"
rm -rf "$build_dir"
mkdir -p "$build_dir"

cp "$root_dir/examples/minic.y" "$build_dir/"
cp "$root_dir/examples/minic.l" "$build_dir/"

echo "Running seuyacc..."
"$root_dir/seuyacc" --definitions "$build_dir/minic.y"

echo "Running lex..."
lex -o "$build_dir/minic_lex.yy.c" "$build_dir/minic.l"

echo "Compiling parser..."
parser_c="$build_dir/minic.tab.c"
lexer_c="$build_dir/minic_lex.yy.c"
cc -I"$build_dir" ${CPPFLAGS:+$CPPFLAGS} "$parser_c" "$lexer_c" ${LDFLAGS:+$LDFLAGS} -ll -o "$build_dir/minic_parser" 2>/dev/null || \
  cc -I"$build_dir" ${CPPFLAGS:+$CPPFLAGS} "$parser_c" "$lexer_c" ${LDFLAGS:+$LDFLAGS} -lfl -o "$build_dir/minic_parser"

test_files=(
  "led_display.c"
  "fibonacci.c"
  "array_param.c"
  "no_param.c"
  "interrupt.c"
  "array_access.c"
)

echo ""
echo "Parsing test files..."
for test_file in "${test_files[@]}"; do
  echo "Testing $test_file..."
  log_file="$build_dir/${test_file%.c}.log"
  asm_file="$build_dir/${test_file%.c}.asm"
  
  "$build_dir/minic_parser" < "$root_dir/examples/$test_file" > "$log_file" 2>&1
  
  # 重命名生成的汇编文件
  if [ -f "output.asm" ]; then
    mv "output.asm" "$asm_file"
    echo "  → Assembly: $asm_file"
  fi
  
  if [ $? -eq 0 ]; then
    echo "✓ $test_file passed (log: $log_file)"
  else
    echo "✗ $test_file failed (log: $log_file)"
    exit 1
  fi
done

echo ""
echo "Build directory: $build_dir"
echo "All tests passed!"