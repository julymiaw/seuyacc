#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <c99|test> [--keep]" >&2
  exit 1
fi

example=$1
shift || true
keep=false
if [[ ${1:-} == "--keep" ]]; then
  keep=true
fi

case "$example" in
  c99|test) ;;
  *) echo "unknown example: $example" >&2; exit 1 ;;
esac

root_dir=$(cd "$(dirname "$0")" && pwd)
build_dir="$root_dir/build/run/$example"
rm -rf "$build_dir"
mkdir -p "$build_dir"
trap 'if [[ $keep == false ]]; then rm -rf "$build_dir"; fi' EXIT

cp "$root_dir/examples/$example.y" "$build_dir/"
cp "$root_dir/examples/$example.l" "$build_dir/"
cp "$root_dir/examples/$example.c" "$build_dir/"
start_time=$(perl -MTime::HiRes=time -e 'printf "%.6f\n", time')
"$root_dir/seuyacc" --definitions "$build_dir/$example.y"
end_time=$(perl -MTime::HiRes=time -e 'printf "%.6f\n", time')
seuyacc_elapsed=$(START="$start_time" END="$end_time" perl -e 'printf "%.3f\n", $ENV{END} - $ENV{START}')
lex -o "$build_dir/${example}_lex.yy.c" "$build_dir/$example.l"

parser_c="$build_dir/$example.tab.c"
lexer_c="$build_dir/${example}_lex.yy.c"
cc -I"$build_dir" ${CPPFLAGS:+$CPPFLAGS} "$parser_c" "$lexer_c" ${LDFLAGS:+$LDFLAGS} -ll -o "$build_dir/${example}_parser" 2>/dev/null || \
  cc -I"$build_dir" ${CPPFLAGS:+$CPPFLAGS} "$parser_c" "$lexer_c" ${LDFLAGS:+$LDFLAGS} -lfl -o "$build_dir/${example}_parser"

"$build_dir/${example}_parser" < "$root_dir/examples/$example.c"

if [[ $keep == true ]]; then
  echo "$build_dir"
fi

echo "SeuYacc processing time: ${seuyacc_elapsed} seconds"
