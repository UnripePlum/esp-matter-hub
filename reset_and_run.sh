#!/usr/bin/env bash

set -euo pipefail

PORT="${1:-/dev/cu.usbserial-0001}"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "🔄 Loading ESP-IDF environment..."
source "$HOME/esp/esp-idf/export.sh"

echo "🔄 Loading ESP-Matter environment..."
source "$HOME/esp/esp-matter/export.sh"

echo "📂 Moving to project: $PROJECT_DIR"
cd "$PROJECT_DIR"

echo "🧹 Erasing flash on $PORT ..."
idf.py -p "$PORT" erase-flash

echo "🚀 Running build/flash/monitor workflow..."
./run.sh "$PORT"
