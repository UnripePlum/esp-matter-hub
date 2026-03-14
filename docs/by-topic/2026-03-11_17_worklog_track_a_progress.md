# 2026-03-11 작업 정리: Track A 진행 사항

## 목적
- 운영 라인(`main`)에서 API/문서/테스트 시나리오 정합성을 우선 확보
- `DELETE /api/signals/{id}` 및 cascade unbind 동작을 사용자 문서와 운영 도구에 반영

## 확인 결과
- 펌웨어 구현은 이미 반영되어 있음
  - `main/web_server.cpp`: `DELETE /api/signals/{id}` 핸들러 존재
  - `main/bridge_action.cpp`: `bridge_action_unbind_signal_references()`로 참조 필드 정리
- 대시보드 구현도 이미 반영되어 있음
  - Signals 테이블의 `Delete` 버튼 -> `DELETE /api/signals/{id}` 호출

## 이번 반영 내역
1. 운영 테스트 도구 확장
   - 파일: `.hub_api_test_env.sh`
   - 추가: `api_delete()` 및 `delete_signal <signal_id>` 커맨드
   - 도움말(`api_help`)에 `delete_signal` 항목 추가

2. API/대시보드 문서 정합화
   - 파일: `docs/by-topic/2026-03-04_04_web_dashboard_and_rest_api.md`
   - 추가/수정:
     - `DELETE /api/signals/{id}` API 명시
     - Signals Delete UI 절차 명문화(확인 다이얼로그/삭제 후 자동 갱신)
     - cascade unbind 대상 필드(`on/off/level_up/level_down`) 및 삭제 후 `0` 반영 규칙 명시

3. 테스트 시나리오 정합화
   - 파일: `docs/by-topic/2026-03-09_15_run_esp32s3_and_nodelabel_test_plan.md`
   - 추가: `Signal Delete + cascade unbind` 검증 단계 및 pass 기준
   - quick curl 예시에 `DELETE /api/signals/1` 추가

4. 통합 테스트 문서 보강
   - 파일: `docs/by-topic/2026-03-09_14_integrated_test_scenarios.md`
   - 추가: `시나리오 10: Signal Delete + cascade unbind`
   - 갱신: 재부팅/장시간 시나리오 번호 및 최종 판정 기준(필수 시나리오 범위)

## 검증
- `bash -n .hub_api_test_env.sh` 문법 검증 통과
- 하드웨어 실측/API 실기 호출은 다음 단계에서 수행 예정

## 현재 판단
- Track A의 핵심 항목 중 "삭제 API + cascade unbind 동작 문서 반영"은 완료
- 남은 핵심은 실기 기준 회귀 체크(8슬롯 고정, 삭제 후 참조값 0, 문서-동작 불일치 0) 확인

## 다음 액션
1. `hub_api_test`로 `delete_signal` 포함 회귀 스모크 수행
2. `/api/slots` 8개 고정 및 참조 자동 정리 결과를 로그/스크린샷으로 증적화
3. Track A 완료 판정 문서 업데이트
