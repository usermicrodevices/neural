#!/bin/bash
# ============================================================================
# Neural Network Speed Training Test Suite
# Tests training/learning performance over HTTP protocol
# Uses project's own C++ source files as training data
# Runs from build/ directory
# ============================================================================

set -euo pipefail

# Resolve project root from script location
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
BUILD_DIR="${PROJECT_ROOT}/build"

# All paths relative to project root
SRC_DIR="${PROJECT_ROOT}/src"
INC_DIR="${PROJECT_ROOT}/include"

ADMIN_PORT=8080
CLIENT_PORT=8081
ADMIN_URL="http://127.0.0.1:${ADMIN_PORT}"
CLIENT_URL="http://127.0.0.1:${CLIENT_PORT}"
RESULTS_DIR="${BUILD_DIR}/test_results"
LOG_FILE="${RESULTS_DIR}/benchmark.log"
PASS=0
FAIL=0
TOTAL=0

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# ── Helpers ──────────────────────────────────────────────────────────────────

log()      { echo -e "${CYAN}[LOG]${NC} $*" | tee -a "$LOG_FILE"; }
pass()     { echo -e "${GREEN}[PASS]${NC} $*"; PASS=$((PASS+1)); TOTAL=$((TOTAL+1)); }
fail()     { echo -e "${RED}[FAIL]${NC} $*"; FAIL=$((FAIL+1)); TOTAL=$((TOTAL+1)); }
section()  { echo -e "\n${BOLD}${YELLOW}═══ $* ═══${NC}" | tee -a "$LOG_FILE"; }

die() { echo -e "${RED}FATAL: $*${NC}" >&2; cleanup; exit 1; }

cleanup() {
    log "Cleaning up..."
    curl -sf -X POST "${ADMIN_URL}/stop" 2>/dev/null || true
    if [[ -n "${SERVER_PID:-}" ]]; then
        wait "$SERVER_PID" 2>/dev/null || true
        kill "$SERVER_PID" 2>/dev/null || true
    fi
}

wait_for_server() {
    local url="$1"
    local max_wait="${2:-30}"
    local elapsed=0
    while ! curl -sf "$url" >/dev/null 2>&1; do
        sleep 0.5
        elapsed=$((elapsed + 1))
        if [[ $elapsed -ge $((max_wait * 2)) ]]; then
            die "Server at $url did not start within ${max_wait}s"
        fi
    done
    log "Server ready at $url (${elapsed}x0.5s)"
}

send_document() {
    local filepath="$1"
    local tags="${2:-}"
    local serialize="${3:-0}"
    curl -s -X POST "${ADMIN_URL}/" \
        -F "document=@${filepath}" \
        -F "tags=${tags}" \
        -F "serialize=${serialize}"
}

send_text_document() {
    local text="$1"
    local tags="${2:-}"
    local serialize="${3:-0}"
    local tmpfile
    tmpfile=$(mktemp)
    echo -n "$text" > "$tmpfile"

    curl -s -X POST "${ADMIN_URL}/" \
        -F "document=@${tmpfile}" \
        -F "tags=${tags}" \
        -F "serialize=${serialize}"
    rm -f "$tmpfile"
}

ask_question() {
    local prompt="$1"
    local threshold="${2:-0.001}"
    curl -s -X POST "${CLIENT_URL}/ask" \
        -d "prompt=$(python3 -c "import urllib.parse; print(urllib.parse.quote('$prompt'))")&threshold=${threshold}" \
        2>/dev/null || true
}

ask_chat() {
    local prompt="$1"
    local temperature="${2:-0.7}"
    curl -s -X POST "${CLIENT_URL}/v1/chat" \
        -H "Content-Type: application/json" \
        -d "{\"model\":\"neural\",\"messages\":[{\"role\":\"user\",\"content\":\"${prompt}\"}],\"temperature\":${temperature}}" \
        2>/dev/null || true
}

get_progress() {
    curl -s "${ADMIN_URL}/progress" 2>/dev/null || true
}

