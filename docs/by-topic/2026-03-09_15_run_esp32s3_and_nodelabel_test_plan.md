# 2026-03-09 run_esp32-s3 실행 및 NodeLabel 동기화 테스트 순서

## 목적
- `run_esp32-s3` 기준으로 플래시/부팅/로그/웹/API/Matter 검증 순서를 표준화
- 고정 8슬롯 + 수동 할당 + `display_name <-> NodeLabel` 동기화를 단일 플로우로 검증

## 테스트 범위
- 포함: 부팅, mDNS, `/api/slots`, `/api/devices`, assign/unassign/rename, delete signal(cascade unbind), NodeLabel, 재부팅 복원
- 제외: 동적 슬롯 추가(비목표), 인증/권한/클라우드 연동

## 사전 조건
- 보드 연결 및 포트 확인(기본: `/dev/cu.usbserial-0001`)
- 같은 LAN의 테스트 PC 1대
- Matter 컨트롤러(Apple Home 또는 chip-tool) 1대

## 실행 순서

### 1) 플래시 + 모니터 실행
1. 프로젝트 루트에서 실행
   - `./run_esp32-s3`
   - 포트 지정 시: `./run_esp32-s3 /dev/cu.usbserial-0001`
2. 모니터에서 아래 로그 확인
   - `Bridge slot X created with endpoint_id Y` (0~7 총 8개)
   - `HTTP API started:`
   - `mDNS ready` 또는 `mDNS alias-only host`

Pass 기준:
- 빌드/플래시/부팅 성공
- 슬롯 8개 생성 로그 확인

### 2) 기본 API 헬스체크
1. `GET /api/health`
2. `GET /api/slots`
3. `GET /api/devices`

Pass 기준:
- `/api/health.status=ok`
- `/api/slots`는 정확히 8개(0~7)
- 각 슬롯에 `display_name` 필드 존재

### 3) 등록/할당/해제 흐름 검증
1. `POST /api/devices/register`로 기기 2개 등록
2. `POST /api/endpoints/0/assign`에 device A 할당
3. `POST /api/endpoints/1/assign`에 device B 할당
4. `GET /api/slots` 재조회
5. `POST /api/endpoints/1/assign`에 `device_id=0`으로 해제

Pass 기준:
- 할당된 슬롯: `display_name == device.name`
- 해제된 슬롯: `display_name == "Slot 1"` (해당 slot id 기준)

### 4) rename 동기화 검증
1. `POST /api/devices/{id}/rename` 호출
2. `GET /api/devices` 확인
3. `GET /api/slots` 확인

Pass 기준:
- devices 목록의 name 변경
- 해당 기기가 할당된 슬롯의 `display_name` 즉시 동일 반영

### 5) NodeLabel 동기화 검증(Matter)
1. 컨트롤러에서 slot endpoint NodeLabel 읽기
2. 위 3~4단계(assign/rename/unassign) 반복 후 NodeLabel 재조회

Pass 기준:
- 할당/rename 시 NodeLabel == `display_name`
- 해제 시 NodeLabel == `Slot {id}`

참고:
- 펌웨어는 우선 `BridgedDeviceBasicInformation(0x0039).NodeLabel(0x0005)` 갱신 시도,
  없으면 `BasicInformation(0x0028).NodeLabel(0x0005)`로 fallback

### 6) 응답 크기 안정성 검증(chunked)
1. `/api/slots`, `/api/devices`를 연속 호출(예: 각 200회)
2. 모니터에서 HTTP 500 로그 확인

Pass 기준:
- `response too large` 재발 0건
- HTTP 500 0건

### 7) 재부팅 복원 검증
1. 등록/할당/rename 상태 유지한 채 재부팅
2. `/api/devices`, `/api/slots` 비교
3. NodeLabel 재조회

Pass 기준:
- NVS 복원 정상
- `display_name`/NodeLabel 동기 상태 유지

### 8) Signal Delete + cascade unbind 검증
1. 사용 중인 signal 하나를 선택(예: slot bind에 연결된 `on_signal_id`)
2. `DELETE /api/signals/{id}` 호출
3. `GET /api/signals`에서 삭제 반영 확인
4. `GET /api/slots`에서 해당 signal 참조 필드 확인

Pass 기준:
- 삭제 API 200 응답
- signal 목록에서 대상 ID 미존재
- 참조 필드(`on/off/level_up/level_down`)가 자동 `0`으로 정리

## 빠른 검증 curl 예시
```bash
curl -s "http://<hub-host>/api/health"
curl -s "http://<hub-host>/api/slots"
curl -s "http://<hub-host>/api/devices"
curl -s -X POST "http://<hub-host>/api/devices/register" -H "Content-Type: application/json" -d '{"name":"Living Light","device_type":"light"}'
curl -s -X POST "http://<hub-host>/api/endpoints/0/assign" -H "Content-Type: application/json" -d '{"device_id":1}'
curl -s -X POST "http://<hub-host>/api/devices/1/rename" -H "Content-Type: application/json" -d '{"name":"Living Main"}'
curl -s -X POST "http://<hub-host>/api/endpoints/0/assign" -H "Content-Type: application/json" -d '{"device_id":0}'
curl -s -X DELETE "http://<hub-host>/api/signals/1"
```

## 실패 시 우선 점검
1. 슬롯 개수/URI 오입력 여부(0~7 범위)
2. `/api/devices/{id}/rename` 요청 body의 `name` 누락 여부
3. 모니터의 NodeLabel sync 경고 로그 여부
4. mDNS 실패 시 IP 직접 접속으로 API 재검증

## 현 계획(추가): Aggregator 개편 트랙
(완료: 2026-03-12 구현. 상세 내용은 2026-03-12_19 참조)
- 현재 릴리스 라인: 고정 8슬롯 평면 endpoint 구조 유지
- Aggregator 아키텍처는 별도 트랙으로 설계/검증 후 병합 판단

### 단계 A: PoC
1. Root 하위 Aggregator endpoint 생성
2. 슬롯 endpoint를 bridge child로 생성하고 parent 연결
3. Apple Home에서 child accessory 분리 노출 여부 확인

Pass 기준:
- 타일이 개별 accessory로 분리 노출
- 슬롯별 식별/방 배치가 기존 대비 개선

### 단계 B: 안정화
1. assign/rename/unassign 시 NodeLabel 동기 일관성 검증
2. 재부팅/복원 경로에서 NodeLabel 유실 및 crash 여부 검증
3. `/api/slots`, `/api/devices`, `/api/signals` 회귀 테스트

Pass 기준:
- 기능 회귀 0건
- 재부팅 반복에서 NodeLabel 유실 0건

### 단계 C: 병합 판단
1. PoC 결과를 현 구조와 비교(UX/안정성/복잡도)
2. 유지/병합/보류 결정 및 문서 확정
