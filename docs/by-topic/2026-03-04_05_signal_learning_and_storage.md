# 4) 신호 학습/저장 계층

## 목표
- 학습 신호를 임시 상태가 아니라 NVS에 영구 저장
- 슬롯 바인딩 대상(`signal_id`)으로 재사용 가능하게 구성

## 오늘 구현 내용
- `main/ir_engine.h`
  - `ir_signal_record_t` 추가
  - `ir_engine_commit_learning()` 선언 추가
  - `ir_engine_get_signals()` 선언 추가

- `main/ir_engine.cpp`
  - 신호 테이블 구조 도입
    - `version`, `next_signal_id`, `count`, `records[]`
  - NVS 저장/로드 구현
    - namespace: `ir_signals`
    - key: `table`
  - 학습 흐름 정리
    1. `learn/start` -> pending 학습 생성
    2. `learn/commit` -> `signal_id` 확정 + NVS 저장
  - 조회 함수 구현
    - 메모리 테이블을 `/api/signals`로 노출

## 현재 구현 상태 (2026-03-05 기준)
- IR raw payload 캡처/저장/재송신 경로 연결됨
  - RX: 듀얼 채널(GPIO5/GPIO6)에서 캡처 후 `rx_source`, `captured_len`, `quality_score` 노출
  - Commit: pending 학습을 `signal_id`로 확정하고 payload를 NVS에 저장
  - TX: 슬롯 제어 시 `signal_id -> payload` 조회 후 RMT 송신
- `/api/signals` 항목에 `payload_len` 확인 가능

## 현재 한계(명시)
- 긴 에어컨 프레임은 `payload_ticks[128]` 상한에 걸릴 수 있음
- RMT legacy driver 기반이라 deprecate 경고가 존재함

## 다음 구현 포인트
- 메타데이터 테이블 + payload blob 분리 저장 구조로 확장
- 장문 프레임(AC 리모컨) 대응을 위한 payload 상한 확장
