#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "usage: $0 <path/to/grammar.y> [--keep]" >&2
  exit 1
}

if [[ $# -lt 1 ]]; then
  usage
fi

grammar_path=$1
shift || true
keep=false
if [[ ${1:-} == "--keep" ]]; then
  keep=true
fi

if [[ ! -f $grammar_path ]]; then
  echo "error: grammar file not found: $grammar_path" >&2
  exit 1
fi

root_dir=$(cd "$(dirname "$0")" && pwd)
abs_grammar=$(cd "$(dirname "$grammar_path")" && pwd)/$(basename "$grammar_path")
base_name=$(basename "$abs_grammar" .y)
work_dir="$root_dir/build/diagnostics/$base_name"
rm -rf "$work_dir"
mkdir -p "$work_dir"
trap 'if [[ $keep == false ]]; then rm -rf "$work_dir"; fi' EXIT

cp "$abs_grammar" "$work_dir/"

input_copy="$work_dir/$(basename "$abs_grammar")"
"$root_dir/seuyacc" --markdown --plantUML --definitions "$input_copy"

outputs=("$work_dir/$base_name.md" "$work_dir/$base_name.puml" "$work_dir/$base_name.tab.c" "$work_dir/$base_name.tab.h")

echo "generated files:" >&2
for file in "${outputs[@]}"; do
  if [[ -f $file ]]; then
    echo "  $file" >&2
  else
    echo "  missing: $file" >&2
  fi
done

if [[ $keep == true ]]; then
  echo "$work_dir"
fi
