#!/bin/bash
# ────────────────────────────────────────────────────────────
# Flujo kanban: el agente crea worktree + submodules FUERA,
# el contenedor solo compila (sin git).
#
# Uso:  worktree-helper.sh <worktree-dir> [comando]
# ────────────────────────────────────────────────────────────
set -euo pipefail
PROJECTS="${PROJECTS_ROOT:-$HOME/projects}"
MAIN="${MAIN_REPO:-$PROJECTS/chiaki-ng}"
IMAGE="${DEV_IMAGE:-chiaki-devcontainer}"

WT="${1:?Uso: $0 <worktree-dir> [comando]}"
shift || true
CMD="${@:-bash}"

WT_PATH="${PROJECTS}/${WT}"

# ── 1. Crear worktree (fuera del contenedor) ─────────────
if [ ! -d "$WT_PATH" ]; then
    BRANCH="feat/${WT#chiaki-ng-}"
    echo "📁 Creando worktree: $WT_PATH ($BRANCH)"
    cd "$MAIN"
    git worktree add "$WT_PATH" -b "$BRANCH"
    cd "$WT_PATH"
    git submodule update --init --recursive
fi

# ── 2. Build dentro del contenedor ────────────────────────
echo "🐳 Build: $WT_PATH"
exec docker run --rm -u root \
    -v "${WT_PATH}:/workspace" \
    -w /workspace \
    "$IMAGE" bash -c "$CMD"
