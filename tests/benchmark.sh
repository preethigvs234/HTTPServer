#!/usr/bin/env bash
# =============================================================================
# tests/benchmark.sh – HTTP server benchmarking with ApacheBench (ab) or wrk
#
# Usage:
#   bash tests/benchmark.sh [HOST] [PORT]
#
# Prerequisites (install one):
#   sudo apt install apache2-utils   # provides ab
#   sudo apt install wrk             # provides wrk
# =============================================================================

set -euo pipefail

HOST="${1:-127.0.0.1}"
PORT="${2:-8080}"
BASE="http://${HOST}:${PORT}/"

CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'; YELLOW='\033[1;33m'

header() { echo -e "\n${CYAN}${BOLD}══ $* ══${RESET}"; }

# ── Check server is up ───────────────────────────────────────────────────────
echo -e "${BOLD}Benchmarking ${BASE}${RESET}"
nc -z "${HOST}" "${PORT}" 2>/dev/null || {
    echo "ERROR: Server not reachable at ${HOST}:${PORT}"
    exit 1
}

# ── ApacheBench ──────────────────────────────────────────────────────────────
if command -v ab &>/dev/null; then
    header "ApacheBench – 10,000 requests, concurrency 100"
    ab -n 10000 -c 100 -q "${BASE}"
    echo ""

    header "ApacheBench – 10,000 requests, concurrency 500"
    ab -n 10000 -c 500 -q "${BASE}"
    echo ""
else
    echo -e "${YELLOW}ab not found – install with: sudo apt install apache2-utils${RESET}"
fi

# ── wrk ─────────────────────────────────────────────────────────────────────
if command -v wrk &>/dev/null; then
    header "wrk – 4 threads, 100 connections, 10 seconds"
    wrk -t4 -c100 -d10s "${BASE}"
    echo ""

    header "wrk – 4 threads, 500 connections, 30 seconds"
    wrk -t4 -c500 -d30s "${BASE}"
    echo ""
else
    echo -e "${YELLOW}wrk not found – install with: sudo apt install wrk${RESET}"
fi

echo -e "${CYAN}${BOLD}Benchmark complete.${RESET}"
