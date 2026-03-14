# 3) 로컬 웹 대시보드 및 REST API

## 목적
- 네이티브 앱 없이 허브 자체 웹 UI로 학습/매핑/상태 조회 수행

## 오늘 기준 제공 API
- `GET /` : 내장 대시보드
- `GET /api/health` : 서비스 상태
- `GET /api/slots` : 슬롯/엔드포인트/바인딩 조회
- `POST /api/slots/{id}/bind` : 슬롯 바인딩 저장
- `POST /api/learn/start` : 학습 시작
- `GET /api/learn/status` : 학습 상태 조회
- `POST /api/learn/commit` : 학습 신호 커밋(신규)
- `GET /api/signals` : 신호 목록 조회(신규)
- `DELETE /api/signals/{id}` : 신호 삭제 + 참조 바인딩 cascade unbind
- `GET /api/export/nvs` : NVS export(JSON, payload 포함)
- `GET /api/devices` : 등록 기기 목록 조회
- `POST /api/devices/register` : light 기기 등록
- `POST /api/devices/{id}/rename` : 등록 기기 이름 변경
- `POST /api/endpoints/{slot}/assign` : endpoint-slot에 device 할당/해제
- `POST /api/commissioning/open` : 커미셔닝 윈도우 수동 오픈

`GET /api/health` 추가 필드:
- `hostname` : mDNS 호스트명 (예: `esp-matter-hub-a1b2c3`)
- `fqdn` : mDNS FQDN (예: `esp-matter-hub-a1b2c3.local`)
- `mdns` : mDNS 상태 (`ready`/`disabled`)

## 대시보드 업데이트
- 학습 시작/상태 조회 UI 유지
- 신호 커밋 입력 추가
  - `name`, `device_type`
- 신호 목록 테이블 추가
- Signals Delete 동작
  - Signals 테이블의 `Delete` 버튼 클릭
  - 확인 다이얼로그 승인 시 `DELETE /api/signals/{id}` 호출
  - 삭제 성공 후 signals/devices/slots 자동 새로고침
- 슬롯 바인딩 UI 개선
  - 섹션명: `Bind Signal to Slot`
  - `onSignalId`, `offSignalId`, `levelUpSignalId`, `levelDownSignalId` 선택
  - 기본값 `Unbind (0)` 제공

`POST /api/slots/{id}/bind` 요청 필드(현재):
- `on_signal_id`
- `off_signal_id`
- `level_up_signal_id`
- `level_down_signal_id`

`DELETE /api/signals/{id}` 동작(현재):
- 대상 signal 삭제 전, 등록 기기의 참조 필드 자동 정리(cascade unbind)
  - `on_signal_id`
  - `off_signal_id`
  - `level_up_signal_id`
  - `level_down_signal_id`
- 삭제 후 `GET /api/slots`에서 해당 참조값은 `0`으로 조회됨

기기 등록/할당(현재):
- endpoint 개수: 8 고정
- 등록 기기: 최대 16
- 기본 정책: 수동 할당
- 슬롯은 런타임 추가/삭제하지 않음 (웹 UI도 동일 정책)

`GET /api/slots` 응답(현재):
- `device_id` + `display_name` 포함
- `display_name` 동기화 규칙:
  - 할당 시: `display_name = device.name`
  - 미할당 시: `display_name = Slot {id}`
  - rename 시: 해당 device가 할당된 slot의 `display_name` 즉시 반영

`GET /api/export/nvs`:
- `scope=all|signals|bindings|devices`
- `signals[].payload_ticks` 포함
- 엄격 모드: payload 무결성 실패 시 전체 실패(HTTP 500)

## 접근 정책
- 현재 운영 기본값: IP + mDNS 병행
- mDNS hostname은 충돌 방지를 위해 `esp-matter-hub-<mac_suffix>` 규칙 사용
- 운영 스크립트 기준 접속: `./run_esp32-s3` -> `./open-local-web`
