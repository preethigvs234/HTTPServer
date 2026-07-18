#!/usr/bin/env bash
# =============================================================================
# tests/run_tests.sh – Integration test suite for the HTTP server
#
# Usage:
#   cd build && cmake .. && make -j$(nproc)
#   cd ..
#   bash tests/run_tests.sh [HOST] [PORT]
#
# Prerequisites: curl, nc (netcat), bc
# =============================================================================

set -euo pipefail

HOST="${1:-127.0.0.1}"
PORT="${2:-8080}"
BASE="http://${HOST}:${PORT}"
PASS=0
FAIL=0
TOTAL=0

# ── Colour helpers ──────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; RESET='\033[0m'; BOLD='\033[1m'

header()  { echo -e "\n${CYAN}${BOLD}══ $* ══${RESET}"; }
pass()    { echo -e "  ${GREEN}✔ PASS${RESET}  $*"; ((PASS++)); ((TOTAL++)); }
fail()    { echo -e "  ${RED}✘ FAIL${RESET}  $*"; ((FAIL++)); ((TOTAL++)); }
info()    { echo -e "  ${YELLOW}ℹ${RESET}      $*"; }

# ── Wait for server ─────────────────────────────────────────────────────────
wait_for_server() {
    local retries=20
    while ((retries-- > 0)); do
        nc -z "${HOST}" "${PORT}" 2>/dev/null && return 0
        sleep 0.2
    done
    echo -e "${RED}ERROR: Server not reachable at ${HOST}:${PORT}${RESET}"
    exit 1
}

# ── HTTP helper ─────────────────────────────────────────────────────────────
# Returns the HTTP status code for a GET request
http_status() {
    curl -s -o /dev/null -w "%{http_code}" "${BASE}${1}"
}

# Returns the HTTP status code for a HEAD request
http_head_status() {
    curl -s -I -o /dev/null -w "%{http_code}" "${BASE}${1}"
}

# Returns the Content-Type header value
http_content_type() {
    curl -s -I "${BASE}${1}" | grep -i '^content-type:' | awk '{print $2}' | tr -d '\r'
}

# Returns body text for a GET request
http_body() {
    curl -s "${BASE}${1}"
}

# ── Test definitions ─────────────────────────────────────────────────────────

test_200_index() {
    header "Test: GET / → 200 (index.html)"
    local status
    status=$(http_status "/")
    [[ "$status" == "200" ]] && pass "GET / returned 200" || fail "GET / returned $status (expected 200)"

    local body
    body=$(http_body "/")
    echo "$body" | grep -qi "HPHTTPServer" && pass "Body contains expected content" \
                                           || fail "Body missing expected content"
}

test_200_explicit_html() {
    header "Test: GET /index.html → 200"
    local status
    status=$(http_status "/index.html")
    [[ "$status" == "200" ]] && pass "GET /index.html returned 200" || fail "Got $status"
}

test_200_about() {
    header "Test: GET /about.html → 200"
    local status
    status=$(http_status "/about.html")
    [[ "$status" == "200" ]] && pass "GET /about.html returned 200" || fail "Got $status"
}

test_200_css() {
    header "Test: GET /styles.css → 200 with text/css"
    local status ct
    status=$(http_status "/styles.css")
    ct=$(http_content_type "/styles.css")
    [[ "$status" == "200" ]] && pass "GET /styles.css returned 200" || fail "Got $status"
    echo "$ct" | grep -qi "text/css" && pass "Content-Type is text/css" \
                                     || fail "Content-Type is '$ct' (expected text/css)"
}

test_404_missing() {
    header "Test: GET /nonexistent → 404"
    local status
    status=$(http_status "/this_file_does_not_exist_xyz.html")
    [[ "$status" == "404" ]] && pass "Missing file returned 404" || fail "Got $status"

    local body
    body=$(http_body "/this_file_does_not_exist_xyz.html")
    echo "$body" | grep -qi "404" && pass "404 page contains '404' text" \
                                  || fail "404 response body missing '404'"
}

test_head_method() {
    header "Test: HEAD / → 200 with no body"
    local status
    status=$(http_head_status "/")
    [[ "$status" == "200" ]] && pass "HEAD / returned 200" || fail "Got $status"

    local body_len
    body_len=$(curl -s -I "${BASE}/" -o /dev/null -w "%{size_download}")
    # HEAD response body download should be 0 bytes
    [[ "$body_len" -eq 0 ]] && pass "HEAD / returned 0-byte body" \
                             || fail "HEAD / returned ${body_len}-byte body (expected 0)"
}

