#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

pushd src/control > /dev/null
go test ./...
golangci-lint run
popd > /dev/null
