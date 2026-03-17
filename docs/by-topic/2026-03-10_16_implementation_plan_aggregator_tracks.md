# 2026-03-10 구현 계획: Apple Home 대응을 위한 운영/개편 트랙

> **상태**: Track A~C 전부 완료 (2026-03-12). 본 문서는 계획 단계의 기록이며, 구현 결과는 `2026-03-12_19_worklog_aggregator_transition_and_slot_model.md` 참조.

## 목적
- 운영 라인 안정성을 유지하면서 Apple Home UX 한계를 검증 가능한 방식으로 개선
- 고정 8슬롯 API 계약을 깨지 않고 Aggregator/Bridge 전환 가능성 평가

## 전제
- 운영 라인(`main`): 고정 8 슬롯 평면 endpoint 구조 유지
- 실험 라인: Aggregator/Bridge 아키텍처 별도 브랜치에서 검증
- 병합 조건: UX 개선 + 안정성 유지 + API 회귀 0

## 트랙 구성

### Track A: 운영 라인 안정화
목표:
- 현재 릴리스 기능을 회귀 없이 고정

대상:
- `main/web_server.cpp`
- `main/bridge_action.cpp`
- `main/ir_engine.cpp`
- 관련 문서

구현 항목:
1. `DELETE /api/signals/{id}` 및 cascade unbind 동작 문서 반영
2. 대시보드 Signals Delete 동작 절차 명문화
3. API/문서/테스트 시나리오 정합성 통일

완료 기준:
- `/api/slots` 8개 고정 확인
- signal 삭제 시 참조 ID(`on/off/level_up/level_down`) 자동 `0`
- 문서와 실제 동작 불일치 0

### Track B: Aggregator PoC (별도 브랜치)
목표:
- Apple Home에서 child accessory 분리 노출 및 식별성 개선 검증

구현 항목:
1. Root 하위 Aggregator endpoint 1개 생성
2. 슬롯 endpoint를 bridged child로 생성 후 parent 연결
3. slot id(0~7)와 내부 endpoint id 매핑 계층 유지
4. assign/rename/unassign 시 `display_name` -> NodeLabel 동기화

완료 기준:
- 기존 `/api/slots` 계약 유지(8 슬롯, slot_id 기준)
- Apple Home 타일 분리 노출 여부 확인 가능

### Track C: Aggregator 안정화
목표:
- 재부팅/복원/초기화 타이밍 문제로 인한 NodeLabel 유실 및 crash 방지

구현 항목:
1. create/resume 경로에서 NodeLabel 재주입 로직 적용
2. 초기 부팅 시 attribute update 타이밍 안전화
3. endpoint/label/update 결과를 추적 가능한 로그 표준화

완료 기준:
- 재부팅 반복 테스트(NodeLabel 유실 0)
- 크래시(LoadProhibited 등) 0

### Track D: 인증/제품화 전략
목표:
- 최상위 이름(`Matter Accessory`) 이슈의 비기술적 한계 대응

구현 항목:
1. CSA membership/VID/PID/DCL 메인넷 등록 계획 분리
2. 테스트 기기와 인증 기기 로드맵 분리

완료 기준:
- 인증 준비 문서(비용/기간/리스크) 확정

## 구현 순서
1. Track A 회귀/문서 정합성 완료
2. Track B PoC 구현 및 Apple Home 실측
3. Track C 안정화(복원/크래시 방어)
4. 병합 게이트 평가 후 유지/병합/보류 결정

## 병합 게이트(필수 통과)
1. UX: 식별성/배치 제어가 기존 대비 명확히 개선
2. 안정성: 재부팅/전원복구에서 NodeLabel 유실 없음
3. 기능: `/api/slots`, `/api/devices`, `/api/signals` 회귀 없음
4. 운영성: 복잡도 증가가 유지 가능한 수준

## 테스트 매트릭스(요약)
- 기능 회귀: assign/rename/unassign/bind/unbind/delete signal
- Apple Home: 최상위 명칭/하위 타일/정렬 및 방 배치
- 내구성: 재부팅 N회, 전원 차단 복구, 대량 API 호출
- 위생 절차: 필요 시 clean build + `erase-flash` 검증 포함

## 리스크 및 대응
- 컨트롤러 렌더링 정책으로 PoC 효과 제한 가능
  - 대응: 재커미셔닝 포함 A/B 비교 측정
- NVS 잔존 데이터로 결과 왜곡 가능
  - 대응: 클린 플래시 절차를 테스트 케이스에 포함
- topology 변경으로 API 회귀 발생 가능
  - 대응: `/api/slots` 계약 보호 테스트 선행
