# API JSON Schema: `/api/export/nvs`

## 개요
- 목적: ESP32-S3 Hub의 NVS 데이터를 Wi-Fi HTTP로 백업(export)
- 메서드: `GET`
- 경로: `/api/export/nvs`
- 쿼리:
  - `scope=all|signals|bindings|devices`
  - 기본값: `all`
- 인증: 1차 구현 기준 무인증(로컬 네트워크 전용)

## 동작 규칙(엄격 모드)
- `signals`를 포함하는 export에서 신호 하나라도 payload 무결성 검증 실패 시 전체 요청 실패
- 실패 시 HTTP `500` + 에러 JSON 반환
- 검증 규칙:
  - payload 로드 성공
  - `payload_len > 0`
  - `payload_len == payload_ticks.length`

## 성공 응답 스키마 (HTTP 200)

```json
{
  "schema_version": 1,
  "board_id": "string",
  "exported_at_unix": 0,
  "scope": "all|signals|bindings|devices",
  "counts": {
    "signals": 0,
    "bindings": 0,
    "devices": 0
  },
  "signals": [
    {
      "signal_id": 0,
      "name": "string",
      "device_type": "string",
      "carrier_hz": 0,
      "repeat": 0,
      "payload_len": 0,
      "payload_ticks": [0]
    }
  ],
  "bindings": [
    {
      "slot_id": 0,
      "endpoint_id": 0,
      "role": "string",
      "on_signal_id": 0,
      "off_signal_id": 0,
      "level_up_signal_id": 0,
      "level_down_signal_id": 0
    }
  ],
  "devices": []
}
```

### 필드 설명
- `schema_version` (number): export 포맷 버전
- `board_id` (string): 허브 식별자(호스트명)
- `exported_at_unix` (number): export 시점 Unix epoch seconds
- `scope` (string): 실제 적용된 scope
- `counts.signals` (number): `signals` 항목 개수
- `counts.bindings` (number): `bindings` 항목 개수
- `counts.devices` (number): 현재 구현에서는 `0` (향후 확장용)
- `signals[].payload_ticks` (number[]): IR raw duration tick 배열

## 실패 응답 스키마 (HTTP 500)

### payload 무결성 실패

```json
{
  "status": "error",
  "code": "EXPORT_PAYLOAD_INTEGRITY_FAILED",
  "signal_id": 12
}
```

### 기타 서버 내부 에러
- `httpd_resp_send_err` 기본 포맷(plain text) 반환 가능

## 쿼리 scope별 반환
- `scope=all`
  - `signals`, `bindings`, `devices` 모두 반환
- `scope=signals`
  - `signals`만 유효 데이터, `bindings`/`devices`는 빈 배열
- `scope=bindings`
  - `bindings`만 유효 데이터, `signals`/`devices`는 빈 배열
- `scope=devices`
  - 현재 구현에서는 `devices` 빈 배열(확장 예정)

## 예시

### 전체 export

```bash
curl -s "http://<hub-host>/api/export/nvs?scope=all"
```

### signals만 export

```bash
curl -s "http://<hub-host>/api/export/nvs?scope=signals"
```

### 무결성 확인 포인트
- 각 signal에 대해 `payload_len == len(payload_ticks)`인지 확인
