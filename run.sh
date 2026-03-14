#!/bin/bash

# 포트 자동 탐지 (인자가 있으면 인자 우선)
detect_serial_port() {
    local candidates=()
    local patterns=(
        "/dev/cu.usbserial*"
        "/dev/cu.SLAB_USBtoUART*"
        "/dev/cu.wchusbserial*"
        "/dev/cu.usbmodem*"
        "/dev/tty.usbserial*"
        "/dev/tty.SLAB_USBtoUART*"
        "/dev/tty.wchusbserial*"
        "/dev/tty.usbmodem*"
    )

    for pattern in "${patterns[@]}"; do
        for dev in $pattern; do
            [ -e "$dev" ] && candidates+=("$dev")
        done
    done

    if [ ${#candidates[@]} -gt 0 ]; then
        printf '%s\n' "${candidates[@]}" | sort | head -n 1
        return 0
    fi

    return 1
}

if [ -n "${1:-}" ]; then
    PORT="$1"
else
    PORT="$(detect_serial_port)"
fi

if [ -z "${PORT:-}" ]; then
    echo "❌ 자동 탐지 실패: 연결된 ESP 시리얼 포트를 찾지 못했습니다."
    echo "💡 예시: ./run.sh /dev/cu.usbserial-0001"
    exit 1
fi

PROJECT_ROOT=$(pwd)
ESP_ROOT="$HOME/esp"
BUILD_DIR="build"

echo "🎯 ESP32 빌드, 플래싱, 모니터 통합 워크플로우 (Port: $PORT)"
echo "--------------------------------------------------------"

# 외부 esp 디렉토리에 설치된 esp-idf 환경 활성화
if [ -f "$ESP_ROOT/esp-idf/export.sh" ]; then
    echo "🔄 외부 ESP-IDF 환경 변수를 불러옵니다..."
    source "$ESP_ROOT/esp-idf/export.sh"
else
    echo "❌ 오류: 외부 esp-idf 디렉토리를 찾을 수 없습니다 ($ESP_ROOT/esp-idf)."
    echo "💡 먼저 './setup.sh'를 실행하여 ESP 환경을 다운로드(설치)해주세요."
    exit 1
fi

# 외부 esp 디렉토리에 설치된 esp-matter 환경 활성화
if [ -f "$ESP_ROOT/esp-matter/export.sh" ]; then
    echo "🔄 외부 ESP-Matter 환경 변수를 불러옵니다..."
    source "$ESP_ROOT/esp-matter/export.sh"
else
    echo "⚠️ 경고: 외부 esp-matter 디렉토리를 찾을 수 없습니다 ($ESP_ROOT/esp-matter)."
    echo "Matter 개발이 필요하다면 './setup.sh'를 다시 확인해주세요."
fi

# 경로 표기를 심볼릭 링크(/Users/hanjunkim/esp)로 강제 통일
export IDF_PATH="$ESP_ROOT/esp-idf"
export ESP_MATTER_PATH="$ESP_ROOT/esp-matter"
unset ESP_MATTER_DEVICE_PATH

# idf.py 명령어가 정상적으로 로드되었는지 최종 확인
if ! command -v idf.py &> /dev/null
then
    echo "❌ 환경 변수 로드 실패. idf.py 명령어를 찾을 수 없습니다."
    exit 1
fi

# macOS에서 ruamel.yaml 의존성 패키지 검사 버그를 우회하기 위한 환경 변수 (idf.py용)
export IDF_PYTHON_CHECK_CMDS=0

echo "🚀 작업을 시작합니다..."
idf.py --build-dir "$BUILD_DIR" set-target esp32s3
idf.py --build-dir "$BUILD_DIR" -p "$PORT" build flash monitor

echo "✅ 종료되었습니다."
