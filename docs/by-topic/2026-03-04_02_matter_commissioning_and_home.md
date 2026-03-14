# 1) Matter 커미셔닝 및 Apple Home 연동

## 목표
- ESP32-S3 허브를 Matter 액세서리로 정상 커미셔닝
- Apple Home에서 제어 가능한 상태 확보

## 오늘 수행 내용
- BLE 기반 커미셔닝 반복 검증
- Factory reset 이후 재커미셔닝 플로우 확인
- Apple Home에서 슬롯 제어 시 디바이스 로그 반응 확인
  - 예: `app_driver: Slot 0 action: level=112`

## 확인된 사실
- `matter esp factoryreset` 실행 후에는 Wi-Fi 자격증명이 지워져 IP가 사라짐
- 이 상태에서는 HTTP API 접속 불가가 정상 동작
- 재커미셔닝 성공 후 `IP_EVENT_STA_GOT_IP` + `sta ip: ...` 로그가 뜨면 API 접근 가능

## 이슈
- macOS `chip-tool` 경로에서 BLE GATT write 실패(0x407)와 PASE timeout 반복 발생
- 따라서 실사용 플로우는 Apple Home 우선 페어링으로 정리

## 현재 권장 절차
1. 보드 부팅 후 커미셔닝 윈도우 확인
2. Apple Home으로 Matter 페어링
3. 시리얼에서 IP 할당 확인
4. IP로 웹/API 접근 및 슬롯 매핑/학습 진행

