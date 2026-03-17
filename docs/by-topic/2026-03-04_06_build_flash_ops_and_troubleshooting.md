# 5) 빌드/플래시/운영 및 트러블슈팅

## 빌드 상태
- `idf.py --build-dir build2 build` 성공
- 산출물: `build2/light.bin`

## 운영 명령 정리
- 권장(운영 스크립트)
```bash
cd ~/IdeaProjects/esp-matter-hub
./run_esp32-s3 /dev/cu.usbserial-0001
```
- 웹 접속: `hub_api_test` 또는 브라우저 직접 접속

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

- 증상: Apple Home 페어링 시 "Pairing Failed" (AddNOC 단계)
  - 로그: `mbedTLS error: BIGNUM - Memory allocation failed`
  - 원인: 내부 DRAM 힙 부족. CHIP/mbedTLS가 내부 DRAM에서만 할당하고 있었음
  - 대응: `CONFIG_CHIP_MEM_ALLOC_MODE_EXTERNAL=y`를 sdkconfig.defaults에 추가하여 PSRAM으로 전환
  - 참고: `CONFIG_ESP_MATTER_MEM_ALLOC_MODE_EXTERNAL`과는 별개 설정임

- 증상: chip-tool 속성 읽기/페어링 타임아웃 (IP 변경 후)
  - 원인: 디바이스 리부트 시 DHCP에서 다른 IP를 받을 수 있음. chip-tool KVS에 stale 세션 잔존
  - 대응: KVS 삭제 (`rm /var/folders/.../chip_tool_kvs*`) + commissioning window 재오픈 + 재페어링

- 증상: IRAM 링크 실패 (region `iram0_0_seg` overflowed)
  - 원인: IRAM 사용량 100% (16,383/16,384 bytes, 1바이트 잔여)
  - 대응: IRAM에 배치되는 함수 추가 자제. 필요시 `IRAM_ATTR` 제거 또는 flash 실행으로 전환

## 오늘 결론
- 실사용 운영 절차는
  1) Apple Home 페어링
  2) 보드 IP 또는 mDNS FQDN 확인
  3) IP/mDNS 기반 웹/API 작업
  으로 정리됨.
