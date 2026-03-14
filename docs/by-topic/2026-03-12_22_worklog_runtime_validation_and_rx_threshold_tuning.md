# 2026-03-12 작업 정리 (4): 런타임 검증 및 RX threshold 튜닝

## 개요
- 펌웨어를 반복 플래시/모니터링하며 런타임 동작을 점검함
- 하드웨어 셀프테스트(`hw_selftest.sh`)로 API/IR 경로를 검증함
- RX 캡처 임계값을 현장 조건에 맞게 조정함

## 수행 내용

### 1) 반복 플래시/모니터 실행
- `run_esp32-s3`를 사용해 빌드/플래시/모니터를 반복 수행
- 부팅 로그, Wi-Fi 연결, Matter 이벤트, LED 상태 전환 로그를 확인

### 2) HW 셀프테스트 실행 및 관찰
- `./hw_selftest.sh esp-matter-hub-66c524.local` 실행
- 관찰 결과:
  - `health`, `slots_contract` 등 API 기본 항목 PASS
  - RX2 캡처는 성공 사례 확인
  - RX1 캡처 timeout이 반복되어 하드웨어 경로 점검 필요성 확인
  - delete 이후 후속 파싱 구간에서 JSON decode 오류 발생 사례 확인

### 3) LED 상태/표시 이슈 디버깅
- `health`의 `led_state`와 실물 LED 색상이 다르게 보이는 사례 점검
- 상태 프레임을 주기적으로 재적용하도록 변경하여 외부 간섭 시 복구되도록 보강

### 4) RX 캡처 기준 조정
- `main/ir_engine.cpp`
  - `kMinCapturePayloadLen`을 채널 공통 `4`로 조정
  - 변경 전: `{6, 8}`
  - 변경 후: `{4, 4}`

## 현재 상태 요약
- Aggregator/NodeLabel/온보드 LED 기반 구조는 유지
- RX2는 캡처 성공 사례가 있으나 RX1은 현장 조건에서 불안정
- RX threshold 완화 반영 완료(추가 실기 검증 필요)

## 후속 권장
1. RX1/RX2 모듈 교차 연결 테스트로 모듈/보드 경로 원인 분리
2. `hw_selftest.sh`의 delete 이후 JSON 파싱 안정화 보완
3. threshold=4 적용 상태에서 오검출(노이즈) 증가 여부 장시간 관찰
