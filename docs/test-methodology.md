# 테스트 방법론: ESP Matter Hub IR Bridge

## 1. chip-tool 테스트 시나리오

### 사전 준비

```bash
# 환경 변수 설정
NODE_ID=<commissioning 완료된 노드 ID>
ENDPOINT=<테스트 대상 엔드포인트 ID>
FABRIC_ID=1
```

---

### 1.1 SendSignalWithRaw 정상 송신

#### OnOff On 송신 (is_level=false, high_low=true)

```bash
chip-tool irmanagement send-signal-with-raw \
  <device_id> <signal_id> 0 1 \
  <payload_hex> <carrier_hz> <repeat> \
  $NODE_ID $ENDPOINT
```

**기대 결과:** 디바이스 on_signal_id가 signal_id로 바인딩되고 IR 송신 성공. 응답 status=0x00.

#### OnOff Off 송신 (is_level=false, high_low=false)

```bash
chip-tool irmanagement send-signal-with-raw \
  <device_id> <signal_id> 0 0 \
  <payload_hex> <carrier_hz> <repeat> \
  $NODE_ID $ENDPOINT
```

**기대 결과:** off_signal_id 바인딩 후 IR 송신 성공. 응답 status=0x00.

#### Level Up 송신 (is_level=true, high_low=true)

```bash
chip-tool irmanagement send-signal-with-raw \
  <device_id> <signal_id> 1 1 \
  <payload_hex> <carrier_hz> <repeat> \
  $NODE_ID $ENDPOINT
```

**기대 결과:** level_up_signal_id 바인딩 후 IR 송신 성공. 응답 status=0x00.

#### Level Down 송신 (is_level=true, high_low=false)

```bash
chip-tool irmanagement send-signal-with-raw \
  <device_id> <signal_id> 1 0 \
  <payload_hex> <carrier_hz> <repeat> \
  $NODE_ID $ENDPOINT
```

**기대 결과:** level_down_signal_id 바인딩 후 IR 송신 성공. 응답 status=0x00.

---

### 1.2 캐시 히트 확인

같은 signal_id로 두 번 송신하면 두 번째 송신 시 RMT 재초기화 없이 캐시에서 직접 송신되어야 한다.

```bash
# 첫 번째 송신 (캐시 미스 - RMT 초기화)
chip-tool irmanagement send-signal-with-raw \
  <device_id> <signal_id> 0 1 <payload> <carrier_hz> <repeat> \
  $NODE_ID $ENDPOINT

# 두 번째 송신 (캐시 히트)
chip-tool irmanagement send-signal-with-raw \
  <device_id> <signal_id> 0 1 <payload> <carrier_hz> <repeat> \
  $NODE_ID $ENDPOINT
```

**기대 결과:** 두 번째 송신 시 시리얼 로그에 `cache hit signal_id=<signal_id>` 포함. 응답 status=0x00.

**확인 방법:**
```
idf_monitor 출력에서 "cache hit" 문자열 검색
```

---

### 1.3 is_level 불일치 거부

OnOff 타입으로 등록된 device_id에 Level 신호를 보내면 거부되어야 한다.

```bash
# 먼저 OnOff 타입으로 등록
chip-tool irmanagement send-signal-with-raw \
  <device_id> <signal_id_1> 0 1 <payload> <carrier_hz> <repeat> \
  $NODE_ID $ENDPOINT

# 같은 device_id에 is_level=true로 재시도 → 거부 예상
chip-tool irmanagement send-signal-with-raw \
  <device_id> <signal_id_2> 1 1 <payload> <carrier_hz> <repeat> \
  $NODE_ID $ENDPOINT
```

**기대 결과:** 두 번째 커맨드 응답 status=0x85 (INVALID_COMMAND).

---

### 1.4 device_id=0 거부

```bash
chip-tool irmanagement send-signal-with-raw \
  0 <signal_id> 0 1 <payload> <carrier_hz> <repeat> \
  $NODE_ID $ENDPOINT
```

**기대 결과:** 응답 status=0x85 (INVALID_COMMAND). device_id 0은 예약값이므로 반드시 거부.

