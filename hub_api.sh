#!/bin/bash

set -euo pipefail

# ── IrManagement Matter Custom Cluster CLI ──────────────────────────────────
# chip-tool 기반 CLI. 기존 HTTP REST API 대체.
# Cluster ID: 0x1337FC01 (VendorPrefix 0x1337 | Cluster 0xFC01)
# ────────────────────────────────────────────────────────────────────────────

export PATH="$HOME/esp/esp-matter/connectedhomeip/connectedhomeip/.environment/gn_out:$PATH"

CLUSTER_ID="0x1337FC01"
DEFAULT_NODE_ID=1
DEFAULT_ENDPOINT_ID=10

NODE_ID="${NODE_ID:-$DEFAULT_NODE_ID}"
ENDPOINT_ID="${ENDPOINT_ID:-$DEFAULT_ENDPOINT_ID}"

# ── 인자 파싱 ──────────────────────────────────────────────────────────────
while [[ "${1:-}" == --* ]]; do
  case "$1" in
    --node)     NODE_ID="$2";     shift 2 ;;
    --endpoint) ENDPOINT_ID="$2"; shift 2 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

cmd="${1:-help}"
shift || true

# ── chip-tool 래퍼 ─────────────────────────────────────────────────────────
read_attr() {
  local attr_id="$1"
  chip-tool any read-by-id "$CLUSTER_ID" "$attr_id" "$NODE_ID" "$ENDPOINT_ID"
}

send_cmd() {
  local cmd_id="$1"
  local fields="${2:-\{\}}"
  chip-tool any command-by-id "$CLUSTER_ID" "$cmd_id" "$fields" "$NODE_ID" "$ENDPOINT_ID"
}

# ── 명령어 라우팅 ──────────────────────────────────────────────────────────
case "$cmd" in

  # ── 속성 읽기 ────────────────────────────────────────────────────────────
  learn-state)
    read_attr 0x0000
    ;;
  learned-payload)
    read_attr 0x0001
    ;;
  signals)
    read_attr 0x0002
    ;;
  slots)
    read_attr 0x0003
    ;;
  devices)
    read_attr 0x0004
    ;;

  # ── IR 학습 ──────────────────────────────────────────────────────────────
  learn)
    timeout_ms="${1:-15000}"
    send_cmd 0x00 "{\"0:U32\":${timeout_ms}}"
    ;;
  cancel-learn)
    send_cmd 0x01
    ;;
  save)
    name="${1:-}"
    device_type="${2:-light}"
    if [[ -z "$name" ]]; then
      echo "Usage: $0 save <name> [device_type]"
      exit 1
    fi
    send_cmd 0x02 "{\"0:STR\":\"${name}\", \"1:STR\":\"${device_type}\"}"
    ;;
  delete-signal)
    signal_id="${1:-}"
    if [[ -z "$signal_id" ]]; then
      echo "Usage: $0 delete-signal <signal_id>"
      exit 1
    fi
    send_cmd 0x03 "{\"0:U32\":${signal_id}}"
    ;;

  # ── 장치 관리 ────────────────────────────────────────────────────────────
  register)
    name="${1:-}"
    device_type="${2:-light}"
    if [[ -z "$name" ]]; then
      echo "Usage: $0 register <name> [device_type]"
      exit 1
    fi
    send_cmd 0x05 "{\"0:STR\":\"${name}\", \"1:STR\":\"${device_type}\"}"
    ;;
  rename)
    device_id="${1:-}"
    name="${2:-}"
    if [[ -z "$device_id" || -z "$name" ]]; then
      echo "Usage: $0 rename <device_id> <new_name>"
      exit 1
    fi
    send_cmd 0x06 "{\"0:U32\":${device_id}, \"1:STR\":\"${name}\"}"
    ;;
  bind)
    device_id="${1:-}"
    on_id="${2:-0}"
    off_id="${3:-0}"
    up_id="${4:-0}"
    down_id="${5:-0}"
    if [[ -z "$device_id" ]]; then
      echo "Usage: $0 bind <device_id> [on_signal] [off_signal] [up_signal] [down_signal]"
      exit 1
    fi
    send_cmd 0x04 "{\"0:U32\":${device_id}, \"1:U32\":${on_id}, \"2:U32\":${off_id}, \"3:U32\":${up_id}, \"4:U32\":${down_id}}"
    ;;

  # ── 슬롯 할당 ───────────────────────────────────────────────────────────
  assign)
    slot="${1:-}"
    device_id="${2:-}"
    if [[ -z "$slot" || -z "$device_id" ]]; then
      echo "Usage: $0 assign <slot:0-7> <device_id>"
      exit 1
    fi
    send_cmd 0x07 "{\"0:U8\":${slot}, \"1:U32\":${device_id}}"
    ;;
  unassign)
    slot="${1:-}"
    if [[ -z "$slot" ]]; then
      echo "Usage: $0 unassign <slot:0-7>"
      exit 1
    fi
    send_cmd 0x07 "{\"0:U8\":${slot}, \"1:U32\":0}"
    ;;

  # ── 커미셔닝 ────────────────────────────────────────────────────────────
  commission)
    timeout_sec="${1:-300}"
    send_cmd 0x08 "{\"0:U16\":${timeout_sec}}"
    ;;

  # ── 일괄 조회 ───────────────────────────────────────────────────────────
  smoke)
    echo "=== Learn State ==="
    read_attr 0x0000
    echo ""
    echo "=== Slots ==="
    read_attr 0x0003
    echo ""
    echo "=== Devices ==="
    read_attr 0x0004
    echo ""
    echo "=== Signals ==="
    read_attr 0x0002
    ;;

  # ── 도움말 ──────────────────────────────────────────────────────────────
  help|*)
    cat <<EOF
