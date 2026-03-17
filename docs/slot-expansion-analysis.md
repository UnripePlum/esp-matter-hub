# 브릿지 슬롯 확장 가능성 분석 보고서

> **작성일**: 2026-03-17
> **현재 슬롯 수**: 8개 (고정)
> **결론**: 확장 가능하나, IRAM 100% 사용이 핵심 병목

---

## 1. 현재 구조

```
Endpoint 0:  Root Node
Endpoint 1:  Aggregator (브릿지 부모)
Endpoint 2~9: Bridge Slot 0~7 (dimmable_light)  ← 8개
Endpoint 10: IrManagement Custom Cluster
```

`BRIDGE_SLOT_COUNT`는 `main/app_priv.h:35`에 `#define BRIDGE_SLOT_COUNT 8`로 정의되어 있으며, 프로젝트 전반에서 이 매크로를 참조합니다.

---

## 2. 제약 분석

### 2.1 IRAM — 핵심 병목

| 항목 | 값 |
|------|---|
| 전체 IRAM | 16,384 bytes |
| 사용 중 | 16,383 bytes |
| **잔여** | **1 byte** |

**영향**: 엔드포인트를 추가하면 esp-matter SDK 내부에서 추가 IRAM 코드/데이터가 생성됩니다. 현재 1바이트만 남아 있어 **슬롯 1개만 추가해도 링크 실패할 가능성이 높습니다.**

IRAM에 들어가는 항목:
- ISR (Interrupt Service Routines)
- `IRAM_ATTR` 함수들
- 벡터 테이블
- 일부 ESP-IDF 핵심 코드

### 2.2 DRAM (내부 RAM)

| 항목 | 값 |
|------|---|
| 전체 D/IRAM | 345,856 bytes |
| 사용 중 (static) | 201,379 bytes (58.2%) |
| **잔여 (힙)** | **144,477 bytes** |

**슬롯당 DRAM 비용:**

| 구성 요소 | 슬롯당 크기 | 비고 |
|-----------|-----------|------|
| `bridge_slot_state_t` | ~63 bytes | 슬롯 상태 구조체 |
| `bridge_slot_endpoint_ids[]` | 2 bytes | uint16_t |
| `s_last_level[]` + `s_last_level_valid[]` | 2 bytes | 레벨 캐시 |
| `slot_assignments[]` (NVS registry) | 4 bytes | uint32_t |
| dimmable_light endpoint (런타임) | ~500~1000 bytes | SDK 내부 할당 (힙) |
| **합계** | **~570~1070 bytes** | |

**결론**: DRAM은 여유 있음. 슬롯 4개 추가해도 ~4KB 정도로 144KB 힙에서 무시할 수준.

### 2.3 sdkconfig 제한

```
CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=12
```

현재 사용: root(1) + aggregator(1) + slot 8(8) + IrMgmt(1) = **11개**

| 슬롯 수 | 총 엔드포인트 | sdkconfig 변경 필요 |
|---------|-------------|-------------------|
| 8 (현재) | 11 | 불필요 (12 한도 내) |
| 9 | 12 | 불필요 (딱 맞음) |
| 10~12 | 13~15 | `MAX_DYNAMIC_ENDPOINT_COUNT` 증가 필요 |
| 16 | 19 | 증가 필요 (최대 255) |

**결론**: sdkconfig 값 변경으로 쉽게 해결. Matter 표준은 최대 254 엔드포인트 허용.

### 2.4 NVS (비휘발성 저장소)

```c
// bridge_action.cpp:26-32
typedef struct bridge_registry_store {
    uint32_t version;
    uint32_t next_device_id;
    uint32_t device_count;
    bridge_device_t devices[BRIDGE_MAX_REGISTERED_DEVICES]; // 16
    uint32_t slot_assignments[BRIDGE_SLOT_COUNT];            // 8 ← 여기
} bridge_registry_store_t;
```

**영향**: `BRIDGE_SLOT_COUNT` 변경 시 `slot_assignments` 배열 크기가 바뀌므로, 기존 NVS에 저장된 레지스트리 데이터와 **구조 불일치** 발생.

**해결 방법**:
- `version` 필드 증가 + 마이그레이션 로직 추가
- 또는 factory reset 후 재설정 (기존 바인딩 소실)

### 2.5 PSRAM (외부 RAM)

| 항목 | 값 |
|------|---|
| PSRAM 전체 | 8 MB |
| 현재 사용 | CHIP/mbedTLS/NimBLE/esp-matter 할당 |
| 잔여 | ~7+ MB (대부분 미사용) |

PSRAM은 충분히 여유 있으므로, 추가 슬롯의 런타임 할당을 PSRAM으로 보낼 수 있습니다.

### 2.6 ir_mgmt_cluster.cpp JSON 버퍼

| 버퍼 | 크기 | 영향 |
|------|------|------|
| `s_slots_json` | 2,048 bytes | 슬롯 수 증가 시 부족할 수 있음 |
| `s_signals_json` | 8,192 bytes | 슬롯과 무관 |
| `s_devices_json` | 4,096 bytes | 슬롯과 무관 |

슬롯 16개일 때 `s_slots_json` 예상 크기: ~1,600 bytes (현재 8슬롯 ~800 bytes) → 2,048 bytes 버퍼로 충분.

---

## 3. 확장 로드맵

### Phase 1: IRAM 확보 (필수, 선행 조건)