---

### 1.5 빈 ticks 거부

payload_hex를 빈 바이트 배열로 전달:

```bash
chip-tool irmanagement send-signal-with-raw \
  <device_id> <signal_id> 0 1 "" <carrier_hz> <repeat> \
  $NODE_ID $ENDPOINT
```

**기대 결과:** 응답 status=0x85 (INVALID_COMMAND). payload 길이 0은 유효하지 않은 IR 신호.

---

### 1.6 자동 등록 + 슬롯 할당 확인

신규 device_id로 SendSignalWithRaw를 호출하면 자동으로 디바이스가 등록되고 슬롯에 할당되어야 한다.

```bash
NEW_DEVICE_ID=9999

chip-tool irmanagement send-signal-with-raw \
  $NEW_DEVICE_ID <signal_id> 0 1 <payload> <carrier_hz> <repeat> \
  $NODE_ID $ENDPOINT
```

**확인 절차:**
1. 응답 status=0x00 확인
2. HTTP API `GET /api/slots` 호출 → slots 배열에서 assigned_device_id=9999 항목 확인
3. 시리얼 로그에서 `auto-register device_id=9999` 로그 확인

---

### 1.7 재부팅 후 Apple Home 토글 동작 (NVS 바인딩 복구)

1. SendSignalWithRaw로 device_id, signal_id 바인딩 설정
2. 디바이스 재부팅: `idf.py monitor` 세션에서 리셋
3. 재부팅 완료 후 Apple Home 앱에서 해당 슬롯 토글
4. 시리얼 로그에서 IR 송신 로그 확인

**기대 결과:** NVS에서 바인딩이 복구되어 Apple Home 토글 시 정상적으로 IR 신호 송신.

**확인 로그 키워드:**
```
bridge_action: Initialized bridge actions: slots=8 devices=N
ir_engine: send signal_id=<id>
```

---

### 1.8 SaveSignal/GetSignalPayload 백업/복구 흐름

#### SaveSignal (0x09) 호출

```bash
chip-tool irmanagement save-signal \
  <signal_id> <name> <device_type> <carrier_hz> <repeat> <payload_hex> \
  $NODE_ID $ENDPOINT
```

**기대 결과:** 응답에 assigned_signal_id 포함, status=0x00.

#### GetSignalPayload (0x0A) 호출

```bash
chip-tool irmanagement get-signal-payload \
  <signal_id> \
  $NODE_ID $ENDPOINT
```

**기대 결과:** 저장된 payload_ticks 배열 반환, carrier_hz, repeat 일치 확인.

#### 복구 흐름 검증

1. SaveSignal로 여러 신호 저장
2. 재부팅
3. GetSignalPayload로 각 신호 payload 조회
4. 저장 시 payload와 일치 확인

---

### 1.9 CacheStatus 속성 읽기

```bash
chip-tool irmanagement read cache-status $NODE_ID $ENDPOINT
```

**기대 결과:** 현재 캐시 상태 반환. signal_id, hit_count, miss_count 등 포함.

캐시 히트/미스 발생 후 속성값 변화 확인:
```bash
# 몇 번의 송신 후
chip-tool irmanagement read cache-status $NODE_ID $ENDPOINT
# hit_count 증가 확인
```

---

## 2. HTTP API 테스트 (유지된 엔드포인트)

### 2.1 GET /api/health

```bash
curl http://<esp-ip>/api/health
```

**기대 응답:**
```json
{
  "status": "ok",
  "service": "esp-matter-hub",
  "slots": 8,
  "hostname": "esp-matter-hub",
  "fqdn": "esp-matter-hub.local",
  "mdns": "ready",
  "led_state": "idle"
}
```

---

### 2.2 POST /api/learn/start

```bash
curl -X POST http://<esp-ip>/api/learn/start \
  -H "Content-Type: application/json" \
  -d '{"timeout_s": 15}'
```

**기대 응답:** `{"status":"ok","learning":"started"}`

중복 호출 시: HTTP 409, `{"status":"error","message":"learning already in progress"}`

