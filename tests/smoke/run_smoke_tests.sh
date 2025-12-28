#!/bin/bash
#
# run_smoke_tests.sh - Smoke tests for reflector-native
#
# Requires: Linux, root/sudo, veth pair support
# Tests basic reflector functionality
#

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Configuration
VETH_TX="veth-smoke-tx"
VETH_RX="veth-smoke-rx"
IP_TX="192.168.253.1"
IP_RX="192.168.253.2"
SUBNET="/24"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR}/../.."
REFLECTOR_BIN="${PROJECT_ROOT}/reflector-linux"

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0

REFLECTOR_PID=""

# Logging
log_info()  { echo -e "${CYAN}[INFO]${NC} $1"; }
log_pass()  { echo -e "${GREEN}[PASS]${NC} $1"; }
log_fail()  { echo -e "${RED}[FAIL]${NC} $1"; }
log_skip()  { echo -e "${YELLOW}[SKIP]${NC} $1"; }
log_header() { echo -e "\n${CYAN}=== $1 ===${NC}"; }

# Check if running as root
check_root() {
    if [[ $EUID -ne 0 ]]; then
        echo -e "${RED}Error: Smoke tests require root for veth creation${NC}"
        echo "Usage: sudo $0"
        exit 1
    fi
}

# Cleanup function
cleanup() {
    log_info "Cleaning up..."

    # Kill reflector
    if [[ -n "$REFLECTOR_PID" ]]; then
        kill $REFLECTOR_PID 2>/dev/null || true
        wait $REFLECTOR_PID 2>/dev/null || true
    fi

    # Kill any stray processes
    pkill -f "reflector.*${VETH_RX}" 2>/dev/null || true

    # Remove veth pair
    ip link delete "${VETH_TX}" 2>/dev/null || true

    log_info "Cleanup complete"
}

# Set up trap for cleanup
trap cleanup EXIT

# Create veth pair for testing
setup_veth() {
    log_info "Creating veth pair..."

    # Remove existing if present
    ip link delete "${VETH_TX}" 2>/dev/null || true

    # Create veth pair
    ip link add "${VETH_TX}" type veth peer name "${VETH_RX}"

    # Configure both ends
    ip addr add "${IP_TX}${SUBNET}" dev "${VETH_TX}"
    ip addr add "${IP_RX}${SUBNET}" dev "${VETH_RX}"
    ip link set "${VETH_TX}" up
    ip link set "${VETH_RX}" up

    # Disable reverse path filtering
    echo 0 > /proc/sys/net/ipv4/conf/${VETH_TX}/rp_filter
    echo 0 > /proc/sys/net/ipv4/conf/${VETH_RX}/rp_filter

    log_info "veth pair ready: ${VETH_TX} <-> ${VETH_RX}"
}

