# 5) 빌드/플래시/운영 및 트러블슈팅

## 빌드 상태
- `idf.py --build-dir build2 build` 성공
- 산출물: `build2/light.bin`

## 운영 명령 정리
- 권장(운영 스크립트)
```bash
cd ~/IdeaProjects/esp-matter-hub
./run_esp32-s3 /dev/cu.usbserial-0001
./open-local-web
```

- 환경 로드 + 플래시/모니터
```bash
source ~/esp/esp-idf/export.sh
source ~/esp/esp-matter/export.sh
export ESP_MATTER_DEVICE_PATH=$ESP_MATTER_PATH/device_hal/device/esp32s3_devkit_c
cd ~/IdeaProjects/esp-matter-hub
idf.py --build-dir build2 -p /dev/cu.usbserial-0001 flash monitor
```

- monitor 종료
  - `Ctrl+]`

## 주요 장애와 대응
- 증상: `idf.py` 명령 없음
  - 원인: IDF export 미적용
  - 대응: `source ~/esp/esp-idf/export.sh`

- 증상: 포트 점유로 플래시 실패
  - 대응: 기존 monitor 프로세스 종료 후 재시도

- 증상: `chip-tool` BLE 0x407 / PASE timeout
  - 대응: Apple Home 기반 페어링을 메인 경로로 사용

- 증상: factory reset 후 IP/API 미응답
  - 원인: Wi-Fi 자격증명 제거
  - 대응: 재커미셔닝 후 `sta ip` 확인

- 증상: `.local` 접속이 불안정하거나 실패
  - 원인: AP client isolation, 멀티캐스트 차단, 로컬 DNS 캐시 상태
  - 대응: `GET /api/health`에서 `fqdn`/`mdns` 확인 후 IP 접속으로 우회

- 로그: `Timeout waiting for mDNS resolution`
  - 해석: Matter 운영 탐색 재시도 로그이며, 웹 mDNS(`_http._tcp`)와는 별개일 수 있음

## 오늘 결론
- 실사용 운영 절차는
  1) Apple Home 페어링
  2) 보드 IP 또는 mDNS FQDN 확인
  3) IP/mDNS 기반 웹/API 작업
  으로 정리됨.
