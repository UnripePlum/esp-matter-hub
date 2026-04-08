#!/bin/bash
# ESP Matter Hub 펌웨어 빌드 + GitHub 릴리즈 자동화
#
# 사전 준비:
#   gh CLI 설치 및 로그인 (brew install gh && gh auth login)
#
# 사용법:
#   ./release.sh          — FIRMWARE_VERSION 자동 감지
#   ./release.sh v1.2.3   — 버전 직접 지정

set -euo pipefail

REPO="muinlab/esp-matter-hub"
BIN_NAME="esp-matter-hub.bin"
ESP_ROOT="$HOME/esp"
VENV_GLOB="$HOME/.espressif/python_env/idf5.2_py3.9_env"

# ── 1. 버전 결정 ────────────────────────────────────────────────────────────
if [[ -n "${1:-}" ]]; then
    VERSION="$1"
else
    VERSION=$(grep -oE '"v[0-9]+\.[0-9]+\.[0-9]+"' main/app_main.cpp \
                | head -1 | tr -d '"' || true)
    if [[ -z "$VERSION" ]]; then
        echo "[ERROR] main/app_main.cpp 에서 FIRMWARE_VERSION을 찾지 못했습니다."
        echo "       '#define FIRMWARE_VERSION \"vX.Y.Z\"' 형식이어야 합니다."
        exit 1
    fi
fi

echo "────────────────────────────────────────"
echo "  ESP Matter Hub 릴리즈: $VERSION"
echo "────────────────────────────────────────"

# ── 2. 태그 중복 확인 ───────────────────────────────────────────────────────
if git rev-parse "$VERSION" &>/dev/null 2>&1; then
    echo "[ERROR] 태그 '$VERSION' 이 이미 존재합니다."
    echo "       app_main.cpp 의 FIRMWARE_VERSION 을 올려주세요."
    exit 1
fi

# ── 3. ESP-IDF 환경 활성화 ──────────────────────────────────────────────────
if ! command -v idf.py &>/dev/null; then
    echo "▶ ESP-IDF 환경 로드 중..."
    # Python venv 먼저 활성화 (idf.py import 에러 방지)
    if [[ -f "$VENV_GLOB/bin/activate" ]]; then
        # shellcheck disable=SC1090
        source "$VENV_GLOB/bin/activate"
    fi
    # IDF_PATH 탐색: $HOME/esp or 심볼릭 링크 실경로
    if [[ -f "$ESP_ROOT/esp-idf/export.sh" ]]; then
        export IDF_PATH="$ESP_ROOT/esp-idf"
    elif [[ -d "/Volumes/Gold-P31-SSD-2TB/esp/esp-idf" ]]; then
        export IDF_PATH="/Volumes/Gold-P31-SSD-2TB/esp/esp-idf"
    else
        echo "[ERROR] esp-idf 를 찾을 수 없습니다. IDF_PATH 를 직접 export 하거나"
        echo "       IDF 환경을 활성화한 뒤 이 스크립트를 실행하세요."
        exit 1
    fi
    export ESP_MATTER_PATH="$ESP_ROOT/esp-matter"
    export _PW_ACTUAL_ENVIRONMENT_ROOT="$ESP_MATTER_PATH/connectedhomeip/connectedhomeip/.environment"
    export IDF_PYTHON_CHECK_CMDS=0
    IDF_PY="python $IDF_PATH/tools/idf.py"
else
    IDF_PY="idf.py"
fi

# ── 4. 빌드 ─────────────────────────────────────────────────────────────────
echo "▶ 펌웨어 빌드 중..."
$IDF_PY build

echo "▶ 빌드 성공"

# ── 5. 바이너리 스테이징 ────────────────────────────────────────────────────
STAGE_DIR="$(mktemp -d)"
trap 'rm -rf "$STAGE_DIR"' EXIT

cp build/light.bin                          "$STAGE_DIR/$BIN_NAME"
cp build/bootloader/bootloader.bin          "$STAGE_DIR/bootloader.bin"
cp build/partition_table/partition-table.bin "$STAGE_DIR/partition-table.bin"
cp build/ota_data_initial.bin               "$STAGE_DIR/ota_data_initial.bin"

echo "▶ 릴리즈 바이너리:"
ls -lh "$STAGE_DIR/"

# ── 6. Git 태그 생성 및 푸시 ───────────────────────────────────────────────
echo "▶ git tag $VERSION 생성..."
git tag "$VERSION"
git push origin "$VERSION"

# ── 7. GitHub 릴리즈 생성 ──────────────────────────────────────────────────
echo "▶ GitHub 릴리즈 생성 중..."
gh release create "$VERSION" \
    --repo "$REPO" \
    --title "esp-matter-hub $VERSION" \
    --notes "## 설치 방법

### 초기 플래싱 (ESP32-S3에 직접 연결)
\`\`\`bash
./flash_remote.sh
\`\`\`

### OTA 업데이트 (기기가 이미 네트워크에 연결된 경우)
\`\`\`bash
curl -X POST http://<hub-ip>/api/ota/trigger \\
  -H 'X-Api-Key: <key>'
\`\`\`

---
자동 OTA: 기기가 1시간마다 이 릴리즈를 확인하여 자동 업데이트합니다." \
    "$STAGE_DIR/$BIN_NAME" \
    "$STAGE_DIR/bootloader.bin" \
    "$STAGE_DIR/partition-table.bin" \
    "$STAGE_DIR/ota_data_initial.bin"

echo ""
echo "════════════════════════════════════════"
echo "  릴리즈 완료: $VERSION"
echo "  https://github.com/$REPO/releases/tag/$VERSION"
echo "════════════════════════════════════════"
