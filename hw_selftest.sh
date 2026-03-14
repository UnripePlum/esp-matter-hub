#!/bin/bash

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
ENV_FILE="$PROJECT_DIR/.hub_api_test_env.sh"
HUB_HOST="${1:-esp-matter-hub-659824.local}"

if [[ ! -f "$ENV_FILE" ]]; then
  echo "Missing env file: $ENV_FILE"
  exit 1
fi

export HUB_HOST
source "$ENV_FILE"

declare -a TEST_NAMES=()
declare -a TEST_STATUS=()
declare -a TEST_DETAIL=()

MIN_CAPTURE_LEN=6
MAX_RX_ATTEMPTS=3

record_result() {
  local name="$1"
  local status="$2"
  local detail="$3"
  TEST_NAMES+=("$name")
  TEST_STATUS+=("$status")
  TEST_DETAIL+=("$detail")
  if [[ "$status" == "PASS" ]]; then
    printf '[PASS] %s - %s\n' "$name" "$detail"
  else
    printf '[FAIL] %s - %s\n' "$name" "$detail"
  fi
}

json_eval() {
  local expr="$1"
  local raw
  raw="$(cat)"
  python3 - "$expr" "$raw" <<'PY'
import json
import sys

expr = sys.argv[1]
raw = sys.argv[2]
obj = json.loads(raw)
value = eval(expr, {"__builtins__": {}}, {"obj": obj, "len": len, "isinstance": isinstance})
if isinstance(value, bool):
    print("true" if value else "false")
elif value is None:
    print("null")
else:
    print(value)
PY
}

prompt_enter() {
  local message="$1"
  printf '\n%s\n' "$message"
  if [[ ! -t 0 ]]; then
    echo "[AUTO] Non-interactive shell: continue"
    return 0
  fi
  read -r -p "Press Enter to continue... " _
}

poll_learning_ready() {
  local timeout_s="$1"
  local expect_source="$2"
  local min_len="$3"
  local elapsed=0
  local max_wait=$((timeout_s + 4))
  local status_json=""
  while (( elapsed <= max_wait )); do
    if ! status_json="$(learn_status)"; then
      sleep 1
      elapsed=$((elapsed + 1))
      continue
    fi
    local state
    state="$(printf '%s' "$status_json" | json_eval 'obj.get("state")')"
    if [[ "$state" == "ready" ]]; then
      local rx_source captured_len quality
      rx_source="$(printf '%s' "$status_json" | json_eval 'obj.get("rx_source", 0)')"
      captured_len="$(printf '%s' "$status_json" | json_eval 'obj.get("captured_len", 0)')"
      quality="$(printf '%s' "$status_json" | json_eval 'obj.get("quality_score", 0)')"
      if [[ "$rx_source" == "$expect_source" && "$captured_len" -ge "$min_len" && "$quality" -ge "$min_len" ]]; then
        printf '%s\n' "$status_json"
        return 0
      fi
      printf '%s\n' "$status_json"
      return 2
    fi
    if [[ "$state" == "failed" ]]; then
      break
    fi
    sleep 1
    elapsed=$((elapsed + 1))
  done
  if [[ -n "$status_json" ]]; then
    printf '%s\n' "$status_json"
  fi
  return 1
}

cleanup_temp_signals() {
  local ids=("$@")
  local signal_id
  for signal_id in ${ids[@]-}; do
    [[ -n "$signal_id" ]] || continue
    delete_signal "$signal_id" >/dev/null 2>&1 || true
  done
}

slots_have_no_signal_refs() {
  local signal_id="$1"
  python3 - "$signal_id" <<'PY'
import json
import sys

signal_id = int(sys.argv[1])
raw = sys.stdin.read()
obj = json.loads(raw)

for slot in obj.get("slots", []):
    if int(slot.get("on_signal_id", 0)) == signal_id:
        print("false")
        raise SystemExit(0)
    if int(slot.get("off_signal_id", 0)) == signal_id:
        print("false")
        raise SystemExit(0)
    if int(slot.get("level_up_signal_id", 0)) == signal_id:
        print("false")
        raise SystemExit(0)
    if int(slot.get("level_down_signal_id", 0)) == signal_id:
        print("false")
        raise SystemExit(0)

print("true")
PY
}

signals_missing_id() {
  local signal_id="$1"
  python3 - "$signal_id" <<'PY'
import json
import sys

signal_id = int(sys.argv[1])
raw = sys.stdin.read()
obj = json.loads(raw)

for signal in obj.get("signals", []):
    if int(signal.get("signal_id", 0)) == signal_id:
        print("false")
        raise SystemExit(0)

print("true")
PY
}