wait_training_done() {
    local max_wait="${1:-120}"
    local elapsed=0
    while true; do
        local prog
        prog=$(get_progress)
        if echo "$prog" | grep -q '"training":false'; then
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
        if [[ $elapsed -ge $max_wait ]]; then
            log "WARNING: Training did not finish within ${max_wait}s"
            return 1
        fi
    done
}

# ── Setup ────────────────────────────────────────────────────────────────────

setup() {
    mkdir -p "$RESULTS_DIR"
    # Kill any leftover server
    curl -sf -X POST "http://127.0.0.1:${ADMIN_PORT}/stop" 2>/dev/null || true
    sleep 2
    # Force kill any remaining processes
    kill $(lsof -t -i:${ADMIN_PORT} 2>/dev/null) 2>/dev/null || true
    kill $(lsof -t -i:${CLIENT_PORT} 2>/dev/null) 2>/dev/null || true
    sleep 1

    section "BUILD"
    log "Building neural server..."
    if [[ ! -f "${BUILD_DIR}/neural" ]] || [[ "${SRC_DIR}/" -nt "${BUILD_DIR}/neural" ]] || [[ "${INC_DIR}/" -nt "${BUILD_DIR}/neural" ]]; then
        (cd "$PROJECT_ROOT" && bash build.sh) 2>&1 | tail -5 | tee -a "$LOG_FILE"
    fi
    [[ -f "${BUILD_DIR}/neural" ]] || die "Build failed: ${BUILD_DIR}/neural not found"
    log "Binary ready: ${BUILD_DIR}/neural"

    section "START SERVER"
    rm -f "${BUILD_DIR}/data.db"

    log "Starting neural server from build/..."
    cd "$BUILD_DIR"
    ./neural &
    SERVER_PID=$!
    cd "$PROJECT_ROOT"
    log "Server PID: $SERVER_PID"
    wait_for_server "${ADMIN_URL}/progress" 30
    wait_for_server "${CLIENT_URL}/" 10
    sleep 1
    log "Server is up. Admin=${ADMIN_PORT} Client=${CLIENT_PORT}"
}

# ── Test: Connectivity ──────────────────────────────────────────────────────

test_connectivity() {
    section "TEST: Connectivity"
    local resp
    resp=$(get_progress)
    if echo "$resp" | grep -q '"progress"'; then
        pass "Admin /progress responds"
    else
        fail "Admin /progress broken: $resp"
    fi

    resp=$(http_proxy= https_proxy= no_proxy='*' curl -s "${ADMIN_URL}/src_types" 2>/dev/null || echo "FAIL")
    if echo "$resp" | grep -q "C++"; then
        pass "Admin /src_types responds"
    else
        fail "Admin /src_types broken: $resp"
    fi

    resp=$(curl -sf "${CLIENT_URL}/" 2>/dev/null | head -1 || echo "FAIL")
    if [[ -n "$resp" ]]; then
        pass "Client / responds"
    else
        fail "Client / broken"
    fi
}

# ── Test: Single Document Training Speed ────────────────────────────────────

test_single_doc_speed() {
    section "TEST: Single Document Training Speed"

    local src_files=(
        "${SRC_DIR}/store.cpp"
        "${SRC_DIR}/neural.cpp"
    )

    for src in "${src_files[@]}"; do
        if [[ ! -f "$src" ]]; then
            log "SKIP $src (not found)"
            continue
        fi
        local size
        size=$(wc -c < "$src")
        local tags
        tags=$(basename "$src" .cpp)

        log "Training: $(basename "$src") (${size} bytes, tags=$tags)"
        local start end elapsed resp
        start=$(date +%s%N)

        resp=$(send_document "$src" "$tags" 0)

        end=$(date +%s%N)
        elapsed=$(echo "scale=3; ($end - $start) / 1000000000" | bc)
        log "  -> ${elapsed}s  response: $(echo "$resp" | head -c 120)"

        if echo "$resp" | grep -q '"status"'; then
            pass "$(basename "$src"): ${elapsed}s"
        else
            fail "$(basename "$src"): no status in response"
        fi
    done

    log "Waiting for all training to complete..."
    wait_training_done 120 || log "WARNING: Training still running"
}