Usage: $0 [--node <id>] [--endpoint <id>] <command> [args]

  Defaults: node=${DEFAULT_NODE_ID}, endpoint=${DEFAULT_ENDPOINT_ID}
  Env vars: NODE_ID, ENDPOINT_ID

── 속성 읽기 ──────────────────────────────────────────────
  learn-state                 학습 상태 (0=IDLE 1=IN_PROGRESS 2=READY 3=FAILED)
  learned-payload             현재 학습 세션 상세 (JSON)
  signals                     저장된 IR 신호 목록 (JSON)
  slots                       브릿지 슬롯 할당 정보 (JSON)
  devices                     등록된 가상 장치 목록 (JSON)

── IR 학습 ────────────────────────────────────────────────
  learn [timeout_ms]          IR 학습 시작 (기본 15000ms)
  cancel-learn                학습 취소
  save <name> [type]          학습한 신호 저장 (기본 type: light)
  delete-signal <signal_id>   저장된 신호 삭제

── 장치 관리 ──────────────────────────────────────────────
  register <name> [type]      가상 장치 등록 (기본 type: light)
  rename <device_id> <name>   장치 이름 변경
  bind <dev> [on] [off] [up] [down]  신호 바인딩 (signal_id, 0=해제)

── 슬롯 할당 ──────────────────────────────────────────────
  assign <slot:0-7> <dev_id>  장치를 슬롯에 할당
  unassign <slot:0-7>         슬롯 할당 해제

── 기타 ───────────────────────────────────────────────────
  commission [timeout_sec]    커미셔닝 창 열기 (기본 300초)
  smoke                       전체 상태 일괄 조회

── 예시 ───────────────────────────────────────────────────
  $0 learn
  $0 save "TV Power" tv
  $0 register "Living Light"
  $0 bind 1 1 2 0 0
  $0 assign 0 1
  $0 --node 2 --endpoint 9 signals
  NODE_ID=2 $0 slots
EOF
    ;;
esac
