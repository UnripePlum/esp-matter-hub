# 2) 브리지 엔드포인트 및 슬롯 설계

## 설계 방향
- 동적 엔드포인트 무한 생성 대신 고정 슬롯 전략 채택
- Apple Home 반영 안정성을 우선

## 오늘 반영된 슬롯 구성
- Slot 0: 전등 (dimmable light)
- Slot 1: TV 리모컨 (on/off plug)
- Slot 2: 에어컨 리모컨 (dimmable light)
- Slot 3: 로봇청소기 리모컨 (on/off plug)

## 구현 포인트
- `main/app_main.cpp`
  - 슬롯별 엔드포인트 생성 로직 반영
  - 슬롯 endpoint id 배열 초기화
- `main/bridge_action.cpp`
  - 슬롯 바인딩 상태 로드/저장(NVS)
  - `bridge_action_execute()`에서 slot + cluster + attribute 라우팅

## 동작 확인
- Home 앱 조작 시 슬롯 로그가 출력되어 명령 라우팅 경로 확인 완료
  - OnOff/Level 명령이 slot action으로 반영됨

