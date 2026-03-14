#!/bin/bash
# setup.sh

ESP_DIR="$HOME/esp"

echo "📦 ESP32 외부 환경 구축을 시작합니다 ($ESP_DIR)..."
echo "--------------------------------------------------------"

mkdir -p "$ESP_DIR"

# 1. ESP-IDF 설치
if [ ! -d "$ESP_DIR/esp-idf" ]; then
    echo "⬇️ ESP-IDF 다운로드 중..."
    cd "$ESP_DIR" || exit
    git clone -b v5.2.1 --recursive https://github.com/espressif/esp-idf.git
    cd esp-idf || exit
    echo "⚙️ ESP-IDF 도구 설치 중 (시간이 다소 소요됩니다)..."
    ./install.sh esp32,esp32s3
else
    echo "✅ ESP-IDF가 이미 설치되어 있습니다 ($ESP_DIR/esp-idf)."
fi

# 2. ESP-Matter 설치 (관련 라이브러리가 필요한 경우)
if [ ! -d "$ESP_DIR/esp-matter" ]; then
    echo "⬇️ ESP-Matter 다운로드 중..."
    cd "$ESP_DIR" || exit
    git clone --depth 1 https://github.com/espressif/esp-matter.git
    cd esp-matter || exit
    echo "⚙️ ESP-Matter 서브모듈 초기화 중..."
    git submodule update --init --recursive
    # 설치 명령어 (필요 시 수정)
    # ./install.sh
else
    echo "✅ ESP-Matter가 이미 설치되어 있습니다 ($ESP_DIR/esp-matter)."
fi

echo "========================================================"
echo "🎉 설치 완료!"
echo "이제 외부 터미널에서는 'get_idf', 'get_matter' 별칭으로 환경을 로드할 수 있습니다 열."
echo "이 프로젝트에서는 './run.sh' 를 실행하시면 자동으로 외부 환경 변수를 가져와 빌드/플래싱을 진행합니다."