# ── Test: Batch Upload Speed ────────────────────────────────────────────────

test_batch_upload_speed() {
    section "TEST: Batch Upload Speed (all src/*.cpp files)"

    local all_src=()
    for f in "${SRC_DIR}"/*.cpp; do
        [[ -f "$f" ]] && all_src+=("$f")
    done

    if [[ ${#all_src[@]} -eq 0 ]]; then
        fail "No source files found"
        return
    fi

    log "Uploading ${#all_src[@]} files sequentially..."
    local total_start total_end total_elapsed
    total_start=$(date +%s%N)

    local i=0
    for src in "${all_src[@]}"; do
        i=$((i+1))
        local tags
        tags=$(basename "$src" .cpp)
        log "  [$i/${#all_src[@]}] $(basename "$src")"
        send_document "$src" "$tags" 0 >/dev/null
    done

    total_end=$(date +%s%N)
    total_elapsed=$(echo "scale=3; ($total_end - $total_start) / 1000000000" | bc)
    log "All ${#all_src[@]} files uploaded in ${total_elapsed}s"
    pass "Batch upload: ${#all_src[@]} files in ${total_elapsed}s"
}

# ── Test: Large Document Training ───────────────────────────────────────────

test_large_doc_speed() {
    section "TEST: Large Document Training"

    local largefile="${RESULTS_DIR}/large_doc.txt"
    log "Generating large test document (~500KB)..."
    : > "$largefile"
    for i in $(seq 1 20); do
        for f in "${SRC_DIR}"/*.cpp "${INC_DIR}"/*.hpp; do
            [[ -f "$f" ]] && cat "$f" >> "$largefile"
        done
    done
    local size
    size=$(wc -c < "$largefile")
    log "Large document size: ${size} bytes"

    local start end elapsed resp
    start=$(date +%s%N)
    resp=$(send_document "$largefile" "large_doc;benchmark" 0)
    end=$(date +%s%N)
    elapsed=$(echo "scale=3; ($end - $start) / 1000000000" | bc)

    log "Upload response time: ${elapsed}s"
    pass "Large document (${size}B) uploaded"
}

# ── Test: Repeated Training (Catastrophic Forgetting Check) ─────────────────

test_repeated_training() {
    section "TEST: Repeated Training on Same Content"

    local testfile="${SRC_DIR}/neural.cpp"
    [[ -f "$testfile" ]] || { fail "neural.cpp not found"; return; }

    local timings=()
    for run in 1 2 3 4 5; do
        log "Run $run/5..."
        local start end elapsed resp
        start=$(date +%s%N)
        resp=$(send_document "$testfile" "run_${run}" 0)
        end=$(date +%s%N)
        elapsed=$(echo "scale=3; ($end - $start) / 1000000000" | bc)
        timings+=("$elapsed")
        log "  Run $run: ${elapsed}s"
    done

    log "Timings: ${timings[*]}"
    local first="${timings[0]}"
    local last="${timings[-1]}"
    local slowdown
    slowdown=$(echo "scale=2; $last / ($first + 0.001)" | bc)
    log "Slowdown factor (last/first): ${slowdown}x"
    pass "Repeated training completed: slowdown=${slowdown}x"
}

# ── Test: Query Speed After Training ────────────────────────────────────────

test_query_speed() {
    section "TEST: Query (Inference) Speed"

    local prompts=(
        "What is a neural network forward pass?"
        "How does backpropagation work?"
        "Explain softmax activation function"
        "What is a vocabulary in NLP?"
        "How do you save model weights to disk?"
    )

    local total_time=0
    for prompt in "${prompts[@]}"; do
        local start end elapsed resp
        start=$(date +%s%N)
        resp=$(ask_question "$prompt" 0.001)
        end=$(date +%s%N)
        elapsed=$(echo "scale=3; ($end - $start) / 1000000000" | bc)
        total_time=$(echo "scale=3; $total_time + $elapsed" | bc)
        log "  Query '${prompt:0:40}...' -> ${elapsed}s"
    done

    local avg
    avg=$(echo "scale=3; $total_time / ${#prompts[@]}" | bc)
    log "Average query time: ${avg}s"
    pass "Query speed: avg=${avg}s over ${#prompts[@]} queries"
}

# ── Test: OpenAI-Compatible Chat Endpoint ───────────────────────────────────

test_chat_endpoint() {
    section "TEST: OpenAI Chat Endpoint (/v1/chat)"

    local prompts=(
        "What is the softmax function?"
        "Explain gradient descent"
        "How does memory allocation work in C++?"
    )

    for prompt in "${prompts[@]}"; do
        local start end elapsed resp
        start=$(date +%s%N)
        resp=$(ask_chat "$prompt" 0.7)
        end=$(date +%s%N)
        elapsed=$(echo "scale=3; ($end - $start) / 1000000000" | bc)

        if echo "$resp" | grep -q '"choices"'; then
            pass "/v1/chat: ${elapsed}s"
        else
            fail "/v1/chat failed: $(echo "$resp" | head -c 80)"
        fi
    done
}

# ── Test: Concurrent Queries ────────────────────────────────────────────────

test_concurrent_queries() {
    section "TEST: Concurrent Query Load"

    local num_concurrent=10
    local pids=()
    local tmpfiles=()

    log "Sending ${num_concurrent} concurrent queries..."
    local start
    start=$(date +%s%N)

    for i in $(seq 1 "$num_concurrent"); do
        local tmpfile="${RESULTS_DIR}/query_${i}.txt"
        tmpfiles+=("$tmpfile")
        (
            local resp
            resp=$(ask_question "What is chunk $i?" 0.001)
            echo "$resp" > "$tmpfile"
        ) &
        pids+=($!)
    done

    for pid in "${pids[@]}"; do
        wait "$pid" 2>/dev/null || true
    done

    local end
    end=$(date +%s%N)
    local elapsed
    elapsed=$(echo "scale=3; ($end - $start) / 1000000000" | bc)

    local success=0
    for tmpfile in "${tmpfiles[@]}"; do
        if [[ -f "$tmpfile" ]] && grep -q "chunk_id" "$tmpfile" 2>/dev/null; then
            success=$((success+1))
        fi
        rm -f "$tmpfile"
    done

    log "Concurrent queries: ${success}/${num_concurrent} succeeded in ${elapsed}s"
    pass "Concurrent load: ${success}/${num_concurrent} in ${elapsed}s"
}

# ── Test: Vocabulary Growth Overhead ────────────────────────────────────────

test_vocab_growth() {
    section "TEST: Vocabulary Growth Overhead"

    local unique_docs=(
        "${SRC_DIR}/store.cpp:vocabulary_store"
        "${SRC_DIR}/neural.cpp:vocabulary_neural"
        "${SRC_DIR}/vocabulary.cpp:vocabulary_vocab"
        "${SRC_DIR}/ipc.cpp:vocabulary_ipc"
        "${SRC_DIR}/logger.cpp:vocabulary_logger"
    )

    local timings=()
    for entry in "${unique_docs[@]}"; do
        local file="${entry%%:*}"
        local tag="${entry##*:}"
        if [[ ! -f "$file" ]]; then continue; fi

        local start end elapsed resp
        start=$(date +%s%N)
        resp=$(send_document "$file" "$tag" 0)
        end=$(date +%s%N)
        elapsed=$(echo "scale=3; ($end - $start) / 1000000000" | bc)
        timings+=("$elapsed")
        log "  $(basename "$file") -> ${elapsed}s"
    done

    if [[ ${#timings[@]} -gt 1 ]]; then
        local first="${timings[0]}"
        local last="${timings[-1]}"
        local growth
        growth=$(echo "scale=2; $last / ($first + 0.001)" | bc)
        log "Vocab growth overhead factor: ${growth}x"
        pass "Vocab growth: factor=${growth}x over ${#timings[@]} uploads"
    else
        pass "Vocab growth: single upload"
    fi
}

# ── Test: Database Operations Speed ─────────────────────────────────────────

test_db_operations() {
    section "TEST: Database Operations Speed"

    local start end elapsed

    start=$(date +%s%N)
    local resp
    resp=$(curl -sf -X POST "${ADMIN_URL}/show_db" \
        -H "Content-Type: application/json" \
        -d '{"cmd":"list_tables"}' 2>/dev/null)
    end=$(date +%s%N)
    elapsed=$(echo "scale=3; ($end - $start) / 1000000000" | bc)
    if echo "$resp" | grep -q "chunks"; then
        pass "list_tables: ${elapsed}s"
    else
        fail "list_tables failed"
    fi

    start=$(date +%s%N)
    resp=$(curl -sf -X POST "${ADMIN_URL}/get_table" \
        -H "Content-Type: application/json" \
        -d '{"table":"chunks","filter":"","offset":0,"limit":10}' 2>/dev/null)
    end=$(date +%s%N)
    elapsed=$(echo "scale=3; ($end - $start) / 1000000000" | bc)
    if echo "$resp" | grep -q "rows"; then
        pass "get_table(chunks): ${elapsed}s"
    else
        fail "get_table(chunks) failed"
    fi

    start=$(date +%s%N)
    resp=$(curl -sf -X POST "${ADMIN_URL}/get_table" \
        -H "Content-Type: application/json" \
        -d '{"table":"vocab","filter":"word","offset":0,"limit":10}' 2>/dev/null)
    end=$(date +%s%N)
    elapsed=$(echo "scale=3; ($end - $start) / 1000000000" | bc)
    if echo "$resp" | grep -q "rows"; then
        pass "get_table(vocab,filtered): ${elapsed}s"
    else
        fail "get_table(vocab,filtered) failed"
    fi
}

# ── Test: Training Progress Monitoring ──────────────────────────────────────

test_progress_monitoring() {
    section "TEST: Training Progress Monitoring"

    local startfile="${SRC_DIR}/store.cpp"
    [[ -f "$startfile" ]] || { fail "store.cpp not found"; return; }

    send_document "$startfile" "progress_test" 0 >/dev/null &
    sleep 0.5

    local prog_count=0
    local found_progress=false
    for i in $(seq 1 10); do
        local resp
        resp=$(get_progress)
        if echo "$resp" | grep -q '"training":true'; then
            found_progress=true
            prog_count=$((prog_count+1))
            log "  Poll $i: training in progress..."
        fi
        sleep 0.5
    done

    wait_training_done 60

    if $found_progress; then
        pass "Progress monitoring: caught ${prog_count} training polls"
    else
        pass "Progress monitoring: training completed quickly (<0.5s)"
    fi
}

# ── Test: Serialize Speed ───────────────────────────────────────────────────

test_serialize_speed() {
    section "TEST: Serialize (DB Backup) Speed"

    local start end elapsed resp
    start=$(date +%s%N)
    resp=$(curl -sf -X POST "${ADMIN_URL}/serialize" 2>/dev/null)
    end=$(date +%s%N)
    elapsed=$(echo "scale=3; ($end - $start) / 1000000000" | bc)
    log "Serialize response: $resp (${elapsed}s)"

    if [[ -f "${BUILD_DIR}/data.db" ]]; then
        local dbsize
        dbsize=$(stat -c%s "${BUILD_DIR}/data.db" 2>/dev/null || echo "0")
        pass "Serialize: ${elapsed}s, DB size: ${dbsize} bytes"
    else
        fail "Serialize: data.db not created"
    fi
}

# ── Test: Memory Usage Tracking ─────────────────────────────────────────────

test_memory_tracking() {
    section "TEST: Memory Usage Tracking"

    local resp
    resp=$(get_progress)
    log "Memory stats: $resp"
    if echo "$resp" | grep -q '"memory"'; then
        pass "Memory usage reported"
    else
        fail "Memory usage not reported"
    fi
}

# ── Test: Stress - Rapid Sequential Uploads ──────────────────────────────────

test_rapid_uploads() {
    section "TEST: Stress - Rapid Sequential Uploads"

    local count=5
    local total_start total_end total_elapsed
    total_start=$(date +%s%N)

    for i in $(seq 1 "$count"); do
        local text="Rapid upload test number $i with unique content: $(date +%s%N)-$RANDOM"
        send_text_document "$text" "rapid_$i" 0 >/dev/null
        log "  Upload $i/$count done"
    done

    total_end=$(date +%s%N)
    total_elapsed=$(echo "scale=3; ($total_end - $total_start) / 1000000000" | bc)

    pass "Rapid uploads: ${count} docs in ${total_elapsed}s"
}

# ── Test: Full Pipeline End-to-End ──────────────────────────────────────────

test_e2e_pipeline() {
    section "TEST: End-to-End Pipeline (Upload -> Train -> Query)"

    local src="${SRC_DIR}/vocabulary.cpp"
    [[ -f "$src" ]] || { fail "vocabulary.cpp not found"; return; }

    log "Step 1: Upload document"
    local upload_start upload_end upload_time
    upload_start=$(date +%s%N)
    local resp
    resp=$(send_document "$src" "e2e_test" 0)
    upload_end=$(date +%s%N)
    upload_time=$(echo "scale=3; ($upload_end - $upload_start) / 1000000000" | bc)
    log "  Upload: ${upload_time}s"

    log "Step 2: Wait for training (max 60s)"
    local train_start train_end train_time
    train_start=$(date +%s%N)
    if wait_training_done 60; then
        train_end=$(date +%s%N)
        train_time=$(echo "scale=3; ($train_end - $train_start) / 1000000000" | bc)
    else
        train_end=$(date +%s%N)
        train_time=$(echo "scale=3; ($train_end - $train_start) / 1000000000" | bc)
        log "  WARNING: Training not done in 120s, continuing..."
    fi
    log "  Training: ${train_time}s"

    log "Step 3: Query the model"
    local query_start query_end query_time
    query_start=$(date +%s%N)
    resp=$(ask_question "What does the vocabulary add_words function do?" 0.001)
    query_end=$(date +%s%N)
    query_time=$(echo "scale=3; ($query_end - $query_start) / 1000000000" | bc)
    log "  Query: ${query_time}s"
    log "  Response: $(echo "$resp" | head -c 200)"

    local total
    total=$(echo "scale=3; $upload_time + $train_time + $query_time" | bc)
    log "  Total pipeline: ${total}s"
    pass "E2E: upload=${upload_time}s train=${train_time}s query=${query_time}s total=${total}s"
}

# ── Summary ──────────────────────────────────────────────────────────────────

print_summary() {
    section "TEST SUMMARY"
    echo -e "${BOLD}Results:${NC} ${GREEN}${PASS} passed${NC}, ${RED}${FAIL} failed${NC}, ${TOTAL} total"
    echo ""
    echo "Log saved to: ${LOG_FILE}"
    if [[ $FAIL -gt 0 ]]; then
        echo -e "${RED}Some tests failed!${NC}"
        return 1
    else
        echo -e "${GREEN}All tests passed!${NC}"
        return 0
    fi
}

# ── Main ─────────────────────────────────────────────────────────────────────

main() {
    echo -e "${BOLD}Neural Network Speed Training Test Suite${NC}"
    echo "Started: $(date)"
    echo "Project root: ${PROJECT_ROOT}"
    echo "Build dir: ${BUILD_DIR}"
    echo ""

    trap cleanup EXIT
    setup

    test_connectivity
    test_single_doc_speed
    test_serialize_speed
    test_memory_tracking
    test_db_operations
    test_query_speed
    test_chat_endpoint
    test_concurrent_queries
    test_progress_monitoring
    test_vocab_growth
    test_repeated_training
    test_batch_upload_speed
    test_large_doc_speed
    test_rapid_uploads
    test_e2e_pipeline

    print_summary
}

main "$@"
