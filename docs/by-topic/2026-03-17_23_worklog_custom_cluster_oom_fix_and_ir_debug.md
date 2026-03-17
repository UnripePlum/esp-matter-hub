# 2026-03-17 작업 정리: Custom Cluster 구현, OOM 수정, IR 송신 디버깅

## 개요
- IrManagement Matter Custom Cluster (0x1337FC01) 구현 및 endpoint 10에 등록
- Apple Home 페어링 실패 — 원인: 내부 DRAM 부족으로 AddNOC 단계에서 mbedTLS OOM
- CONFIG_CHIP_MEM_ALLOC_MODE_EXTERNAL=y 적용으로 해결
- IR 송신 불가 신고 — 원인: 하드웨어 문제 (코드 변경 없음 확인)

## 수행 내용

### 1) IrManagement Custom Cluster 구현
- `main/ir_mgmt_cluster.h` / `main/ir_mgmt_cluster.cpp` 신규 생성 (~465줄)
- Cluster ID: `0x1337FC01` (Vendor prefix 0x1337 + suffix 0xFC01)
- Endpoint 10 (동적 생성), Device Type ID: `0xFFF10001`
- Descriptor cluster 포함

속성 5개:
| ID | 이름 | 타입 |
|----|------|------|
| 0x0000 | LearnState | enum8 |
| 0x0001 | LearnedPayload | long_char_string |
| 0x0002 | SavedSignalsList | long_char_string (JSON) |
| 0x0003 | SlotAssignments | long_char_string (JSON) |
| 0x0004 | RegisteredDevicesList | long_char_string (JSON) |

커맨드 9개:
| ID | 이름 | 검증 결과 |
|----|------|-----------|
| 0x00 | StartLearning | PASS |
| 0x01 | CancelLearning | PASS (의도적 실패) |
| 0x02 | SaveSignal | PASS |
| 0x03 | DeleteSignal | PASS |
| 0x04 | AssignSignalToDevice | PASS |
| 0x05 | RegisterDevice | PASS |
| 0x06 | RenameDevice | PASS |
| 0x07 | AssignDeviceToSlot | PASS |
| 0x08 | OpenCommissioning | PASS |

이벤트 1개: `0x0000 LearningCompleted`

구현 중 발견한 제약사항:
- **Cluster ID suffix 범위**: vendor-specific cluster의 하위 16비트는 0xFC00~0xFFFE여야 함. 초기 0x13370001은 suffix가 0x0001이라 무효 → 0x1337FC01로 변경.
- **Override callback 미지원**: SDK `set_override_callback()`은 LONG_CHAR_STRING 타입을 거부함. Push 모델(`attribute::set_val()` + `refresh_all_attributes()`)로 대체.
- **Commissioning window 데드락**: HTTP 서버 스레드에서 직접 `OpenBasicCommissioningWindow()` 호출 시 데드락 발생. `PlatformMgr().ScheduleWork()` 패턴으로 해결.
- **Descriptor cluster 필수**: 빈 endpoint에 descriptor cluster 없으면 UNSUPPORTED_CLUSTER 에러.

### 2) Apple Home 페어링 실패 (OOM) — 근본 원인 분석 및 수정

#### 증상
- Factory reset 후 Apple Home 페어링 시도 → "Pairing Failed"
- 모니터 로그에서 확인된 에러:
```
E chip[CR]: mbedTLS error: BIGNUM - Memory allocation failed
E chip[FP]: Failed NOC chain validation, VerifyCredentials returned: 14
E chip[ZCL]: OpCreds: Failed AddNOC request (err=50) with OperationalCert error 3
```

#### 원인 분석
커미셔닝 플로우는 AddNOC(인증서 검증) 직전까지 정상 진행:
- BLE 연결 ✅ → PASE ✅ → ArmFailSafe ✅ → Attestation ✅ → CSR ✅ → AddTrustedRootCert ✅ → **AddNOC ❌ OOM**

메모리 분석 (`idf.py size`):
```
Used static IRAM:   16,383 bytes (1 remain — 100.0% full)
Used stat D/IRAM:  201,379 bytes (144,477 remain — 58.2% used)
```

libmain.a(우리 코드) DRAM 사용량: 23,375 bytes (2위)
- `ir_mgmt_cluster.cpp` static 버퍼: 14,848 bytes (신규 추가)
  - `s_signals_json[8192]`, `s_devices_json[4096]`, `s_slots_json[2048]`, `s_payload_json[512]`
- `ir_engine.cpp` signal table: ~6,200 bytes
- `bridge_action.cpp` registry: ~1,850 bytes

PSRAM 설정 상태:
```
CONFIG_ESP_MATTER_MEM_ALLOC_MODE_EXTERNAL=y   ✅ esp-matter → PSRAM
CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y    ✅ NimBLE → PSRAM
CONFIG_CHIP_MEM_ALLOC_MODE_EXTERNAL            ❌ 미설정 → CHIP/mbedTLS는 내부 DRAM만 사용
```

mbedTLS 인증서 검증(BIGNUM 연산)은 CHIP 할당자를 통해 내부 DRAM에서만 할당. PSRAM 8MB가 남아있어도 접근 불가.

#### 수정
```
# sdkconfig.defaults에 추가
CONFIG_CHIP_MEM_ALLOC_MODE_EXTERNAL=y
```

이 한 줄로 CHIP/mbedTLS 할당이 PSRAM으로 전환되어 AddNOC 성공.

