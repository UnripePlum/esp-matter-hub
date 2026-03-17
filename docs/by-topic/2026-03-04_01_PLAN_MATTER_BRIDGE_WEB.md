# [프로젝트 계획서] 범용 스마트 홈 통합을 위한 360도 Matter-IR 브릿지 허브 개발

## 1) 프로젝트 개요 및 비전
- 구형 가전(TV, 에어컨, 조명 등)을 Apple Home/Google Home 생태계에 편입하는 단일 통합 허브를 개발한다.
- 외부 클라우드/전용 앱 의존을 제거한 `Local-first` 아키텍처를 채택한다.
- 목표 지표:
  - 오프라인 동작 100%
  - 명령 반응 지연 체감 0.1초급
  - 단일 허브로 다기종 리모컨 신호 학습/재생/관리

## 2) 핵심 하드웨어 아키텍처
- 메인 프로세서: `ESP32-S3-DevKitC-1`
  - 듀얼코어 + RMT/DMA 활용
- 360도 IR 송신부(TX):
  - 고출력 광각 IR LED 5~6개 반구형 방사 배치
  - N-Channel MOSFET 스위칭 드라이버로 송신 전류 확보
  - 천장 바운스(난반사) 기반 사각지대 최소화
- IR 학습 수신부(RX):
  - 38kHz 대역 수신기(`VS1838B`) 사용
  - 외란광 노이즈 필터링 전제

## 3) 핵심 소프트웨어 스택 및 UI 전략
- 펌웨어 OS: `ESP-IDF + FreeRTOS`
  - Matter 통신 태스크와 IR 제어 태스크를 코어/우선순위 분리
- 프로토콜: `esp-matter` 기반 Matter Bridge
- UI 전략: 네이티브 앱 미사용, 내장 로컬 웹 대시보드
  - 저장소: SPIFFS 또는 LittleFS
  - 프론트엔드: HTML/JS/CSS 정적 리소스 내장
  - 접근: `http://<hub-ip>` 또는 `http://<hub-hostname>.local`
  - 현재 운영 기준(2026-03-05): IP 접근 우선(`http://<hub-ip>`)

## 4) 주요 시스템 동작 시나리오

### 4-1) 학습 시나리오 (신호 도둑질 + 로컬 저장)
1. 사용자가 웹 대시보드에서 `새 기기 학습` 클릭
2. 허브가 RMT RX 대기 모드 진입
3. 리모컨 버튼 입력 파형(raw timing array) 캡처
4. 신호 이름 지정(예: `거실 에어컨 켜기`)
5. NVS에 신호/메타데이터 영구 저장

### 4-2) 통합 시나리오 (Matter 가상 엔드포인트)
1. 학습된 신호를 슬롯 기반 가상 엔드포인트에 매핑
2. Matter 네트워크에서 해당 엔드포인트가 기기로 노출
3. Home 앱에서 일반 스마트기기처럼 제어 가능

### 4-3) 실행 시나리오 (Apple Home 제어)
1. 사용자가 Home 앱에서 명령 실행(예: `에어컨 켜기`)
2. 허브가 매핑된 `signal_id`를 NVS에서 조회
3. RMT TX로 38kHz IR 펄스를 송신
4. 대상 가전 동작 및 상태 로그 기록

## 5) 설계 원칙
- Home 앱 반영 안정성 확보를 위해 `고정 슬롯 엔드포인트` 전략 채택
  - 동적 엔드포인트 무제한 생성은 지양
- 표준 클러스터 중심 매핑:
  - OnOff, Level, Thermostat/Fan 범위 우선
- 비표준 기능은 로컬 웹 UI에서 직접 제공
- 장애 복구 우선:
  - 재부팅 후 NVS 기반 상태 자동 복원

## 6) 단계별 개발 마일스톤

### Phase 1: 하드웨어 프로토타이핑 + RMT 펄스 테스트
- 단일 IR LED 송신 회로 + VS1838B 수신 회로 구성
- RMT 캡처/재생 테스트
- NVS 저장 로직 1차 구현

