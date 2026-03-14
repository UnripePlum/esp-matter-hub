#!/bin/bash

set -uo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
ENV_FILE="$PROJECT_DIR/.hub_api_test_env.sh"
HUB_HOST="${1:-esp-matter-hub-659824.local}"

if [[ ! -f "$ENV_FILE" ]]; then
  echo "Missing env file: $ENV_FILE"
  exit 1
fi

export HUB_HOST
source "$ENV_FILE"

print_banner() {
  echo ""
  echo "hub_api_test"
  echo "Host: $HUB_HOST"
  echo "Type /help for commands, /exit to quit."
  echo ""
}

run_cmd() {
  local input="$1"
  input="${input#${input%%[![:space:]]*}}"
  input="${input%${input##*[![:space:]]}}"
  if [[ -z "$input" ]]; then
    return 0
  fi

  case "$input" in
    /help|help)
      api_help
      ;;
    /host)
      echo "$HUB_HOST"
      ;;
    /exit|exit|quit)
      return 99
      ;;
    /health)
      health
      ;;
    /slots)
      slots
      ;;
    /devices)
      devices
      ;;
    /signals)
      signals
      ;;
    /smoke)
      smoke
      ;;
    *)
      if [[ "$input" == /* ]]; then
        local raw="${input#/}"
        eval "$raw"
      else
        eval "$input"
      fi
      ;;
  esac
}

print_banner
while true; do
  read -r -e -p "hub_api_test> " line || break
  run_cmd "$line"
  rc=$?
  if [[ $rc -eq 99 ]]; then
    break
  fi
done