test_traversal_attack() {
    header "Test: Directory traversal attack → 403 or 404"
    local status
    status=$(http_status "/../../../etc/passwd")
    [[ "$status" == "403" || "$status" == "404" ]] \
        && pass "Traversal attack blocked (got $status)" \
        || fail "Traversal attack NOT blocked! Got $status"

    status=$(http_status "/..%2F..%2Fetc%2Fpasswd")
    [[ "$status" == "403" || "$status" == "404" ]] \
        && pass "URL-encoded traversal blocked (got $status)" \
        || fail "URL-encoded traversal NOT blocked! Got $status"
}

test_concurrent_requests() {
    header "Test: Concurrent requests (20 parallel)"
    local pids=()
    local tmpdir
    tmpdir=$(mktemp -d)

    for i in $(seq 1 20); do
        (
            st=$(http_status "/")
            echo "$st" > "${tmpdir}/${i}.status"
        ) &
        pids+=($!)
    done

    # Wait for all background jobs
    for pid in "${pids[@]}"; do wait "$pid"; done

    local failed=0
    for i in $(seq 1 20); do
        st=$(cat "${tmpdir}/${i}.status" 2>/dev/null || echo "ERR")
        [[ "$st" != "200" ]] && ((failed++))
    done
    rm -rf "$tmpdir"

    [[ "$failed" -eq 0 ]] \
        && pass "All 20 concurrent requests returned 200" \
        || fail "$failed / 20 concurrent requests failed"
}

test_multiple_clients_sequential() {
    header "Test: Sequential requests from multiple simulated clients"
    local ok=0
    for i in $(seq 1 10); do
        st=$(http_status "/")
        [[ "$st" == "200" ]] && ((ok++))
    done
    [[ "$ok" -eq 10 ]] \
        && pass "10/10 sequential requests returned 200" \
        || fail "$ok/10 succeeded"
}

test_large_file() {
    header "Test: Large file response (logo.png)"
    # logo.png is a generated PNG - test it exists and has non-zero size
    local status size
    status=$(http_status "/logo.png")
    if [[ "$status" == "200" ]]; then
        size=$(curl -s "${BASE}/logo.png" | wc -c)
        pass "GET /logo.png returned 200 (${size} bytes)"
        [[ "$size" -gt 100 ]] && pass "File has non-trivial size" \
                               || fail "File suspiciously small (${size} bytes)"
    elif [[ "$status" == "404" ]]; then
        info "logo.png not present – skipping size check (404 is correct)"
        pass "Server handled missing logo.png cleanly"
    else
        fail "Got $status for /logo.png"
    fi
}

test_response_headers() {
    header "Test: Response contains required headers"
    local headers
    headers=$(curl -s -I "${BASE}/")

    echo "$headers" | grep -qi "Content-Type" \
        && pass "Content-Type header present" \
        || fail "Content-Type header missing"

    echo "$headers" | grep -qi "Content-Length" \
        && pass "Content-Length header present" \
        || fail "Content-Length header missing"

    echo "$headers" | grep -qi "Server:" \
        && pass "Server header present" \
        || fail "Server header missing"
}

test_latency() {
    header "Test: Response latency < 500ms"
    local time_ms
    time_ms=$(curl -s -o /dev/null -w "%{time_total}" "${BASE}/" | awk '{printf "%d", $1*1000}')
    info "Latency: ${time_ms} ms"
    [[ "$time_ms" -lt 500 ]] \
        && pass "Latency ${time_ms}ms < 500ms threshold" \
        || fail "Latency ${time_ms}ms exceeds 500ms threshold"
}

# ── Main ────────────────────────────────────────────────────────────────────

echo -e "\n${BOLD}${CYAN}════════════════════════════════════════════"
echo        "  HPHTTPServer Integration Test Suite"
echo -e     "════════════════════════════════════════════${RESET}"
echo -e "  Target: ${BASE}\n"

info "Waiting for server to be ready..."
wait_for_server
info "Server is up!\n"

test_200_index
test_200_explicit_html
test_200_about
test_200_css
test_404_missing
test_head_method
test_traversal_attack
test_concurrent_requests
test_multiple_clients_sequential
test_large_file
test_response_headers
test_latency

# ── Summary ─────────────────────────────────────────────────────────────────
echo -e "\n${BOLD}${CYAN}════════════════════════════════════════════"
echo        "  Test Summary"
echo -e     "════════════════════════════════════════════${RESET}"
echo -e "  Total : ${TOTAL}"
echo -e "  ${GREEN}Passed: ${PASS}${RESET}"
if [[ "$FAIL" -gt 0 ]]; then
    echo -e "  ${RED}Failed: ${FAIL}${RESET}"
    exit 1
else
    echo -e "  ${GREEN}All tests passed! 🎉${RESET}\n"
    exit 0
fi
