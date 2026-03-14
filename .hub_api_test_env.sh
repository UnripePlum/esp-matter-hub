#!/bin/bash

resolve_hub_candidates() {
  if [[ "$HUB_HOST" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "$HUB_HOST"
    return 0
  fi

  python3 - <<'PY'
import ipaddress
import socket
import os
import re
import subprocess

host = os.environ.get("HUB_HOST", "")
if not host:
    raise SystemExit(0)

ips = []

def add_ip(ip):
    if ip and ip not in ips:
        ips.append(ip)

if host.endswith(".local"):
    try:
        p = subprocess.Popen(
            ["dns-sd", "-G", "v4", host],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        out = ""
        try:
            out, _ = p.communicate(timeout=2.5)
        except subprocess.TimeoutExpired as e:
            out = e.output or ""
            p.kill()
            try:
                rest, _ = p.communicate(timeout=0.2)
                out += rest or ""
            except Exception:
                pass

        for line in (out or "").splitlines():
            m = re.search(r"(\d+\.\d+\.\d+\.\d+)", line)
            if m:
                add_ip(m.group(1))
    except Exception:
        pass

try:
    infos = socket.getaddrinfo(host, None, socket.AF_INET, 0, 0, socket.AI_ADDRCONFIG)
    for info in infos:
        add_ip(info[4][0])
except Exception:
    pass

private = []
for ip in ips:
    try:
        obj = ipaddress.ip_address(ip)
        if obj.is_private:
            private.append(ip)
    except Exception:
        pass

for ip in private:
    print(ip)
PY
}

curl_hub() {
  local method="$1"
  local path="$2"
  local data="${3:-}"
  local timeout_args=(-4 -sS --connect-timeout 2 --max-time 8)
  local candidates=()
  while IFS= read -r c; do
    [[ -n "$c" ]] && candidates+=("$c")
  done < <(resolve_hub_candidates)

  if [ ${#candidates[@]} -eq 0 ]; then
    echo "No LAN IP resolved for ${HUB_HOST}" >&2
    return 1
  fi

  local target
  for target in "${candidates[@]}"; do
    if [ "$method" = "GET" ]; then
      if curl "${timeout_args[@]}" -H "Host: ${HUB_HOST}" "http://${target}${path}"; then
        return 0
      fi
    else
      if curl "${timeout_args[@]}" -X "$method" -H "Host: ${HUB_HOST}" -H "Content-Type: application/json" -d "$data" "http://${target}${path}"; then
        return 0
      fi
    fi
  done

  return 1
}

api_get() {
  curl_hub GET "$1"
}

api_post() {
  curl_hub POST "$1" "$2"
}

api_delete() {
  curl_hub DELETE "$1"
}

pp() {
  if command -v jq >/dev/null 2>&1; then
    jq .
  else
    cat
  fi
}

health() { api_get /api/health | pp; }
slots() { api_get /api/slots | pp; }
devices() { api_get /api/devices | pp; }
signals() { api_get /api/signals | pp; }

delete_signal() {
  if [ -z "${1:-}" ]; then
    echo "usage: delete_signal <signal_id>"
    return 1
  fi
  api_delete "/api/signals/$1" | pp
}

register() {
  local name="$*"
  if [ -z "$name" ]; then
    echo "usage: register <name>"
    return 1
  fi
  api_post /api/devices/register "{\"name\":\"${name}\",\"device_type\":\"light\"}" | pp
}

assign() {
  if [ -z "${1:-}" ] || [ -z "${2:-}" ]; then
    echo "usage: assign <slot> <device_id>"
    return 1
  fi
  api_post "/api/endpoints/$1/assign" "{\"device_id\":$2}" | pp
}

unassign() {
  if [ -z "${1:-}" ]; then
    echo "usage: unassign <slot>"
    return 1
  fi
  api_post "/api/endpoints/$1/assign" "{\"device_id\":0}" | pp
}

rename() {
  if [ -z "${1:-}" ] || [ -z "${2:-}" ]; then
    echo "usage: rename <device_id> <new_name>"
    return 1
  fi
  local device_id="$1"
  shift
  local name="$*"
  api_post "/api/devices/${device_id}/rename" "{\"name\":\"${name}\"}" | pp
}

bind() {
  if [ -z "${1:-}" ] || [ -z "${2:-}" ] || [ -z "${3:-}" ] || [ -z "${4:-}" ] || [ -z "${5:-}" ]; then
    echo "usage: bind <slot> <on_signal_id> <off_signal_id> <level_up_signal_id> <level_down_signal_id>"
    return 1
  fi
  api_post "/api/slots/$1/bind" "{\"on_signal_id\":$2,\"off_signal_id\":$3,\"level_up_signal_id\":$4,\"level_down_signal_id\":$5}" | pp
}

learn_start() {
  local timeout="${1:-15}"
  api_post /api/learn/start "{\"timeout_s\":$timeout}" | pp
}

learn_status() { api_get /api/learn/status | pp; }

learn_commit() {
  local name="${1:-}"
  local device_type="${2:-light}"
  api_post /api/learn/commit "{\"name\":\"${name}\",\"device_type\":\"${device_type}\"}" | pp
}

export_nvs() {
  local scope="${1:-all}"
  api_get "/api/export/nvs?scope=${scope}" | pp
}

commission_open() {
  local timeout="${1:-300}"
  api_post /api/commissioning/open "{\"timeout_s\":$timeout}" | pp
}

smoke() {
  health
  slots
  devices
}

hw_selftest() {
  local script="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/hw_selftest.sh"
  if [ ! -f "$script" ]; then
    echo "Missing self-test script: $script"
    return 1
  fi
  bash "$script" "$HUB_HOST"
}

api_help() {
  cat <<'EOF'
commands:
  /help, /health, /slots, /devices, /signals, /smoke, /host, /exit

api commands:
  health
  slots
  devices
  signals
  delete_signal <signal_id>
  register <name>
  assign <slot> <device_id>
  unassign <slot>
  rename <device_id> <new_name>
  bind <slot> <on_signal_id> <off_signal_id> <level_up_signal_id> <level_down_signal_id>
  learn_start [timeout_s]
  learn_status
  learn_commit [name] [device_type]
  export_nvs [all|signals|bindings|devices]
  commission_open [timeout_s]
  smoke
  hw_selftest
EOF
}