---

### 2.3 GET /api/learn/status

```bash
curl http://<esp-ip>/api/learn/status
```

**기대 응답 (학습 중):**
```json
{
  "state": "in_progress",
  "elapsed_ms": 3200,
  "timeout_ms": 15000,
  "last_signal_id": 0,
  "rx_source": 0,
  "captured_len": 0,
  "quality_score": 0
}
```

**기대 응답 (캡처 완료):** state="ready", captured_len > 0

---

### 2.4 POST /api/learn/commit

리모컨 버튼을 누른 후 (state=ready):

```bash
curl -X POST http://<esp-ip>/api/learn/commit \
  -H "Content-Type: application/json" \
  -d '{"name": "tv_power", "device_type": "tv"}'
```

**기대 응답:** `{"status":"ok","signal_id":N}`

state가 ready가 아닌 경우: HTTP 409, `{"status":"error","message":"no pending learned signal"}`

---

### 2.5 GET /api/signals

```bash
curl http://<esp-ip>/api/signals
```

**기대 응답:**
```json
{
  "signals": [
    {
      "signal_id": 1,
      "name": "tv_power",
      "device_type": "tv",
      "carrier_hz": 38000,
      "repeat": 1,
      "payload_len": 68
    }
  ]
}
```

---

### 2.6 DELETE /api/signals/{id}

```bash
curl -X DELETE http://<esp-ip>/api/signals/1
```

**기대 응답:** `{"status":"ok","signal_id":1}`

존재하지 않는 signal_id: HTTP 404

**사이드 이펙트 확인:** 삭제된 signal_id를 참조하는 바인딩이 자동으로 0으로 클리어되는지 `GET /api/slots`로 확인.

---

### 2.7 POST /api/commissioning/open

```bash
curl -X POST http://<esp-ip>/api/commissioning/open \
  -H "Content-Type: application/json" \
  -d '{"timeout_s": 300}'
```

**기대 응답:** `{"status":"ok","commissioning_window":"opened","timeout_s":300}`

---

## 3. 에러 시나리오 전체 목록

| 시나리오 | 입력 조건 | 기대 에러 코드 | 비고 |
|---------|----------|--------------|------|
| device_id=0 | SendSignalWithRaw device_id=0 | 0x85 INVALID_COMMAND | 예약된 ID |
| 빈 payload | ticks 배열 길이 0 | 0x85 INVALID_COMMAND | 유효하지 않은 IR 신호 |
| is_level 타입 불일치 | OnOff 등록 기기에 is_level=true | 0x85 INVALID_COMMAND | 타입 고정 후 변경 불가 |
| 슬롯 전체 사용 | device_count > BRIDGE_MAX_REGISTERED_DEVICES(16) | slot=0xFF (경고, 계속) | 슬롯 없으면 경고 후 out_slot_id=0xFF |
| 디바이스 등록 한도 초과 | 17번째 auto-register | ESP_ERR_NO_MEM | IR 송신 불가 |
| 신호 저장 공간 부족 | learn/commit 신호 수 초과 | HTTP 507 | `{"status":"error","message":"signal storage full"}` |
| 학습 중 중복 시작 | 학습 중 POST /api/learn/start | HTTP 409 | already in progress |
| 학습 미완료 커밋 | state != ready 시 commit | HTTP 409 | no pending learned signal |
| 존재하지 않는 signal 삭제 | DELETE /api/signals/9999 | HTTP 404 | signal not found |
| 잘못된 캐리어 주파수 | carrier_hz=0 | 0x85 INVALID_COMMAND | IR 송신 불가 |
| SaveSignal 중복 signal_id | 기존 signal_id로 재저장 | 0x85 또는 덮어쓰기 | 구현 정책에 따라 다름 |
| GetSignalPayload 미존재 | 없는 signal_id 조회 | 0x8B NOT_FOUND | 응답에 signal_id 포함 |
| commissioning 윈도우 열기 실패 | Matter 스택 미시작 상태 | HTTP 500 | failed to open commissioning window |
