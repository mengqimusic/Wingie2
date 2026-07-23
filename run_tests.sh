#!/usr/bin/env bash
# Wingie2 统一测试入口：host C++ 单元测试 + Python pytest。
# AGENTS.md Testing Guidelines 记录了这两层的覆盖范围。
set -euo pipefail
cd "$(dirname "$0")"

status=0

echo "=== Host C++ 单元测试 (tests/host/) ==="
for src in tests/host/*_test.cpp; do
  name=$(basename "$src" .cpp)
  bin="/tmp/wingie2_${name}"
  if g++ -std=c++17 -I. "$src" -o "$bin" 2>/dev/null; then
    if "$bin" > /dev/null 2>&1; then
      echo "  PASS  $name"
    else
      echo "  FAIL  $name (运行失败)"
      status=1
    fi
  else
    echo "  FAIL  $name (编译失败)"
    status=1
  fi
  rm -f "$bin"
done

echo ""
echo "=== Python pytest (tests/) ==="
if python3 -m pytest tests/ -q "$@"; then
  echo "  pytest 全部通过"
else
  echo "  pytest 有失败"
  status=1
fi

echo ""
if [ "$status" -eq 0 ]; then
  echo "全部测试通过。"
else
  echo "存在测试失败。"
fi
exit "$status"
