#!/usr/bin/env bash
# Sync local docs/wiki to GitHub Wiki repository (AkshajCreator/ArchaOS.wiki.git)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DOCS_WIKI="$REPO_ROOT/docs/wiki"

WIKI_REMOTE="https://github.com/AkshajCreator/ArchaOS.wiki.git"

echo "Checking GitHub Wiki repository status..."
if ! git ls-remote "$WIKI_REMOTE" &>/dev/null; then
    echo ""
    echo "⚠️  GitHub Wiki repository has not been initialized yet."
    echo ""
    echo "To initialize it:"
    echo "  1. Open: https://github.com/AkshajCreator/ArchaOS/wiki"
    echo "  2. Click the green 'Create the first page' button"
    echo "  3. Click 'Save Page' at the bottom (any temporary text is fine)"
    echo "  4. Run this script again: ./scripts/sync-wiki.sh"
    echo ""
    exit 1
fi

echo "Cloning GitHub Wiki..."
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

git clone "$WIKI_REMOTE" "$TMP_DIR"
cp "$DOCS_WIKI"/*.md "$TMP_DIR/"

cd "$TMP_DIR"
git add -A
if git diff --cached --quiet; then
    echo "✅ GitHub Wiki is already up-to-date with docs/wiki!"
else
    git commit -m "Deploy complete ArchaOS v0.5 documentation wiki"
    git push origin HEAD
    echo "🚀 GitHub Wiki published successfully!"
    echo "Visit: https://github.com/AkshajCreator/ArchaOS/wiki"
fi
