#!/bin/bash

export PATH="$HOME/esp/esp-matter/connectedhomeip/connectedhomeip/.environment/gn_out:$PATH"

CLUSTER_ID="0x13370001"
NODE_ID="${1:-1}"
ENDPOINT_ID="${2:-10}"

read_attr() {
  chip-tool any read-by-id "$CLUSTER_ID" "$1" "$NODE_ID" "$ENDPOINT_ID"
}

send_cmd() {
  local cmd_id="$1"
  local fields="${2:-\{\}}"
  chip-tool any command-by-id "$CLUSTER_ID" "$cmd_id" "$fields" "$NODE_ID" "$ENDPOINT_ID"
}

learn_state()      { read_attr 0x0000; }
learned_payload()  { read_attr 0x0001; }
signals()          { read_attr 0x0002; }
slots()            { read_attr 0x0003; }
devices()          { read_attr 0x0004; }

learn() {
  local timeout_ms="${1:-15000}"
  send_cmd 0x00 "{\"0:U32\":${timeout_ms}}"
}

cancel_learn() { send_cmd 0x01; }

save() {
  local name="${1:-}"
  local device_type="${2:-light}"
  if [[ -z "$name" ]]; then
    echo "usage: save <name> [device_type]"
    return 1
  fi
  send_cmd 0x02 "{\"0:STR\":\"${name}\", \"1:STR\":\"${device_type}\"}"
}

delete_signal() {
  if [[ -z "${1:-}" ]]; then
    echo "usage: delete_signal <signal_id>"
    return 1
  fi
  send_cmd 0x03 "{\"0:U32\":$1}"
}

register() {
  local name="${1:-}"
  local device_type="${2:-light}"
  if [[ -z "$name" ]]; then
    echo "usage: register <name> [device_type]"
    return 1
  fi
  send_cmd 0x05 "{\"0:STR\":\"${name}\", \"1:STR\":\"${device_type}\"}"
}

rename() {
  if [[ -z "${1:-}" || -z "${2:-}" ]]; then
    echo "usage: rename <device_id> <new_name>"
    return 1
  fi
  local device_id="$1"; shift
  local name="$*"
  send_cmd 0x06 "{\"0:U32\":${device_id}, \"1:STR\":\"${name}\"}"
}

bind() {
  if [[ -z "${1:-}" ]]; then
    echo "usage: bind <device_id> [on_signal] [off_signal] [up_signal] [down_signal]"
    return 1
  fi
  local dev="$1" on="${2:-0}" off="${3:-0}" up="${4:-0}" down="${5:-0}"
  send_cmd 0x04 "{\"0:U32\":${dev}, \"1:U32\":${on}, \"2:U32\":${off}, \"3:U32\":${up}, \"4:U32\":${down}}"
}

assign() {
  if [[ -z "${1:-}" || -z "${2:-}" ]]; then
    echo "usage: assign <slot:0-7> <device_id>"
    return 1
  fi
  send_cmd 0x07 "{\"0:U8\":$1, \"1:U32\":$2}"
}

unassign() {
  if [[ -z "${1:-}" ]]; then
    echo "usage: unassign <slot:0-7>"
    return 1
  fi
  send_cmd 0x07 "{\"0:U8\":$1, \"1:U32\":0}"
}

commission() {
  local timeout="${1:-300}"
  send_cmd 0x08 "{\"0:U16\":${timeout}}"
}

pair() {
  local pin="${1:-20202021}"
  echo "Pairing node $NODE_ID with setup-pin-code $pin ..."
  chip-tool pairing onnetwork "$NODE_ID" "$pin"
}

unpair() {
  echo "Unpairing node $NODE_ID ..."
  chip-tool pairing unpair "$NODE_ID"
}

smoke() {
  echo "=== Learn State ===" && learn_state
  echo "" && echo "=== Slots ===" && slots
  echo "" && echo "=== Devices ===" && devices
  echo "" && echo "=== Signals ===" && signals
}

api_help() {
  cat <<'HELP'
── 속성 읽기 ──────────────────────────────────────────────
  learn_state                 학습 상태 (0=IDLE 1=IN_PROGRESS 2=READY 3=FAILED)
  learned_payload             현재 학습 세션 상세 (JSON)
  signals                     저장된 IR 신호 목록
  slots                       브릿지 슬롯 할당 정보
  devices                     등록된 가상 장치 목록

── IR 학습 ────────────────────────────────────────────────
  learn [timeout_ms]          학습 시작 (기본 15000ms)
  cancel_learn                학습 취소
  save <name> [type]          학습한 신호 저장
  delete_signal <signal_id>   저장된 신호 삭제

── 장치 관리 ──────────────────────────────────────────────
  register <name> [type]      가상 장치 등록
  rename <device_id> <name>   장치 이름 변경
  bind <dev> [on] [off] [up] [down]  신호 바인딩

── 슬롯 할당 ──────────────────────────────────────────────
  assign <slot:0-7> <dev_id>  장치를 슬롯에 할당
  unassign <slot:0-7>         슬롯 할당 해제

── 기타 ───────────────────────────────────────────────────
  pair [setup_pin]              디바이스 커미셔닝 (기본 20202021)
  unpair                        디바이스 커미셔닝 해제
  commission [timeout_sec]    커미셔닝 창 열기
  smoke                       전체 상태 일괄 조회

── REPL ───────────────────────────────────────────────────
  /help                       이 도움말
  /node                       현재 node/endpoint 확인
  /exit                       종료
HELP
}
