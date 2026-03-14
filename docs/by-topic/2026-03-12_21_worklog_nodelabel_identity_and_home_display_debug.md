# 2026-03-12 작업 정리 (3): NodeLabel/UniqueID 및 Apple Home 표시명 이슈 디버깅

## 관측된 문제
- 슬롯에 `device_0~7`를 할당했지만 Apple Home에서는 일부가 `Slot n`으로 표시
- 초기에는 로그상 sync 성공처럼 보이는데 readback이 기본값인 케이스 확인

## 분석 결과
- 표시명 원천은 signal 이름이 아니라 Matter `NodeLabel`
- `display_name`과 `NodeLabel` readback 불일치가 실제로 발생
- 일부 구간에서 `ESP_ERR_NO_MEM`/`ESP_ERR_NOT_FINISHED`가 NodeLabel write 경로에서 관찰됨

## 반영 내용

### 슬롯 식별자 안정화
- `main/app_main.cpp`
  - 슬롯별 `BridgedDeviceBasicInformation.UniqueID` 설정
  - 값: `bridge-slot-0` ~ `bridge-slot-7`

### NodeLabel 동기화 경로 강화
- `main/bridge_action.cpp`
  - sync 후 readback 검증 로그 추가 (`matched`, `readback`)
  - 슬롯 identity dump 추가
    - `display_name`, `node_label`, `unique_id`를 한 번에 출력
  - NodeLabel write는 필요할 때만 시도(이미 동일하면 skip)
  - `ESP_ERR_NO_MEM` 재시도
  - `ESP_ERR_NOT_FINISHED`는 성공 경로로 취급

### 초기/지연 동기화
- `main/app_main.cpp`
  - 초기 sync + identity dump
  - 부팅 후 지연 재동기화 task 추가

## 검증 방법
- 부팅 로그에서 `Slot identity dump` 확인
  - 기대: `display_name='device_n'`와 `node_label='device_n'` 일치
- Apple Home 재페어링 후 child 이름 반영 재확인