IRAM이 1바이트밖에 남지 않아 어떤 엔드포인트도 추가할 수 없습니다.

| 작업 | 예상 효과 | 난이도 |
|------|----------|--------|
| 불필요한 `IRAM_ATTR` 함수 제거/이동 | ~수백 바이트 확보 | 중 |
| esp-matter SDK의 `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` 확인 | ~수백 바이트 | 하 |
| `CONFIG_ESP_WIFI_IRAM_OPT=n` 비활성화 | ~수 KB 확보 | 하 (Wi-Fi 성능 약간 저하) |
| `CONFIG_ESP_WIFI_RX_IRAM_OPT=n` 비활성화 | ~수 KB 확보 | 하 |
| Flash에서 실행 가능한 함수를 `IRAM` → `.text`로 이동 | ~수백 바이트 | 중 |

**가장 쉬운 방법**: Wi-Fi IRAM 최적화 끄기. 성능에 미미한 영향이지만 수 KB 확보 가능.

### Phase 2: 코드 변경

| 파일 | 변경 내용 |
|------|----------|
| `main/app_priv.h` | `#define BRIDGE_SLOT_COUNT N` (원하는 수로 변경) |
| `sdkconfig` | `CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT` 증가 |
| `main/bridge_action.cpp` | NVS registry version 증가 + 마이그레이션 (또는 factory reset) |
| `main/ir_mgmt_cluster.cpp` | `s_slots_json` 버퍼 크기 확인 (16슬롯까지는 2048으로 충분) |

**주의**: 코드 자체는 `BRIDGE_SLOT_COUNT` 매크로를 일관되게 사용하므로, 매크로 값만 변경하면 모든 배열/루프가 자동 조정됩니다.

### Phase 3: 빌드 및 검증

1. `idf.py --build-dir build2 build` — 링크 성공 여부 확인 (IRAM 초과 시 실패)
2. `idf.py --build-dir build2 size` — 메모리 사용량 비교
3. Flash + Apple Home 커미셔닝 테스트
4. 추가 슬롯에 디바이스 할당 + IR 송신 테스트

---

## 4. 비용 추정

### 4.1 슬롯당 리소스 비용

| 리소스 | 슬롯당 비용 | 현재 여유 | 최대 추가 가능 |
|--------|-----------|----------|---------------|
| IRAM | ~200~500 bytes (추정) | 1 byte | **0** (확보 필요) |
| DRAM (static) | ~71 bytes | 144 KB | ~2,000+ |
| DRAM (heap/runtime) | ~500~1,000 bytes | 144 KB | ~140~280 |
| PSRAM | 0 (미사용) | ~7 MB | N/A |
| NVS | 4 bytes (slot_assignments) | 충분 | N/A |
| sdkconfig limit | 1 endpoint | 1개 잔여 | 설정 변경으로 해결 |

### 4.2 IRAM 확보 후 최대 슬롯 수 추정

| 시나리오 | IRAM 확보 | 추가 가능 슬롯 | 총 슬롯 |
|---------|----------|-------------|---------|
| Wi-Fi IRAM opt 끄기 | ~2~4 KB | 4~8개 | 12~16 |
| + RX IRAM opt 끄기 | ~4~8 KB | 8~16개 | 16~24 |
| + 코드 최적화 | ~6~10 KB | 12~20개 | 20~28 |

> **현실적 권장**: IRAM 최적화 끄고 **12~16슬롯**이 안정적 목표.

### 4.3 성능 영향

| 항목 | 영향 |
|------|------|
| Wi-Fi IRAM 최적화 비활성화 | 패킷 처리 지연 수 μs 증가 (체감 불가) |
| 엔드포인트 증가 | Apple Home 초기 로딩 시 더 많은 엔드포인트 조회 |
| Matter 구독 | 컨트롤러당 구독 수 증가 → 메모리 소비 증가 |
| IR 송신 | 영향 없음 (슬롯 수와 무관) |

---

## 5. 결론

### 8개로 제한된 이유 요약

| 원인 | 유형 | 현재 해소 가능? |
|------|------|---------------|
| **IRAM 100% 사용** (1바이트 잔여) | 하드웨어 제약 | Wi-Fi IRAM opt 비활성화로 해소 가능 |
| sdkconfig `MAX_DYNAMIC_ENDPOINT_COUNT=12` | 설정 제약 | 값 변경으로 즉시 해소 |
| NVS 레지스트리 고정 크기 배열 | 소프트웨어 설계 | 버전 마이그레이션으로 해소 |
| 초기 설계 결정 (안정성 우선) | 설계 철학 | 검증 후 변경 가능 |

### 핵심 판단

- **코드 구조상 확장은 간단**: `BRIDGE_SLOT_COUNT` 매크로를 변경하면 모든 코드가 자동 조정
- **IRAM이 유일한 하드 블로커**: Wi-Fi IRAM 최적화 비활성화로 해소 가능
- **권장 최대값**: 12~16슬롯 (IRAM 확보 + 안정성 마진)
- **Matter 표준 한도**: 254 엔드포인트 (실질적으로 무제한)

### 확장 작업 예상 공수

| 작업 | 시간 |
|------|------|
| IRAM 확보 (sdkconfig 변경) | 30분 |
| BRIDGE_SLOT_COUNT 변경 + 빌드 테스트 | 1시간 |
| NVS 마이그레이션 (선택) | 2시간 |
| Apple Home + chip-tool 검증 | 1시간 |
| **총 예상** | **2~4시간** |