완료 기준:
- 리모컨 1개 버튼 학습 후 재송신 성공
- 재부팅 후 학습 신호 유지 확인

### Phase 2: 로컬 웹 서버 및 프론트엔드 통합
- `esp_http_server` 기반 REST API 구축
  - `POST /api/learn/start`
  - `GET /api/learn/status`
  - `POST /api/learn/commit`
  - `GET /api/signals`
  - `POST /api/slots/{id}/bind`
- 웹 대시보드 정적 파일 SPIFFS/LittleFS 탑재

완료 기준:
- 스마트폰 브라우저로 로컬 대시보드 접속
- 학습/목록/매핑 API 왕복 동작

### Phase 3: Matter Bridge 연동 + 360도 하드웨어 설계
- esp-matter 브리지 적용
- 신호를 가상 스위치/온도조절기 엔드포인트로 매핑
- Apple Home 연동 검증
- MOSFET 기반 다이오드 어레이 PCB 회로도 설계

완료 기준:
- Home 앱에서 가상 기기 제어 시 실제 IR 동작
- 360도 송신 시 커버리지/응답성 기준 충족

### Phase 4: 안정화/양산 준비
- 타임아웃/재시도/중복 명령 억제
- NVS 스키마 버전/마이그레이션
- Factory reset/백업 복구 절차 확정
- 열/전류/EMI 안전성 점검

완료 기준:
- 24시간 반복 제어 안정성 통과
- 설치/운영 문서 확정

## 7) 시스템 데이터 모델
- `signal`
  - `signal_id`
  - `transport` (`ir`, `rs485`, `custom`)
  - `raw_payload`
  - `carrier_hz`
  - `repeat`
  - `name`, `room`, `device_type`
  - `created_at`
- `slot`
  - `slot_id`
  - `endpoint_id`
  - `cluster_profile` (`switch`, `light`, `thermostat`)
  - `bindings` (`on`, `off`, `level_up`, `level_down`, ...)

## 8) 코드 구조 계획 (`esp-matter-hub`)
- `main/app_main.cpp`
  - Bridge endpoint 생성 및 콜백 등록
- `main/app_driver.cpp`
  - `execute_action(action_id, value)` 라우터
- `main/ir_learn.c` (신규)
  - RMT RX/TX, 학습 상태 머신
- `main/storage.c` (신규)
  - NVS CRUD, 스키마 버전
- `main/web_server.c` (신규)
  - HTTP 라우팅 및 API 핸들러
- `main/web/` (신규)
  - 대시보드 정적 파일

## 9) 리스크와 대응
- Home 앱 동적 반영 한계
  - 대응: 슬롯 고정 전략
- IR 신호 품질 편차
  - 대응: 다회 학습 + 품질 점수화
- 고출력 IR 회로 발열/전류
  - 대응: MOSFET 여유 설계 + 듀티 제한 + 열 검증
- 메모리 압박
  - 대응: payload 상한, PSRAM 활용, 로그 레벨 제어

## 10) 즉시 실행 항목 (이번 스프린트)
1. 고정 슬롯 4개 endpoint 생성
2. `execute_action()` 라우터 추가
3. `/api/health`, `/api/signals`, `/api/learn/start` 구현
4. 웹 대시보드 최소 UI(학습 버튼, 신호 목록, 슬롯 매핑)
5. 단일 IR LED 기반 학습/재생 E2E 테스트

완료 조건:
- Home 앱에서 4개 가상 기기 확인
- 로컬 웹에서 신호 학습 후 바로 제어 성공
- 재부팅 후 매핑/신호 유지 확인

## 현재 완성된 기능 (2026-03-17 기준)
- Phase 1~4 전부 완료 및 운영 중
- Custom cluster 0x1337FC01 (endpoint 10) 구현
- Aggregator 기반 토폴로지 전환 완료
- Multi-fabric (Apple Home + chip-tool) 지원
- PSRAM 기반 메모리 할당으로 commissioning 안정화