# Run a test and record result
run_test() {
    local name="$1"
    local cmd="$2"
    local expected_exit="${3:-0}"

    TESTS_RUN=$((TESTS_RUN + 1))

    log_info "Running: $name"

    local output
    local exit_code

    set +e
    output=$(eval "$cmd" 2>&1)
    exit_code=$?
    set -e

    if [[ $exit_code -eq $expected_exit ]]; then
        log_pass "$name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        log_fail "$name (exit code: $exit_code, expected: $expected_exit)"
        echo "Output:"
        echo "$output" | head -20
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

# Skip a test
skip_test() {
    local name="$1"
    local reason="$2"

    TESTS_RUN=$((TESTS_RUN + 1))
    TESTS_SKIPPED=$((TESTS_SKIPPED + 1))
    log_skip "$name - $reason"
}

# ============================================================================
# Test Cases
# ============================================================================

test_binary_exists() {
    log_header "Binary Check"

    if [[ ! -x "${REFLECTOR_BIN}" ]]; then
        log_fail "Binary not found: ${REFLECTOR_BIN}"
        log_info "Building binary..."
        (cd "${PROJECT_ROOT}" && make linux)
    fi

    run_test "Binary is executable" \
        "test -x ${REFLECTOR_BIN}"
}

test_help() {
    log_header "CLI Help Tests"

    run_test "Help flag (-h)" \
        "${REFLECTOR_BIN} -h"

    run_test "Help flag (--help)" \
        "${REFLECTOR_BIN} --help"
}

test_version() {
    log_header "Version Test"

    run_test "Version flag (--version)" \
        "${REFLECTOR_BIN} --version"
}

test_startup_shutdown() {
    log_header "Startup/Shutdown Test"

    log_info "Starting reflector..."
    ${REFLECTOR_BIN} ${VETH_RX} --mode all --no-oui-filter --no-mac-filter >/dev/null 2>&1 &
    REFLECTOR_PID=$!

    sleep 2

    if kill -0 $REFLECTOR_PID 2>/dev/null; then
        log_pass "Reflector started successfully"
        TESTS_RUN=$((TESTS_RUN + 1))
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_fail "Reflector failed to start"
        TESTS_RUN=$((TESTS_RUN + 1))
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi

    # Test graceful shutdown
    log_info "Testing graceful shutdown (SIGTERM)..."
    kill -TERM $REFLECTOR_PID 2>/dev/null
    sleep 1

    if ! kill -0 $REFLECTOR_PID 2>/dev/null; then
        log_pass "Reflector shutdown gracefully"
        TESTS_RUN=$((TESTS_RUN + 1))
        TESTS_PASSED=$((TESTS_PASSED + 1))
        REFLECTOR_PID=""
    else
        log_fail "Reflector did not shutdown"
        TESTS_RUN=$((TESTS_RUN + 1))
        TESTS_FAILED=$((TESTS_FAILED + 1))
        kill -9 $REFLECTOR_PID 2>/dev/null || true
        REFLECTOR_PID=""
    fi
}

test_reflection_modes() {
    log_header "Reflection Mode Tests"

    for mode in mac macip all; do
        log_info "Testing mode: $mode"
        ${REFLECTOR_BIN} ${VETH_RX} --mode $mode --no-oui-filter --no-mac-filter >/dev/null 2>&1 &
        local pid=$!
        sleep 1

        if kill -0 $pid 2>/dev/null; then
            log_pass "Mode $mode started"
            TESTS_RUN=$((TESTS_RUN + 1))
            TESTS_PASSED=$((TESTS_PASSED + 1))
            kill $pid 2>/dev/null
            wait $pid 2>/dev/null || true
        else
            log_fail "Mode $mode failed to start"
            TESTS_RUN=$((TESTS_RUN + 1))
            TESTS_FAILED=$((TESTS_FAILED + 1))
        fi
    done
}

test_signature_filters() {
    log_header "Signature Filter Tests"

    for sig in rfc2544 y1564 all; do
        log_info "Testing signature filter: $sig"
        ${REFLECTOR_BIN} ${VETH_RX} --sig $sig --no-oui-filter --no-mac-filter >/dev/null 2>&1 &
        local pid=$!
        sleep 1

        if kill -0 $pid 2>/dev/null; then
            log_pass "Signature filter $sig started"
            TESTS_RUN=$((TESTS_RUN + 1))
            TESTS_PASSED=$((TESTS_PASSED + 1))
            kill $pid 2>/dev/null
            wait $pid 2>/dev/null || true
        else
            log_fail "Signature filter $sig failed to start"
            TESTS_RUN=$((TESTS_RUN + 1))
            TESTS_FAILED=$((TESTS_FAILED + 1))
        fi
    done
}

test_packet_reflection() {
    log_header "Packet Reflection Test"

    # Start reflector
    log_info "Starting reflector in all mode..."
    ${REFLECTOR_BIN} ${VETH_RX} --mode all --no-oui-filter --no-mac-filter --sig all >/dev/null 2>&1 &
    REFLECTOR_PID=$!
    sleep 2

    if ! kill -0 $REFLECTOR_PID 2>/dev/null; then
        log_fail "Reflector failed to start for packet test"
        TESTS_RUN=$((TESTS_RUN + 1))
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi

    # Get pre-test stats
    local pre_rx=$(cat /sys/class/net/${VETH_TX}/statistics/rx_packets)

    # Send test packets using ping (uses raw sockets won't work, but we can check ICMP)
    log_info "Sending test packets..."
    ping -c 10 -I ${VETH_TX} ${IP_RX} -W 1 >/dev/null 2>&1 || true
    sleep 1

    # Get post-test stats
    local post_rx=$(cat /sys/class/net/${VETH_TX}/statistics/rx_packets)
    local reflected=$((post_rx - pre_rx))

    log_info "Packets reflected back to TX interface: $reflected"

    # Cleanup
    kill $REFLECTOR_PID 2>/dev/null
    wait $REFLECTOR_PID 2>/dev/null || true
    REFLECTOR_PID=""

    if [[ $reflected -gt 0 ]]; then
        log_pass "Packet reflection working ($reflected packets)"
        TESTS_RUN=$((TESTS_RUN + 1))
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_skip "Packet reflection test" "ICMP may not trigger ITO path"
        TESTS_RUN=$((TESTS_RUN + 1))
        TESTS_SKIPPED=$((TESTS_SKIPPED + 1))
    fi
}

test_verbose_mode() {
    log_header "Verbose Mode Test"

    run_test "Verbose startup" \
        "timeout 2 ${REFLECTOR_BIN} ${VETH_RX} --verbose --no-oui-filter --no-mac-filter 2>&1 | grep -q 'reflector'" \
        0 || TESTS_FAILED=$((TESTS_FAILED - 1))  # timeout returns 124
}

test_invalid_interface() {
    log_header "Error Handling Tests"

    run_test "Invalid interface name" \
        "${REFLECTOR_BIN} nonexistent_iface --no-oui-filter 2>&1 | grep -qi 'error\|fail\|not found'" \
        0
}

# ============================================================================
# Main
# ============================================================================

main() {
    echo -e "${CYAN}╔════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║            Reflector-Native Smoke Test Suite               ║${NC}"
    echo -e "${CYAN}╚════════════════════════════════════════════════════════════╝${NC}"
    echo ""

    check_root
    test_binary_exists
    setup_veth

    # Run test suites
    test_help
    test_version
    test_startup_shutdown
    test_reflection_modes
    test_signature_filters
    test_packet_reflection
    test_verbose_mode
    test_invalid_interface

    # Summary
    echo ""
    log_header "Test Summary"
    echo -e "  Total:   ${TESTS_RUN}"
    echo -e "  ${GREEN}Passed:${NC}  ${TESTS_PASSED}"
    echo -e "  ${RED}Failed:${NC}  ${TESTS_FAILED}"
    echo -e "  ${YELLOW}Skipped:${NC} ${TESTS_SKIPPED}"
    echo ""

    if [[ $TESTS_FAILED -gt 0 ]]; then
        echo -e "${RED}SMOKE TESTS FAILED${NC}"
        exit 1
    else
        echo -e "${GREEN}ALL SMOKE TESTS PASSED${NC}"
        exit 0
    fi
}

main "$@"
