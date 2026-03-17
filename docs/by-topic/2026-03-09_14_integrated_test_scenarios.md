# 2026-03-09 통합 테스트 시나리오 기획안

## 목적
- 기능 정확성, 운영 안정성, 데이터 무결성을 단일 플로우에서 검증
- 릴리스 전 최소 통합 기준(pass/fail)을 명확히 정의

## 사전 조건
- 최신 펌웨어 플래시 완료
- 보드 전원/배선 정상 (TX/RX)
- 같은 LAN에서 테스트 PC 접속 가능
- `run_esp32-s3`로 monitor 확인 가능

## 시나리오 1: 부팅/연결 기본 검증
### 절차
1. 보드 부팅 후 `sta ip` 로그 확인
2. `mDNS ready` 로그 확인
3. `GET /api/health` 호출

### 기대 결과
- IP/mDNS 모두 유효
- health 응답 `status=ok`

## 시나리오 2: 웹/API 기본 동작
### 절차
1. `GET /api/slots`
2. `GET /api/signals`
3. `GET /api/export/nvs?scope=bindings`

### 기대 결과
- 200 응답
- JSON 형식 정상

## 시나리오 3: IR 학습/커밋
### 절차
1. `POST /api/learn/start`
2. 리모컨 입력 후 `GET /api/learn/status`
3. `POST /api/learn/commit`

### 기대 결과
- `in_progress -> ready`
- `signal_id` 발급
- `payload_len > 0`

## 시나리오 4: 슬롯 바인딩(최신 필드)
### 절차
1. `POST /api/slots/{id}/bind`로 아래 4필드 저장
   - `on_signal_id`
   - `off_signal_id`
   - `level_up_signal_id`
   - `level_down_signal_id`
2. `GET /api/slots` 재확인

### 기대 결과
- 저장값과 조회값 일치

## 시나리오 5: On/Off 제어 검증
### 절차
1. Matter 제어로 On
2. Matter 제어로 Off

### 기대 결과
- On 시 `on_signal_id` 송신
- Off 시 `off_signal_id` 송신

## 시나리오 6: Level 방향 제어 검증
### 절차
1. 밝기 증가
2. 밝기 감소
3. 밝기 0으로 설정
4. 0에서 양수로 재설정

### 기대 결과
- 증가: `level_up_signal_id`
- 감소: `level_down_signal_id`
- 0: `off_signal_id`
- 0->양수: `on_signal_id`

## 시나리오 7: LED 상태 검증
### 절차
1. 부팅/ready 상태 확인
2. 커미셔닝 오픈 시 상태 확인
3. 학습 in_progress/ready/failed 상태 확인

### 기대 결과
- LED 패턴과 `/api/health.led_state` 일치

## 시나리오 8: NVS Export 성공 경로
### 절차
1. `GET /api/export/nvs?scope=signals`
2. `payload_len`과 `payload_ticks.length` 비교
3. `scope=all` 재검증

### 기대 결과
- 200 응답
- 모든 신호에서 길이 일치

## 시나리오 9: NVS Export 엄격 실패 경로
### 절차
1. payload 무결성 실패 조건 유도(테스트 환경)
2. `GET /api/export/nvs?scope=signals`

### 기대 결과
- HTTP 500
- `EXPORT_PAYLOAD_INTEGRITY_FAILED` 반환
- 부분 성공 없음

## 시나리오 10: Signal Delete + cascade unbind
### 절차
1. 바인딩에 사용 중인 signal_id 1개 선택
2. `DELETE /api/signals/{id}` 호출
3. `/api/signals`에서 삭제 반영 확인
4. `/api/slots`에서 참조 필드 확인

### 기대 결과
- 삭제 대상 signal_id 미존재
- 참조 필드(`on/off/level_up/level_down`)가 자동 `0`으로 정리

## 시나리오 11: 재부팅 복원
### 절차
1. 신호/바인딩 구성 후 재부팅
2. `/api/signals`, `/api/slots`, 제어 동작 재검증

### 기대 결과
- 데이터 복원 정상
- 제어 정상 재동작

