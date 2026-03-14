#!/bin/bash

set -euo pipefail

HUB_DEFAULT="esp-matter-hub-659824.local"
HUB="${HUB:-$HUB_DEFAULT}"

if [[ "${1:-}" == "--host" ]]; then
  HUB="${2:-}"
  shift 2
fi

cmd="${1:-help}"
shift || true

api() {
  local method="$1"
  local path="$2"
  local body="${3:-}"
  local url="http://${HUB}${path}"

  if [[ -n "$body" ]]; then
    curl -sS -X "$method" "$url" -H "Content-Type: application/json" -d "$body"
  else
    curl -sS -X "$method" "$url"
  fi
}

pp() {
  if command -v jq >/dev/null 2>&1; then
    jq .
  else
    cat
  fi
}

case "$cmd" in
  health)
    api GET "/api/health" | pp
    ;;
  slots)
    api GET "/api/slots" | pp
    ;;
  devices)
    api GET "/api/devices" | pp
    ;;
  signals)
    api GET "/api/signals" | pp
    ;;
  register)
    name="${1:-}"
    if [[ -z "$name" ]]; then
      echo "Usage: $0 register <name>"
      exit 1
    fi
    api POST "/api/devices/register" "{\"name\":\"${name}\",\"device_type\":\"light\"}" | pp
    ;;
  assign)
    slot="${1:-}"
    device_id="${2:-}"
    if [[ -z "$slot" || -z "$device_id" ]]; then
      echo "Usage: $0 assign <slot> <device_id>"
      exit 1
    fi
    api POST "/api/endpoints/${slot}/assign" "{\"device_id\":${device_id}}" | pp
    ;;
  unassign)
    slot="${1:-}"
    if [[ -z "$slot" ]]; then
      echo "Usage: $0 unassign <slot>"
      exit 1
    fi
    api POST "/api/endpoints/${slot}/assign" "{\"device_id\":0}" | pp
    ;;
  rename)
    device_id="${1:-}"
    name="${2:-}"
    if [[ -z "$device_id" || -z "$name" ]]; then
      echo "Usage: $0 rename <device_id> <new_name>"
      exit 1
    fi
    api POST "/api/devices/${device_id}/rename" "{\"name\":\"${name}\"}" | pp
    ;;
  export)
    scope="${1:-all}"
    api GET "/api/export/nvs?scope=${scope}" | pp
    ;;
  smoke)
    api GET "/api/health" | pp
    api GET "/api/slots" | pp
    api GET "/api/devices" | pp
    ;;
  help|*)
    cat <<EOF
Usage: $0 [--host <host>] <command> [args]

Default host: ${HUB_DEFAULT}

Commands:
  health                    GET /api/health
  slots                     GET /api/slots
  devices                   GET /api/devices
  signals                   GET /api/signals
  register <name>           POST /api/devices/register
  assign <slot> <device>    POST /api/endpoints/{slot}/assign
  unassign <slot>           POST /api/endpoints/{slot}/assign (device_id=0)
  rename <id> <new_name>    POST /api/devices/{id}/rename
  export [scope]            GET /api/export/nvs?scope=all|signals|bindings|devices
  smoke                     health + slots + devices

Examples:
  $0 health
  $0 register "Living Light"
  $0 assign 0 1
  $0 rename 1 "Living Main"
  $0 unassign 0
  $0 export all
  $0 --host 192.168.0.30 slots
EOF
    ;;
esac
