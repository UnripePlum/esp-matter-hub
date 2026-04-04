# 2026-03-17 작업 정리: 메모리 감사 및 ir_engine 버그 수정

## 개요
- main/ 전체 소스 코드 대상 예방적 메모리 감사 수행
- 감사 범위: 힙 누수, 힙 단편화, 스택 오버플로우
- 대상: 장시간 상시 동작 IoT 디바이스 (수주~수개월 연속 가동)
- ir_engine.cpp에서 3건의 메모리/리소스 관련 버그 발견 및 수정
- 나머지 전체 파일은 문제 없음 확인

## 발견 및 수정된 버그

### 1) RMT 링버퍼 아이템 미반환 누수 (CRITICAL)

**위치:** `ir_engine.cpp` — `try_capture_from_rx()`

**문제:** `xRingbufferReceive()`가 non-null 포인터를 반환했지만 `item_size < sizeof(rmt_item32_t)`인 경우, `vRingbufferReturnItem()`이 호출되지 않아 링버퍼 슬롯이 영구적으로 누수됨. 장시간 운영 시 IR 학습 반복으로 2048바이트 링버퍼가 고갈될 수 있음.

**수정:**
```cpp
// 수정 전
if (!items || item_size < sizeof(rmt_item32_t)) {
    return false;
}

// 수정 후
if (!items) {
    return false;
}
if (item_size < sizeof(rmt_item32_t)) {
    vRingbufferReturnItem(s_rx_ringbufs[rx_index], items);
    return false;
}
```

### 2) RMT 드라이버 초기화 실패 시 부분 정리 누락 (MODERATE)

**위치:** `ir_engine.cpp` — `init_rmt_hw()`

**문제:** RX 채널 루프에서 두 번째 채널 설정 실패 시, 이미 설치된 TX 드라이버와 첫 번째 RX 드라이버가 정리되지 않음. `s_hw_initialized`가 false로 남아 재시도 시에도 채널이 점유된 상태로 복구 불가.

**수정:** `goto cleanup_rmt` 패턴으로 실패 시 모든 설치된 드라이버를 `rmt_driver_uninstall()`로 정리하도록 변경.

### 3) RX 시작 실패 시 이미 시작된 채널 미중지 (LOW)

**위치:** `ir_engine.cpp` — `ir_engine_start_learning()`

**문제:** `rmt_rx_start()`가 두 번째 채널에서 실패 시, 이미 시작된 첫 번째 채널이 중지되지 않아 불필요한 인터럽트가 발생하고 링버퍼에 미회수 데이터가 축적됨. 다음 학습 호출 시 `drain_rx_ringbuffers()`로 자체 복구되지만 비효율적.

**수정:** 실패 시 이미 시작된 채널을 `rmt_rx_stop()`으로 정리.

## 문제 없음 확인된 파일

| 파일 | 검증 결과 |
|------|-----------|
| web_server.cpp | malloc/free가 모든 경로(에러/goto cleanup)에서 균형. 유일한 동적 할당(export 핸들러)이 올바르게 정리됨 |
| ir_mgmt_cluster.cpp | 동적 할당 없음. 정적 JSON 버퍼(~15KB)만 사용 |
| bridge_action.cpp | 동적 할당 없음. 정적 레지스트리 구조체 + NVS |
| local_discovery.cpp | 동적 할당 없음. 정적 버퍼만 사용 |
| status_led.cpp | 단일 태스크 + LED 핸들 1회 생성. 누수 없음 |
| app_main.cpp | 자체 삭제 태스크(`vTaskDelete(nullptr)`) 올바르게 사용. mDNS 재시도 태스크 spinlock으로 레이스 프리 |
| app_driver.cpp | 동적 할당 없음 |

## 추가 확인 항목

### NVS 핸들 안전성
- ir_engine.cpp: 6개 NVS 함수 — 모든 코드 경로에서 `nvs_open()`/`nvs_close()` 균형 확인
- bridge_action.cpp: 3개 NVS 함수 — 동일하게 균형 확인

### FreeRTOS 리소스 수명주기
| 리소스 | 생성 | 해제 | 상태 |
|--------|------|------|------|
| ir_learning 태스크 (3072B) | init 1회 | 무한루프 (의도적) | OK |
| status_led 태스크 (6144B) | init 1회 | 무한루프 (의도적) | OK |
| mdns_retry 태스크 (3072B) | 필요시 | vTaskDelete(nullptr) | OK |
| slot_id_sync 태스크 (4096B) | 부팅 1회 | vTaskDelete(nullptr) | OK |

### 힙 단편화 위험도: LOW
- 프로젝트 전체가 거의 정적 버퍼만 사용
- 유일한 동적 할당(web_server export)은 임시적이며 즉시 해제

### 스택 오버플로우 위험도: LOW
- 모든 태스크 스택 여유 충분 (최소 2.5KB 이상 마진)
- 가장 큰 스택 지역 변수: `ir_engine_send_signal()` ~512B (payload_ticks[128] + items[64])

## 알려진 제한사항 (범위 밖, 향후 작업)
- IR 엔진 공유 상태(`s_learning`, `s_pending_payload` 등)가 `learning_task`와 Matter/HTTP 태스크에서 mutex 없이 접근됨 — 잠재적 데이터 레이스. 메모리 누수는 아니지만 동시성 안전성 개선 필요.

## 빌드 검증
- `idf.py -B build2 build` 성공
- `ir_engine.cpp.obj`만 재컴파일, `light.bin` 바이너리 정상 생성
