# 2026-03-12 작업 정리 (2): 온보드 LED 상태표시 및 IR TX 인디케이터

## 배경
- 외부 상태 LED 대신 개발보드 온보드 LED(WS2812) 사용 요청
- IR 송신 시 상태 표시 추가 요청

## 반영 내용

### 상태 LED 제어 백엔드 변경
- `main/status_led.cpp`, `main/status_led.h`
  - 외부 GPIO(15/16/17) 직접 제어 제거
  - `led_driver` 기반 온보드 WS2812 제어로 전환
  - 상태 머신 유지: booting / commissioning / ready / learning 진행/성공/실패

### IR TX 표시 추가
- `main/ir_engine.cpp`
  - 실제 송신 직전에 `status_led_notify_ir_tx()` 호출
- `main/status_led.cpp`
  - `IR_TX_PULSE` 상태 추가(짧은 흰색 펄스)

### 안정화/튜닝
- status_led task stack overflow 대응: task stack 상향
- HSV 범위 보정(드라이버 기대 범위에 맞춤)
- 기본 밝기 10으로 조정
- `led_driver_ws2812` INFO 로그 억제
- 외부 write 간섭 시 복구되도록 상태 프레임 주기 재적용

## 동작 의미(현재 기준)
- 파란 느린 점멸: booting
- 파란 빠른 점멸: commissioning
- 초록 고정: ready
- 노란 점멸: learning 진행
- 초록 점멸: learning 성공
- 빨간 점멸: learning 실패
- 흰색 짧은 펄스: IR 송신
