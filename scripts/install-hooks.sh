#!/bin/bash
# Install git hooks

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
GIT_HOOKS_DIR="$REPO_ROOT/.git/hooks"
CUSTOM_HOOKS_DIR="$REPO_ROOT/.githooks"

echo "Installing git hooks..."

# Check if .git directory exists
if [ ! -d "$REPO_ROOT/.git" ]; then
    echo "Error: Not a git repository"
    exit 1
fi

# Create hooks directory if it doesn't exist
mkdir -p "$GIT_HOOKS_DIR"

# Install pre-commit hook
if [ -f "$CUSTOM_HOOKS_DIR/pre-commit" ]; then
    cp "$CUSTOM_HOOKS_DIR/pre-commit" "$GIT_HOOKS_DIR/pre-commit"
    chmod +x "$GIT_HOOKS_DIR/pre-commit"
    echo "✅ Installed pre-commit hook"
else
    echo "⚠️  Warning: pre-commit hook not found in $CUSTOM_HOOKS_DIR"
fi

echo ""
echo "Git hooks installed successfully!"
echo ""
echo "The pre-commit hook will:"
echo "  - Run tests before each commit"
echo "  - Prevent commits if tests fail"
echo ""
echo "To bypass hooks (not recommended): git commit --no-verify"
