# 10) 테스트 시나리오 (TX 2브랜치 + RX 2채널)

## 테스트 전 공통 준비
- 보드 연결 후 `run_esp32-s3` 실행
- monitor 로그에서 `sta ip:` 확인
- `open-local-web` 실행하여 대시보드 접속
- 반자동 하드웨어 셀프테스트 실행(권장): `./hw_selftest.sh <hub-host>`
- 하드웨어 배선 확인
  - TX: GPIO4 -> PN2222 2개 베이스(각 330R), 각 베이스 100k 풀다운
  - TX 브랜치: 5V -> 100R -> LED -> LED -> PN2222 C
  - RX: GPIO5/GPIO6, 전원 필터(100nF + 100uF)

## 시나리오 1: 서버/기본 API 상태 확인
### 절차
1. `GET /api/health`
2. `GET /api/slots`
3. `GET /api/signals`

### 기대 결과
- `/api/health`: `{"status":"ok"...}`
- `/api/slots`: 고정 8개 슬롯(0~7) 정보 반환
- `/api/signals`: 배열 반환(처음에는 비어 있을 수 있음)

## 시나리오 2: RX 학습 성공 (RX1 또는 RX2)
### 절차
1. 대시보드에서 Learn Timeout 설정 (예: 15000ms)
2. `Start Learning` 클릭
3. 리모컨 버튼 1회 누름 (수신기 근거리)
4. `Refresh Status` 클릭

### 기대 결과
- 상태가 `in_progress -> ready` 전환
- `rx_source`가 `1` 또는 `2`
- `captured_len > 0`
- `quality_score > 0`

## 시나리오 3: 학습 커밋 및 저장 확인
### 절차
1. Signal Name/Device Type 입력
2. `Commit Learned Signal` 클릭
3. `GET /api/signals` 또는 Signals 테이블 갱신

### 기대 결과
- `{"status":"ok","signal_id":N}` 반환
- Signals 테이블에 신규 항목 추가
- 신규 항목의 `payload_len > 0`

## 시나리오 4: 슬롯 바인딩 및 TX 동작 확인
### 절차
1. `Bind Signal to Slot`에서 slot/onSignalId/offSignalId/levelUpSignalId/levelDownSignalId 선택
2. Bind 실행
3. Matter/Home 앱에서 해당 슬롯 On/Off 또는 Level 제어

### 기대 결과
- bind API 성공 응답
- monitor 로그에 `TX signal_id=... len=...` 출력
- TX 브랜치 A/B가 동시 발광
- 실제 가전 반응 확인

## 시나리오 5: RX 듀얼 채널 검증
### 절차
1. RX1만 리모컨이 잘 보이게 배치 후 학습 3회
2. RX2만 리모컨이 잘 보이게 배치 후 학습 3회
3. `rx_source` 분포 기록

### 기대 결과
- 배치에 따라 `rx_source`가 1/2로 바뀜
- 특정 채널이 계속 실패하면 해당 채널 배선/전원/필터 점검 포인트 확보

## 시나리오 6: 예외/실패 처리
### 케이스 A: 타임아웃
1. `Start Learning` 후 리모컨 입력 안 함
2. timeout 경과 후 상태 확인

기대 결과:
- 상태 `failed`
- commit 시 409 충돌(`no pending learned signal`)

### 케이스 B: 미바인딩 제어
1. signal_id 미할당 슬롯 제어

기대 결과:
- monitor에 `No signal mapped` 경고 로그

## 시나리오 7: 재부팅 복원
### 절차
1. 학습/바인딩 완료 상태에서 보드 재부팅
2. `GET /api/signals`, `GET /api/slots` 확인
3. Matter/Home 앱 제어 재실행

### 기대 결과
- 저장된 signal/slot 유지
- 제어 시 정상 TX 재동작

## 시나리오 8: 열/전원 안정성(10분)
### 절차
1. 10분 동안 주기적으로 On/Off 제어 반복
2. PN2222/저항 온도 및 오동작 여부 확인

### 기대 결과
- 오동작/재부팅/신호 누락 없음
- 소자가 과열 임계로 올라가지 않음

## 실패 시 우선 점검 순서
1. IP 변경 여부 (`sta ip`) 및 웹 접속 주소 재확인
2. RX 상태 필드 (`rx_source`, `captured_len`) 확인
3. TX 로그 (`TX signal_id`, `len`, `repeat`) 확인
4. PN2222 핀 방향(E/B/C) 및 LED 극성 재확인
5. 5V/3.3V/GND 공통 및 RX 필터 커패시터 재확인

## 자동화 도구 메모 (2026-03-11)
- 스크립트: `hw_selftest.sh`
- 목적: boot/network/API/IR RX/TX/button/NVS 복원 점검을 단일 플로우로 수행
- 특징:
  - API 검증은 자동 수행 (`/api/health`, `/api/slots`, `/api/learn/*`, `/api/export/nvs`, `/api/signals`)
  - 물리 확인 항목(LED 패턴/버튼 누름/IR 발광/가전 반응/전원 재인가)은 단계별 수동 확인 프롬프트 제공
  - 종료 시 항목별 PASS/FAIL 요약 출력