## 시나리오 12: 장시간 안정성(요약)
### 절차
1. On/Off/Level 반복 제어
2. 주기적 health/export 호출

### 기대 결과
- 재부팅/멈춤/누락 없음

## 운영 안정성 상세 시나리오

### A. 네트워크 복원력
절차:
1. 허브 정상 연결 상태에서 AP 재시작
2. 허브 재접속까지 시간 측정
3. `/api/health` 및 실제 제어 재확인

기대 결과:
- 1~2분 내 자동 복구
- 복구 후 제어 정상

### B. mDNS 신뢰성
절차:
1. 재부팅 20회 반복
2. 매 회 `dns-sd -B _http._tcp local`, `dns-sd -L "ESP Matter Hub UI" _http._tcp local` 확인
3. `.local` 접속 성공률 집계

기대 결과:
- alias host 기반 접근이 일관됨
- 실패 시 IP fallback으로 즉시 운영 가능

### C. 제어 연속 부하
절차:
1. On/Off 1초 간격 300회
2. Level Up/Down 혼합 300회

기대 결과:
- 송신 누락/오동작/재부팅 없음
- 오류 로그 급증 없음

### D. 학습/제어 동시성
절차:
1. 학습 시작 상태에서 다른 슬롯 제어 실행
2. 학습 실패/timeout 이후 즉시 제어 재실행

기대 결과:
- 학습 경로가 전체 제어 경로를 막지 않음

### E. 저장소 안정성 (NVS)
절차:
1. 학습+커밋 100회 반복
2. 재부팅 후 `/api/signals`, `/api/slots` 비교

기대 결과:
- 신호/바인딩 복원 정상
- 데이터 손실 없음

### F. Export API 내구성
절차:
1. `GET /api/export/nvs?scope=signals|all|bindings` 반복 호출
2. 응답 내 `payload_len == payload_ticks.length` 전수 검증

기대 결과:
- 정상 케이스에서 5xx 없음
- 무결성 실패 유도 시 `EXPORT_PAYLOAD_INTEGRITY_FAILED`로 전체 실패

### G. 장시간 Soak Test (4~24h)
절차:
1. 주기 제어 + 주기 health check + 주기 export 병행
2. 메모리/재부팅/에러 로그 관찰

기대 결과:
- 재부팅/멈춤 없음
- heap 추세 안정

### H. 전원 복구
절차:
1. 전원 차단/인가 20회 반복
2. 매 회 연결/제어/export 확인

기대 결과:
- 자동 복구
- 저장 데이터 유지

### I. 오류 주입
절차:
1. 존재하지 않는 signal_id 바인딩/제어
2. 비정상 JSON 요청/과대 body 요청

기대 결과:
- 4xx/5xx가 명확하게 반환
- 시스템 전체는 정상 유지

## 운영 KPI
- 제어 성공률
- API 응답시간(avg/p95)
- `.local` 접근 성공률
- export 성공률
- 재부팅 횟수

## 판정 기준
- 필수 시나리오(1~11) 전부 pass 시 통합 테스트 통과
- 시나리오 12는 soak 결과 별도 첨부

## 참고 문서
- export API 스키마: `docs/by-topic/2026-03-09_13_api_export_nvs_json_schema.md`

## 시나리오 13: Custom Cluster 검증
### 절차
1. chip-tool로 속성 5개 읽기 (smoke test)
2. StartLearning → LearnState=1 확인
3. RegisterDevice + RenameDevice 커맨드
4. OpenCommissioning 커맨드
### 기대 결과
- 모든 속성 정상 반환
- 커맨드 성공 응답

## 시나리오 14: Multi-Fabric 검증
### 절차
1. Apple Home 페어링 확인
2. HTTP API로 commissioning window open
3. chip-tool pairing onnetwork --commissioner-name beta
### 기대 결과
- 두 fabric 모두 제어 가능

## 참고 커맨드
```bash
curl -s "http://<hub-host>/api/health"
curl -s "http://<hub-host>/api/export/nvs?scope=signals"
dns-sd -B _http._tcp local
dns-sd -L "ESP Matter Hub UI" _http._tcp local
```
