#!/bin/bash
# ESP Matter Hub — 원격 플래싱 스크립트
# ESP-IDF 없이 esptool.py 만으로 플래싱합니다.
#
# 사전 준비:
#   pip install esptool
#
# 바이너리는 GitHub 릴리즈에서 다운로드:
#   https://github.com/muinlab/esp-matter-hub/releases
#
# 필요한 파일 (이 스크립트와 같은 디렉토리):
#   bootloader.bin, partition-table.bin, ota_data_initial.bin, esp-matter-hub.bin
#
# 사용법:
#   ./flash_remote.sh           — USB 포트 자동 감지
#   ./flash_remote.sh /dev/ttyUSB0

set -euo pipefail

PORT="${1:-}"
BAUD="${BAUD:-460800}"
CHIP="esp32s3"
FLASH_SIZE="16MB"

# ── 포트 자동 감지 ──────────────────────────────────────────────────────────
if [[ -z "$PORT" ]]; then
    if [[ "$(uname)" == "Darwin" ]]; then
        PORT=$(ls /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART* \
                  /dev/cu.wchusbserial* /dev/cu.usbmodem* 2>/dev/null \
               | sort | head -1 || true)
    else
        PORT=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -1 || true)
    fi
    if [[ -z "$PORT" ]]; then
        echo "[ERROR] USB 시리얼 포트를 찾을 수 없습니다."
        echo "사용법: $0 <PORT>"
        echo "예시:   $0 /dev/cu.usbserial-0001"
        exit 1
    fi
fi

# ── 바이너리 파일 확인 ──────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MISSING=0
for f in bootloader.bin partition-table.bin ota_data_initial.bin esp-matter-hub.bin; do
    if [[ ! -f "$SCRIPT_DIR/$f" ]]; then
        echo "[ERROR] $f 파일이 없습니다."
        MISSING=1
    fi
done

if [[ $MISSING -ne 0 ]]; then
    echo ""
    echo "GitHub 릴리즈에서 바이너리를 다운로드하세요:"
    echo "  https://github.com/muinlab/esp-matter-hub/releases/latest"
    echo ""
    echo "또는 gh CLI 로 한번에 다운로드:"
    echo "  gh release download --repo muinlab/esp-matter-hub --dir ."
    exit 1
fi

# ── 플래싱 ─────────────────────────────────────────────────────────────────
echo "════════════════════════════════════════"
echo "  ESP Matter Hub 플래싱"
echo "  Port:  $PORT"
echo "  Baud:  $BAUD"
echo "════════════════════════════════════════"

esptool.py \
    --chip "$CHIP" \
    -p "$PORT" \
    -b "$BAUD" \
    --before default_reset \
    --after hard_reset \
    write_flash \
    --flash_mode dio \
    --flash_size "$FLASH_SIZE" \
    --flash_freq 80m \
    0x0       "$SCRIPT_DIR/bootloader.bin" \
    0xc000    "$SCRIPT_DIR/partition-table.bin" \
    0x111000  "$SCRIPT_DIR/ota_data_initial.bin" \
    0x120000  "$SCRIPT_DIR/esp-matter-hub.bin"

echo ""
echo "════════════════════════════════════════"
echo "  플래싱 완료"
echo "  모니터: python -m serial.tools.miniterm $PORT 115200"
echo "════════════════════════════════════════"
