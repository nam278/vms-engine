#!/usr/bin/env bash
# scripts/format.sh — Run clang-format on all C++ source files
#
# Usage:
#   ./scripts/format.sh           # Format all files in-place
#   ./scripts/format.sh --check   # Dry-run: exit 1 if any file would change
#   ./scripts/format.sh [file..]  # Format specific files only
#
# Requires: clang-format (v14+ recommended)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"

# ── Validate tool availability ──────────────────────────────────────────────
if ! command -v "${CLANG_FORMAT}" &>/dev/null; then
    echo "✗  clang-format not found. Install it with:"
    echo "     apt install clang-format"
    exit 1
fi

CF_VERSION=$("${CLANG_FORMAT}" --version 2>&1 | grep -oP '\d+\.\d+' | head -1)
echo "  clang-format ${CF_VERSION}  |  style: $(head -3 "${REPO_ROOT}/.clang-format" | grep BasedOnStyle | awk '{print $2}')"

# ── Collect files to format ──────────────────────────────────────────────────
if [[ $# -gt 0 && "$1" != "--check" ]]; then
    # Specific files passed on command line
    FILES=("$@")
else
    mapfile -t FILES < <(
        find "${REPO_ROOT}" \
            \( -path "${REPO_ROOT}/build"   -prune \
            -o -path "${REPO_ROOT}/.git"    -prune \
            -o -path "${REPO_ROOT}/scripts" -prune \) \
            -o \( -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.cc" -o -name "*.cxx" \) -print \)
    )
fi

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "  No C++ files found."
    exit 0
fi

# ── Format (or check) ─────────────────────────────────────────────────────────
CHECK_MODE=false
if [[ "${1:-}" == "--check" ]]; then
    CHECK_MODE=true
fi

CHANGED=0
for f in "${FILES[@]}"; do
    if $CHECK_MODE; then
        if ! "${CLANG_FORMAT}" --dry-run --Werror "$f" &>/dev/null; then
            echo "  ✗  needs formatting: ${f#"${REPO_ROOT}/"}"
            CHANGED=$((CHANGED + 1))
        fi
    else
        "${CLANG_FORMAT}" -i "$f"
    fi
done

# ── Result ─────────────────────────────────────────────────────────────────
TOTAL=${#FILES[@]}
if $CHECK_MODE; then
    if [[ $CHANGED -gt 0 ]]; then
        echo ""
        echo "  ✗  ${CHANGED}/${TOTAL} file(s) need formatting."
        echo "     Run  ./scripts/format.sh  to fix them."
        exit 1
    else
        echo "  ✔  All ${TOTAL} file(s) are correctly formatted."
    fi
else
    echo "  ✔  Formatted ${TOTAL} file(s)."
fi
