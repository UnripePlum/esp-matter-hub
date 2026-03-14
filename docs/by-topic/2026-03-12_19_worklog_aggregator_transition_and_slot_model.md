# 2026-03-12 작업 정리 (1): Aggregator 전환 및 슬롯 모델

## 배경
- 운영 라인에서 평면 슬롯 구조를 Aggregator 기반 bridged child 구조로 전환 요청
- 기존 고정 슬롯(8개) 운영 정책은 유지

## 반영 내용
- `main/app_main.cpp`
  - Aggregator endpoint 생성
  - 슬롯 endpoint를 `ENDPOINT_FLAG_BRIDGE`로 생성
  - 각 슬롯 endpoint의 parent를 aggregator로 연결
  - 생성 로그에 parent endpoint id 출력

## 유지한 정책
- 슬롯 수: 8개 고정 (`BRIDGE_SLOT_COUNT`)
- 슬롯별 endpoint 생성 및 bridge action 연동 흐름 유지
- 기존 REST API 계약(`/api/slots` 등) 유지

## 확인 포인트
- 부팅 로그에서 아래 패턴 확인
  - `Bridge aggregator created with endpoint_id ...`
  - `Bridge slot X created with endpoint_id Y (parent=...)`
- `/api/slots`에서 endpoint 8개가 정상 노출되는지 확인
