#!/usr/bin/env bash
# scripts/install-hooks.sh — Install git hooks from scripts/hooks/ into .git/hooks/
#
# Run once after cloning:
#   ./scripts/install-hooks.sh
#
# Re-run after adding new hooks to scripts/hooks/.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOOKS_SRC="${REPO_ROOT}/scripts/hooks"
HOOKS_DST="${REPO_ROOT}/.git/hooks"

if [[ ! -d "${HOOKS_DST}" ]]; then
    echo "✗  .git/hooks not found — are you inside a git repository?"
    exit 1
fi

INSTALLED=0
for src in "${HOOKS_SRC}"/*; do
    hook_name="$(basename "${src}")"
    dst="${HOOKS_DST}/${hook_name}"

    if [[ -e "${dst}" || -L "${dst}" ]]; then
        rm "${dst}"
    fi

    ln -s "${src}" "${dst}"
    chmod +x "${src}"
    echo "  ✔  installed: .git/hooks/${hook_name}  →  scripts/hooks/${hook_name}"
    INSTALLED=$((INSTALLED + 1))
done

echo ""
echo "  ${INSTALLED} hook(s) installed."
echo "  Use  git commit --no-verify  to bypass hooks when needed."