main() {
  local now
  now="$(date +"%Y%m%d-%H%M%S")"
  local temp_signal_ids=()
  local learned_count=0
  local retained_learned_count=0

  printf '\n=== ESP Matter Hub Hardware Self-Test ===\n'
  printf 'Host: %s\n' "$HUB_HOST"
  printf 'Focus: boot/network/API/IR RX/TX/button/NVS restore\n\n'

  local health_json
  if health_json="$(health)"; then
    local ok slots
    ok="$(printf '%s' "$health_json" | json_eval 'obj.get("status") == "ok"')"
    slots="$(printf '%s' "$health_json" | json_eval 'obj.get("slots", -1)')"
    if [[ "$ok" == "true" && "$slots" -eq 8 ]]; then
      record_result "health" "PASS" "status=ok, slots=8"
    else
      record_result "health" "FAIL" "unexpected health response"
    fi
  else
    record_result "health" "FAIL" "GET /api/health failed"
  fi

  local slots_json
  if slots_json="$(slots)"; then
    local slot_count
    slot_count="$(printf '%s' "$slots_json" | json_eval 'len(obj.get("slots", []))')"
    if [[ "$slot_count" -eq 8 ]]; then
      record_result "slots_contract" "PASS" "8 slots (0~7)"
    else
      record_result "slots_contract" "FAIL" "slot count=${slot_count}"
    fi
  else
    record_result "slots_contract" "FAIL" "GET /api/slots failed"
  fi

  prompt_enter "[MANUAL] Verify status LED changes: booting -> ready (green)"
  record_result "status_led" "PASS" "manual check acknowledged"

  prompt_enter "[MANUAL] Press board button once. Verify toggle action and IR TX log/LED activity in monitor"
  record_result "button_toggle" "PASS" "manual check acknowledged"

  local timeout_s=15
  local rx
  for rx in 1 2; do
    local rx_ok=0
    local status_json=""
    local attempt
    local last_reason="learning timeout"
    for attempt in $(seq 1 "$MAX_RX_ATTEMPTS"); do
      prompt_enter "[MANUAL] RX${rx} capture attempt ${attempt}/${MAX_RX_ATTEMPTS}: place remote very close to RX${rx} only"
      if ! learn_start "$timeout_s" >/dev/null; then
        record_result "rx${rx}_learn_start" "FAIL" "POST /api/learn/start failed"
        break
      fi

      if status_json="$(poll_learning_ready "$timeout_s" "$rx" "$MIN_CAPTURE_LEN")"; then
        local captured_len quality
        captured_len="$(printf '%s' "$status_json" | json_eval 'obj.get("captured_len", 0)')"
        quality="$(printf '%s' "$status_json" | json_eval 'obj.get("quality_score", 0)')"
        record_result "rx${rx}_capture" "PASS" "captured_len=${captured_len}, quality=${quality}, attempt=${attempt}"
        rx_ok=1
        break
      fi

      local rc=$?
      if [[ -n "$status_json" ]]; then
        local got_src got_len
        got_src="$(printf '%s' "$status_json" | json_eval 'obj.get("rx_source", 0)')"
        got_len="$(printf '%s' "$status_json" | json_eval 'obj.get("captured_len", 0)')"
        if [[ "$rc" -eq 2 ]]; then
          last_reason="unexpected capture rx_source=${got_src}, len=${got_len}"
        else
          last_reason="timeout"
        fi
      fi
    done

    if [[ "$rx_ok" -ne 1 ]]; then
      record_result "rx${rx}_capture" "FAIL" "${last_reason}"
      continue
    fi

    local commit_name="selftest-rx${rx}-${now}"
    local commit_json
    if commit_json="$(learn_commit "$commit_name" "light")"; then
      local signal_id
      signal_id="$(printf '%s' "$commit_json" | json_eval 'obj.get("signal_id", 0)')"
      if [[ "$signal_id" -gt 0 ]]; then
        temp_signal_ids+=("$signal_id")
        learned_count=$((learned_count + 1))
        retained_learned_count=$((retained_learned_count + 1))
        record_result "rx${rx}_commit" "PASS" "signal_id=${signal_id}"
      else
        record_result "rx${rx}_commit" "FAIL" "signal_id missing"
      fi
    else
      record_result "rx${rx}_commit" "FAIL" "POST /api/learn/commit failed"
    fi
  done

  local tx_signal=""
  if [[ ${#temp_signal_ids[@]} -gt 0 ]]; then
    tx_signal="${temp_signal_ids[0]}"
    if bind 0 "$tx_signal" "$tx_signal" "$tx_signal" "$tx_signal" >/dev/null; then
      record_result "bind_tx_signal" "PASS" "slot0 bound to signal_id=${tx_signal}"
      prompt_enter "[MANUAL] Press board button 2~3 times and confirm TX IR LED output + target device reaction"
      record_result "tx_path" "PASS" "manual check acknowledged"
    else
      record_result "bind_tx_signal" "FAIL" "POST /api/slots/0/bind failed"
    fi
  else
    record_result "bind_tx_signal" "FAIL" "no learned signal available"
  fi

  if [[ -n "$tx_signal" ]]; then
    if delete_signal "$tx_signal" >/dev/null; then
      record_result "delete_signal" "PASS" "signal_id=${tx_signal}"
      retained_learned_count=$((retained_learned_count - 1))

      local post_delete_slots_json
      if post_delete_slots_json="$(slots)"; then
        local unbound
        unbound="$(printf '%s' "$post_delete_slots_json" | slots_have_no_signal_refs "$tx_signal")"
        if [[ "$unbound" == "true" ]]; then
          record_result "cascade_unbind" "PASS" "signal_id=${tx_signal} references cleared"
        else
          record_result "cascade_unbind" "FAIL" "signal_id=${tx_signal} still referenced in slots"
        fi
      else
        record_result "cascade_unbind" "FAIL" "GET /api/slots failed after delete"
      fi

      local post_delete_signals_json
      if post_delete_signals_json="$(signals)"; then
        local removed
        removed="$(printf '%s' "$post_delete_signals_json" | signals_missing_id "$tx_signal")"
        if [[ "$removed" == "true" ]]; then
          record_result "delete_visibility" "PASS" "signal_id=${tx_signal} removed from list"
        else
          record_result "delete_visibility" "FAIL" "signal_id=${tx_signal} still appears in list"
        fi
      else
        record_result "delete_visibility" "FAIL" "GET /api/signals failed after delete"
      fi
    else
      record_result "delete_signal" "FAIL" "DELETE /api/signals/${tx_signal} failed"
    fi
  fi

  local export_json
  if export_json="$(export_nvs signals)"; then
    local export_count
    export_count="$(printf '%s' "$export_json" | json_eval 'obj.get("counts", {}).get("signals", -1)')"
    if [[ "$export_count" -ge 0 ]]; then
      record_result "nvs_export" "PASS" "signals=${export_count}"
    else
      record_result "nvs_export" "FAIL" "unexpected export response"
    fi
  else
    record_result "nvs_export" "FAIL" "GET /api/export/nvs?scope=signals failed"
  fi

  prompt_enter "[MANUAL] Power-cycle board now, wait boot complete, then continue for restore check"
  if signals_json="$(signals)"; then
    local restored
    restored="$(printf '%s' "$signals_json" | json_eval 'len(obj.get("signals", []))')"
    if [[ "$restored" -ge "$retained_learned_count" ]]; then
      record_result "restore_after_reboot" "PASS" "signals=${restored}"
    else
      record_result "restore_after_reboot" "FAIL" "signals=${restored}, expected>=${retained_learned_count}"
    fi
  else
    record_result "restore_after_reboot" "FAIL" "GET /api/signals failed after reboot"
  fi

  if [[ -n "$tx_signal" ]]; then
    bind 0 0 0 0 0 >/dev/null 2>&1 || true
  fi
  cleanup_temp_signals ${temp_signal_ids[@]-}

  printf '\n=== Self-Test Summary ===\n'
  local i
  local fail_count=0
  for i in "${!TEST_NAMES[@]}"; do
    printf '%-20s %-4s %s\n' "${TEST_NAMES[$i]}" "${TEST_STATUS[$i]}" "${TEST_DETAIL[$i]}"
    if [[ "${TEST_STATUS[$i]}" != "PASS" ]]; then
      fail_count=$((fail_count + 1))
    fi
  done

  if [[ "$fail_count" -eq 0 ]]; then
    printf '\nOVERALL: PASS\n'
    return 0
  fi

  printf '\nOVERALL: FAIL (%d)\n' "$fail_count"
  return 1
}

signals_json=""
main "$@"
