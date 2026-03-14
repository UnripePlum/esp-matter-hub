# 2026-03-12 작업 정리: Track A 회귀 스모크 자동화 보강

## 배경
- 구현 계획 문서(`2026-03-10_16`)의 Track A 잔여 항목은 실기 회귀 증적화
- 핵심 확인 대상: `DELETE /api/signals/{id}` + cascade unbind + `/api/slots` 계약(8 슬롯)

## 이번 구현
1. `hw_selftest.sh`에 delete/cascade 검증 단계 추가
   - 학습/커밋된 테스트 신호를 슬롯0에 바인딩 후 `delete_signal` 실행
   - 삭제 후 `/api/slots`에서 참조 필드(`on/off/level_up/level_down`)에 삭제된 signal_id가 남지 않는지 검사
   - 삭제 후 `/api/signals` 목록에서 해당 signal_id가 제거됐는지 검사

2. RX 테스트 로직 안정화
   - 최소 캡처 길이(`MIN_CAPTURE_LEN`) 조건 적용
   - RX 채널별 학습 재시도(`MAX_RX_ATTEMPTS`) 적용
   - 실패 사유를 `timeout` / `unexpected capture`로 분리 출력

3. 스크립트 안정성 보강
   - `set -u` 환경에서 빈 배열 처리 시 오류가 나지 않도록 cleanup 경로 보완
   - 학습 후 삭제를 반영한 복원 기준(`retained_learned_count`) 적용

## 기대 효과
- Track A 완료 조건 중 "삭제 API + 참조 자동 정리"를 단일 셀프테스트 플로우에서 반복 검증 가능
- 실기 테스트에서 결과를 PASS/FAIL로 바로 기록해 증적화 가능
