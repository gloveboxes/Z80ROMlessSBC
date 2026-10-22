#!/usr/bin/env bash
# Rebuild the same deterministic header as the CMake firmware target.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
HEADER_FILE="${1:-${PROJECT_ROOT}/build/src/stage10_websocket_terminal/generated/static_html_hex.h}"
mkdir -p "$(dirname "${HEADER_FILE}")"
python3 "${SCRIPT_DIR}/../embed_html.py" "${SCRIPT_DIR}/index.html" "${HEADER_FILE}"
printf 'Generated %s\n' "${HEADER_FILE}"
