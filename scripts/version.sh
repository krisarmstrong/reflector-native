#!/bin/bash
# version.sh - Semantic versioning management for reflector-native
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VERSION_FILE="$PROJECT_ROOT/VERSION"
HEADER_FILE="$PROJECT_ROOT/include/reflector.h"

get_current_version() {
    if [ -f "$VERSION_FILE" ]; then
        cat "$VERSION_FILE"
    else
        echo "1.0.0"
    fi
}

parse_version() {
    local version=$1
    IFS='.' read -r major minor patch <<< "$version"
    echo "$major $minor $patch"
}

bump_version() {
    local type=$1
    local current=$(get_current_version)
    read -r major minor patch <<< $(parse_version "$current")

    case "$type" in
        major)
            major=$((major + 1)); minor=0; patch=0 ;;
        minor)
            minor=$((minor + 1)); patch=0 ;;
        patch)
            patch=$((patch + 1)) ;;
        *)
            echo "Error: Invalid bump type" >&2
            exit 1 ;;
    esac
    echo "$major.$minor.$patch"
}

update_version_file() {
    echo "$1" > "$VERSION_FILE"
    echo "Updated VERSION: $1"
}

update_header_file() {
    local version=$1
    read -r major minor patch <<< $(parse_version "$version")
    sed -i.bak "s/^#define REFLECTOR_VERSION_MAJOR .*/#define REFLECTOR_VERSION_MAJOR $major/" "$HEADER_FILE"
    sed -i.bak "s/^#define REFLECTOR_VERSION_MINOR .*/#define REFLECTOR_VERSION_MINOR $minor/" "$HEADER_FILE"
    sed -i.bak "s/^#define REFLECTOR_VERSION_PATCH .*/#define REFLECTOR_VERSION_PATCH $patch/" "$HEADER_FILE"
    rm -f "$HEADER_FILE.bak"
    echo "Updated header: $major.$minor.$patch"
}

case "${1:-}" in
    current)
        get_current_version ;;
    bump)
        new_version=$(bump_version "$2")
        update_version_file "$new_version"
        update_header_file "$new_version" ;;
    set)
        update_version_file "$2"
        update_header_file "$2" ;;
    *)
        echo "Usage: $0 {current|bump|set} [args]"
        exit 1 ;;
esac
