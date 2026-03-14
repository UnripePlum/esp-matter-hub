# 9) 펌웨어 체크리스트 및 코드 리스크 점검 (TX 2브랜치 + RX 2채널)

## 대상 구성
- TX: GPIO4 -> PN2222 2개 베이스 분기 -> IR LED 2직렬 브랜치 2개 동시 송신
- RX: VS1838B 2개 (GPIO5/GPIO6)

## 체크리스트

### A. 빌드/기동
- [ ] `idf.py --build-dir build2 build` 성공
- [ ] 부팅 시 `IR RMT initialized TX=4 RX={5,6}` 로그 확인
- [ ] 웹 서버 기동 로그 확인

### B. TX 송신 경로
- [ ] 슬롯 제어 시 `ir_engine_send_signal()` 진입 로그 확인
- [ ] payload length, repeat 값 로그 확인
- [ ] 브랜치 A/B 동시 발광 확인 (스마트폰 카메라 또는 IR 카드)

### C. RX 학습 경로
- [ ] `/api/learn/start` 후 `in_progress` 상태 확인
- [ ] 리모컨 입력 시 `ready` 전환 확인
- [ ] `rx_source`, `captured_len`, `quality_score` 값 확인
- [ ] `/api/learn/commit` 후 signal_id 생성 확인

### D. 저장/복원
- [ ] `/api/signals`에서 signal metadata + `payload_len` 확인
- [ ] 재부팅 후 signal 목록 유지 확인

### E. E2E
- [ ] signal bind -> Matter/Home 제어 -> 실제 IR 동작
- [ ] 미매핑 signal_id 동작 시 적절한 경고 로그 출력

## 코드 리스크 점검 결과

### 1) 수신 파형 레벨 해석 리스크
- 기존: RX item의 level 정보 없이 duration만 저장해 재송신 시 위상 불일치 가능
- 조치: RX 캡처 시 active-low mark(일반 VS1838B) 기준으로 mark/space 정규화 저장

### 2) 과도 repeat 리스크
- 기존: repeat 상한 없음
- 조치: repeat 최대 5회로 clamp, 반복 사이 gap(12ms) 추가

### 3) 남은 리스크
- RMT legacy driver 사용 중 (동작 가능하나 deprecate 경고 존재)
- 긴 에어컨 프레임은 `payload_ticks[128]` 상한에 걸릴 수 있음
  - 차기: payload blob 분리 저장으로 확장 필요

## 운영 권장
- PN2222 베이스 저항은 기본 330R 사용 가능하나, 발열/포화 상태를 보고 470R~680R 튜닝 검토
- TX 장시간 반복 테스트 시 트랜지스터 및 저항 온도 체크 필수
