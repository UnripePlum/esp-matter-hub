> **참고**: 이 문서는 초기 개발 단계(2026-03-04)의 작업 기록입니다. 현재 프로젝트 경로는 `/Users/unripeplum/projects/esp-matter-hub`이며, 최신 상태는 `2026-03-17_23` 문서를 참조하세요.

# ESP-Matter Light 업무 정리

## 1) 목표
- ESP32-S3-DevKitC-1 기반 `esp-matter` light 예제 설치/빌드/플래시
- 스마트폰 앱으로 Matter 커미셔닝 및 제어 확인
- 디버깅 실패 원인 식별 및 재현 가능한 실행 경로 정리

## 2) 환경
- 프로젝트 경로: `/Users/hanjunkim/IdeaProjects/esp-matter-hub`
- ESP 경로(심볼릭 링크): `/Users/hanjunkim/esp` -> `/Volumes/Gold-P31-SSD-2TB/esp`
- 타겟 보드: ESP32-S3 (`/dev/cu.usbserial-0001`)
- IDF: `v5.2.1`

## 3) 핵심 이슈와 원인

### 이슈 A: 디버깅/빌드 타겟 혼선
- `build`는 `esp32s3`, `build-idf`는 `esp32`로 혼재되어 재현성 저하.
- 경로도 `/Users/.../esp`와 `/Volumes/.../esp` 로그가 섞여 혼란.

원인:
- 빌드 디렉터리 이원화(`build`, `build-idf`)와 타겟 불일치.

조치:
- `build-idf` 사용 중단, `build` 하나로 통일.
- `run.sh`에서 `esp32s3` 타겟 강제.

### 이슈 B: `monitor` 실패
- 로그: `Monitor requires standard input to be attached to TTY`.

원인:
- TTY 없는 실행 컨텍스트에서 `idf.py monitor` 호출.

조치:
- 실제 터미널(Interactive TTY)에서 실행하도록 분리.

## 4) 변경 사항

### 파일 수정
- `/Users/hanjunkim/IdeaProjects/esp-matter-hub/run.sh`

주요 변경:
- `ESP_ROOT="$HOME/esp"` 기준 고정
- `IDF_PATH`, `ESP_MATTER_PATH` 명시적 export
- `ESP_MATTER_DEVICE_PATH` unset
- 항상 `idf.py --build-dir build set-target esp32s3` 실행
- 빌드/플래시/모니터 경로를 `build`로 통일

## 5) 실행/검증 결과

### 빌드/플래시
- `./run.sh /dev/cu.usbserial-0001`로 빌드 성공
- 플래시 성공
- 부팅 로그 정상 확인

### Matter 광고/커미셔닝 상태
- `CHIPoBLE advertising started` 확인
- 로그에서 `discriminator=3840` 확인

### Apple Home
- Home 앱에서 장치가 "전등"으로 표시됨 (정상)

## 6) 페어링 코드 정보

- Discriminator: `3840`
- Setup PIN(기본값): `20202021`
- Home 앱에서 시리얼 로그의 온보딩 정보(코드/QR)를 사용해 커미셔닝

## 7) 현재 권장 운영 방식

1. 커미셔닝: 스마트폰(Home/Google Home) 우선 사용
2. 제어 테스트: Home 앱에서 전등 On/Off 및 상태 반영 확인
3. 문제 재현 시: 보드 재부팅 후 광고 상태(`CHIPoBLE advertising started`)부터 확인

## 8) 재실행 명령 모음

```bash
# 환경 로드
source ~/esp/esp-idf/export.sh
source ~/esp/esp-matter/export.sh

# 빌드/플래시/모니터
cd /Users/hanjunkim/IdeaProjects/esp-matter-hub
./run.sh /dev/cu.usbserial-0001
```

## 9) Apple Home 테스트 절차

1. 보드 부팅 후 시리얼 로그에서 온보딩 코드(QR/숫자 코드) 확인
2. iPhone에서 Home 앱 열기 -> `+` -> `액세서리 추가`
3. QR 스캔 또는 숫자 코드 수동 입력
4. 장치가 "전등"으로 등록되면 방(Room) 지정
5. Home 앱에서 On/Off 토글 시 보드 LED 상태 변경 확인
