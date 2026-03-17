# 2026-03-08 Worklog: mDNS, 상태 LED, On/Off 분리 바인딩

## 요약
- 3색 상태 LED(R=GPIO15, G=GPIO16, B=GPIO17, common cathode) 연동 완료

> **업데이트 (2026-03-12)**: 외부 3색 LED(GPIO 15/16/17)는 온보드 WS2812 LED로 교체됨. 상세 내용은 `2026-03-12_20` 문서 참조.
- 슬롯 바인딩을 `onoff_signal_id` 단일 구조에서 `on_signal_id` / `off_signal_id` / `level_up_signal_id` / `level_down_signal_id` 구조로 변경
- 웹 대시보드/API를 새 바인딩 구조로 전면 업데이트
- mDNS 동작 경로를 정리했고, 현재는 alias host(`esp-matter-hub-<mac_suffix>.local`) 기반 HTTP 광고를 사용하도록 조정

## 1) 상태 LED 구현

### 구현 파일
- `main/status_led.h`
- `main/status_led.cpp`
- `main/app_main.cpp` (Matter 이벤트 연동)
- `main/ir_engine.cpp` (학습 상태 연동)
- `main/web_server.cpp` (`/api/health`에 `led_state` 노출)

### 동작 규칙
- `BOOTING`: 파랑 느린 점멸
- `COMMISSIONING`: 파랑 빠른 점멸
- `READY`: 초록 고정
- `LEARNING_IN_PROGRESS`: 노랑 점멸
- `LEARNING_SUCCESS`: 초록 점멸 후 복귀
- `LEARNING_FAILED`: 빨강 점멸 후 복귀

## 2) 슬롯 바인딩 구조 변경 (중요)

### 변경 이유
- 기존 `onoff_signal_id` 1개 구조로는 On/Off 상태별로 다른 IR 송신이 불가능

### 변경 내용
- 구조체/저장/API/UI를 모두 아래 4필드로 통일
  - `on_signal_id`
  - `off_signal_id`
  - `level_up_signal_id`
  - `level_down_signal_id`

### 반영 파일
- `main/bridge_action.h`
- `main/bridge_action.cpp`
- `main/web_server.cpp`

### 실행 동작
- OnOff 클러스터 제어 시
  - `On=true` -> `on_signal_id` 송신
  - `On=false` -> `off_signal_id` 송신
- Level 클러스터 제어 시
  - 증가 -> `level_up_signal_id`
  - 감소 -> `level_down_signal_id`
  - `level=0` -> `off_signal_id`

### NVS 키 변경
- 기존: `s{slot}_onoff`, `s{slot}_level`
- 현재: `s{slot}_on`, `s{slot}_off`, `s{slot}_level_up`, `s{slot}_level_down`

주의: 기존 `onoff` 키는 더 이상 사용하지 않으므로 슬롯 바인딩 재설정 필요.

## 3) mDNS 경로 작업

### 배경
- minimal mDNS(`CONFIG_USE_MINIMAL_MDNS=y`)에서는 HTTP mDNS 광고 제약이 있어 `.local` 웹 접근 제어가 불안정
- non-minimal mDNS 경로로 전환 시 외부 CHIP 소스의 `nodiscard` 경고가 `-Werror`로 빌드 실패 유발

### 조치
1. non-minimal mDNS 사용
   - `sdkconfig.defaults`: `CONFIG_USE_MINIMAL_MDNS=n`
2. delegated host 기능 활성화
   - `sdkconfig.defaults`: `CONFIG_MDNS_PUBLISH_DELEGATE_HOST=y`
3. 외부 CHIP 소스 패치(`nodiscard` 반환값 무시 경고 처리)
   - `/Users/hanjunkim/esp/esp-matter/connectedhomeip/connectedhomeip/config/esp32/third_party/connectedhomeip/src/platform/ESP32/ESP32DnssdImpl.cpp`
4. HTTP mDNS를 alias host 기준으로 등록
   - `main/local_discovery.cpp`
   - `mdns_delegate_hostname_add/set_address`
   - `mdns_service_add_for_host(..., alias, ... )`
   - local host(`1CDB...`)쪽 HTTP 인스턴스는 제거 시도

### 현재 기대 로그
- `mDNS alias-only host: esp-matter-hub-<suffix> (local host hidden for HTTP)`
- `mDNS ready: http://esp-matter-hub-<suffix>.local`

## 4) 웹/운영 로그 개선
- `app_main`에서 `IP_EVENT_STA_GOT_IP` 시 `Web UI ready (IP): http://<ip>` 로그 추가
- 브라우저 자동 요청 아이콘 경고 완화를 위해 204 응답 핸들러 추가
  - `/favicon.ico`
  - `/apple-touch-icon.png`
  - `/apple-touch-icon-precomposed.png`

## 5) 검증 커맨드

```bash
# mDNS 이름 해석
dns-sd -G v4 esp-matter-hub-659824.local

# HTTP 서비스 브라우징
dns-sd -B _http._tcp local

# 특정 인스턴스 확인
dns-sd -L "ESP Matter Hub UI" _http._tcp local

# API 확인
curl -v "http://esp-matter-hub-659824.local/api/health"
curl -s "http://192.168.75.231/api/health"
```

## 6) 현재 이슈/다음 확인 포인트
- 네트워크 환경에 따라 alias hostname과 기존 hostname(`1CDB...`) 해석 결과가 섞일 수 있음
- 최종 확인 기준은 `dns-sd -L "ESP Matter Hub UI" _http._tcp local` 결과 host가 alias인지 여부
- alias 미고정 시 local_discovery에서 delegated host 등록 상태/서비스 등록 순서를 추가 점검 필요