#### 교훈
- `CONFIG_ESP_MATTER_MEM_ALLOC_MODE_EXTERNAL`과 `CONFIG_CHIP_MEM_ALLOC_MODE_EXTERNAL`은 **별개** 설정. esp-matter 래퍼와 CHIP 코어(mbedTLS 포함)가 각각 다른 할당 모드를 사용.
- IRAM이 100% (1바이트 잔여)이므로 IRAM 사용량을 늘리는 코드 변경 시 링크 실패 주의.

### 3) IR 송신 불가 디버깅

#### 증상
- Apple Home에서 조명 토글 시 대상 기기(조명)가 반응하지 않음

#### 코드 변경 분석
`git diff HEAD~2`로 확인한 결과:
- `ir_engine.cpp` — **변경 없음**
- `bridge_action.cpp` — **변경 없음**
- `app_driver.cpp` — **변경 없음**
- `web_server.cpp` — **변경 없음**

IR TX 경로 전체가 수정되지 않았음.

#### NVS 데이터 확인
HTTP API로 확인한 결과 NVS 데이터 정상 존재:
- Signals: 7개
- Devices: 5개
- Slots: 8슬롯, 3개 할당됨

#### 시리얼 로그 캡처 검증
python3 serial 캡처 + chip-tool toggle 동시 실행으로 확인:
```
I app_driver: Slot 0 action: onoff=1
I ir_engine: TX signal_id=15 name=light_on slot=0 cluster=0x0006 attr=0x0000 len=67 repeat=1 carrier=38000
```

RMT TX가 정상 실행되고 있었음. **소프트웨어 문제 아님.**

#### 결론
- **하드웨어 문제**: IR LED 또는 구동 회로 이상으로 확인됨
- 사용자가 하드웨어 수정 후 정상 동작 확인

### 4) Multi-Fabric 페어링 검증
- Apple Home (fabric 1): ✅ 성공 (OOM 수정 후)
- chip-tool beta (fabric 2): ✅ 성공
  - 2nd fabric 페어링 절차: HTTP API로 commissioning window open → chip-tool pairing onnetwork
  - `curl -X POST http://<device-ip>/api/commissioning/open -d '{"timeout_s":300}'`

### 5) CLI 도구 업데이트
- `.hub_api_test_env.sh`: cluster ID 0x1337FC01, output filtering, `--commissioner-name beta`, help text 수정
- `.hub_api_test_repl.sh`: default endpoint 10으로 수정
- `open-local-web`: 삭제 (hub_api_test로 대체)

## 전체 테스트 결과 (12/12 PASS)

| # | 테스트 항목 | 결과 |
|---|-----------|------|
| 1 | Health API (/api/health) | ✅ status=ok, mDNS ready, LED ready |
| 2 | Signals API (/api/signals) | ✅ 7개 신호 반환 |
| 3 | Devices API (/api/devices) | ✅ 5개 디바이스 반환 |
| 4 | Slots API (/api/slots) | ✅ 8슬롯, 3개 할당됨 |
| 5 | Learn Status API (/api/learn/status) | ✅ idle 상태 |
| 6 | Commissioning Window (/api/commissioning/open) | ✅ opened |
| 7 | NVS Export (/api/export/nvs?scope=bindings) | ✅ 정상 반환 |
| 8 | Custom Cluster 속성 읽기 (chip-tool smoke) | ✅ 4개 속성 모두 읽기 성공 |
| 9 | StartLearning 커맨드 (0x00) → LearnState=1 | ✅ IN_PROGRESS 확인 |
| 10 | RegisterDevice (0x05) + RenameDevice (0x06) | ✅ 등록→이름변경 정상 |
| 11 | IR TX 검증 (chip-tool toggle → 시리얼 로그) | ✅ TX signal_id=15 carrier=38000 |
| 12 | Signals/Slots 읽기 + OpenCommissioning (0x08) | ✅ 7 signals, 8 slots, commission OK |

참고: 테스트 중 디바이스 IP가 192.168.75.179 → 192.168.75.46으로 변경됨 (DHCP). chip-tool KVS 삭제 후 재페어링으로 해결.

## Git 커밋
```
e7e9eb4 fix: use PSRAM for CHIP/mbedTLS allocs to resolve OOM during commissioning
8081824 feat: implement IrManagement custom cluster (0x1337FC01) with chip-tool CLI
```

## 현재 상태 요약
- Custom Cluster 0x1337FC01 동작 중 (endpoint 10)
- Apple Home + chip-tool multi-fabric 운용 가능
- PSRAM 기반 CHIP/mbedTLS 할당으로 commissioning 안정화
- IRAM 100% 사용 — 추가 IRAM 소비 코드 주의 필요
- IR TX/RX 정상 (HW 수정 완료)
- 디바이스 IP: 192.168.75.46 (DHCP, 변경될 수 있음)

## 후속 권장
1. static JSON 버퍼(14.8KB)를 `heap_caps_malloc(MALLOC_CAP_SPIRAM)`로 전환하면 내부 DRAM 여유 추가 확보 가능
2. chip-tool 페어링 타임아웃 대응: IP 변경 시 KVS 삭제 + commissioning window 재오픈 필요
3. API spec 문서(`docs/api-spec-ir-management-cluster.md`) 유지보수
4. `idf.py size` 실행 시 반드시 `esp-idf/export.sh` + `esp-matter/export.sh` 둘 다 source 필요 (ninja PATH 문제)
