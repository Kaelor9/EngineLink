#!/usr/bin/env bash
# On-demand, code-only Graphify index. No semantic/LLM extraction.
# Keep the output inside graphify-out/ (ignored by git).
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

if ! command -v graphify >/dev/null 2>&1; then
  echo "Graphify is unavailable; use focused code searches instead." >&2
  exit 0
fi

if [[ -s graphify-out/graph.json ]]; then
  echo "Updating existing Graphify index without LLM..."
  GRAPHIFY_BACKEND=none graphify update .
else
  echo "Building code-only Graphify index without LLM..."
  GRAPHIFY_BACKEND=none graphify extract . --code-only
fi
