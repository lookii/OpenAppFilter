#!/usr/bin/env bash
# 本地一键自检（等价于 GitHub Actions 的 validate 工作流）
# 用法: ./scripts/check-all.sh
set -euo pipefail
cd "$(dirname "$0")/.."

echo "===== 1/3 源码关键标记 ====="
python3 scripts/check-sources.py

echo
echo "===== 2/3 独立 JS 语法 ====="
bad=0; ok=0
while IFS= read -r f; do
  if node --check "$f"; then ok=$((ok+1)); else bad=$((bad+1)); echo "FAIL $f"; fi
done < <(find luci-app-oaf/htdocs -name '*.js' ! -name '*.min.js')
echo "JS: 通过 $ok, 失败 $bad"
[ "$bad" -eq 0 ]

echo
echo "===== 3/3 HTML 内联 JS 语法 ====="
python3 scripts/check-inline-js.py

echo
echo "全部自检通过 ✅"
