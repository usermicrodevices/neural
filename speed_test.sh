#!/bin/bash
# ============================================================================
# Vocabulary & Training Speed Analyzer
# Profile bottlenecks in the train/learn pipeline
# Uses project's own C++ sources as training data
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
RESULTS_DIR="${BUILD_DIR}/speed_results"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

log()  { echo -e "${CYAN}[$(date +%H:%M:%S)]${NC} $*"; }
hdr() { echo -e "\n${BOLD}${YELLOW}── $* ──${NC}"; }
die() { echo -e "${RED}FATAL: $*${NC}" >&2; exit 1; }

cleanup() {
    curl -sf -X POST "${ADMIN_URL}/stop" 2>/dev/null || true
    wait "${SERVER_PID:-}" 2>/dev/null || true
}

send_doc() {
    local file="$1" tags="${2:-}" serialize="${3:-0}"
    curl -sf -X POST "${ADMIN_URL}/" \
        -H "Content-Type: multipart/form-data; boundary=--b$(date +%s%N)" \
        -F "document@${file}" \
        -F "tags=${tags}" \
        -F "serialize=${serialize}" 2>/dev/null
}

send_text() {
    local text="$1" tags="${2:-}" serialize="${3:-0}"
    local tmp; tmp=$(mktemp)
    echo -n "$text" > "$tmp"
    send_doc "$tmp" "$tags" "$serialize"
    rm -f "$tmp"
}

wait_done() {
    local max="${1:-120}" el=0
    while true; do
        local r; r=$(curl -sf "${ADMIN_URL}/progress" 2>/dev/null || echo '{}')
        echo "$r" | grep -q '"training":false' && return 0
        sleep 0.5; el=$((el+1))
        [[ $el -ge $((max*2)) ]] && return 1
    done
}

time_ms() {
    local s=$(date +%s%N)
    eval "$@" >/dev/null 2>&1
    local e=$(date +%s%N)
    echo $(( (e - s) / 1000000 ))
}

# ── Setup ────────────────────────────────────────────────────────────────────

mkdir -p "$RESULTS_DIR"

[[ -f "${BUILD_DIR}/neural" ]] || (cd "$PROJECT_ROOT" && bash build.sh) 2>&1 | tail -3
[[ -f "${BUILD_DIR}/neural" ]] || die "Build failed"

rm -f "${BUILD_DIR}/data.db"
cd "$BUILD_DIR"
./neural &
SERVER_PID=$!
cd "$PROJECT_ROOT"
log "Server PID=$SERVER_PID"

# Wait for server
for i in $(seq 1 30); do
    curl -sf "${ADMIN_URL}/progress" >/dev/null 2>&1 && break
    sleep 0.5
done
log "Server ready"

# ── Phase 1: Vocabulary Build Speed ─────────────────────────────────────────

hdr "Phase 1: Vocabulary Build Speed (per-file)"

results=()
for f in "${SRC_DIR}"/*.cpp; do
    [[ -f "$f" ]] || continue
    local_name=$(basename "$f" .cpp)
    sz=$(stat -c%s "$f")

    ms=$(time_ms send_doc "$f" "vtest_$local_name" 0)
    wait_done 60

    results+=("${local_name}|${sz}|${ms}")
    printf "  %-20s %8s B  %7s ms\n" "$local_name" "$sz" "$ms"
done

echo ""
echo "File,Size_bytes,Train_Time_ms" > "${RESULTS_DIR}/vocab_speed.csv"
for r in "${results[@]}"; do
    echo "$r" | tr '|' ',' >> "${RESULTS_DIR}/vocab_speed.csv"
done
log "Saved: ${RESULTS_DIR}/vocab_speed.csv"

# ── Phase 2: Repeated Upload on Same Content (vocab growth impact) ───────────

hdr "Phase 2: Repeated Upload Overhead (same file, 5 rounds)"

test_file="${SRC_DIR}/neural.cpp"
[[ -f "$test_file" ]] || test_file="${SRC_DIR}/store.cpp"

round_times=()
for run in 1 2 3 4 5; do
    ms=$(time_ms send_doc "$test_file" "round_$run" 0)
    wait_done 60
    round_times+=("$ms")
    printf "  Round %d: %7s ms\n" "$run" "$ms"
done

first="${round_times[0]}"
last="${round_times[-1]}"
if [[ "$first" -gt 0 ]]; then
    ratio=$(echo "scale=2; $last / $first" | bc 2>/dev/null || echo "?")
else
    ratio="?"
fi
log "First round: ${first}ms, Last round: ${last}ms, Ratio: ${ratio}x"

echo "Round,Time_ms" > "${RESULTS_DIR}/repeat_speed.csv"
i=1
for t in "${round_times[@]}"; do
    echo "$i,$t" >> "${RESULTS_DIR}/repeat_speed.csv"
    ((i++))
done

# ── Phase 3: Incremental Vocab Growth ───────────────────────────────────────

hdr "Phase 3: Incremental Vocabulary Growth Impact"

files=(
    "${SRC_DIR}/store.cpp:vocabA"
    "${SRC_DIR}/neural.cpp:vocabB"
    "${SRC_DIR}/vocabulary.cpp:vocabC"
    "${SRC_DIR}/ipc.cpp:vocabD"
    "${SRC_DIR}/logger.cpp:vocabE"
)

growth_times=()
for entry in "${files[@]}"; do
    f="${entry%%:*}"
    tag="${entry##*:}"
    [[ -f "$f" ]] || continue
    ms=$(time_ms send_doc "$f" "$tag" 0)
    wait_done 60
    growth_times+=("$ms")
    printf "  %-25s %7s ms\n" "$(basename "$f") ($tag)" "$ms"
done

if [[ ${#growth_times[@]} -gt 1 ]]; then
    f0="${growth_times[0]}"
    fl="${growth_times[-1]}"
    if [[ "$f0" -gt 0 ]]; then
        gf=$(echo "scale=2; $fl / $f0" | bc 2>/dev/null || echo "?")
    else
        gf="?"
    fi
    log "Growth factor (last/first): ${gf}x"
fi

echo "File,Tag,Time_ms" > "${RESULTS_DIR}/growth_speed.csv"
i=0
for entry in "${files[@]}"; do
    f="${entry%%:*}"
    tag="${entry##*:}"
    echo "$(basename "$f"),$tag,${growth_times[$i]:-0}" >> "${RESULTS_DIR}/growth_speed.csv"
    ((i++))
done

# ── Phase 4: Query Latency Profile ──────────────────────────────────────────

hdr "Phase 4: Query Latency Profile"

prompts=(
    "neural network forward pass"
    "backpropagation gradient descent"
    "softmax activation function"
    "vocabulary tokenization NLP"
    "model weight serialization"
    "SQLite database operations"
    "IPC socket communication"
    "thread pool worker pattern"
)

query_times=()
for p in "${prompts[@]}"; do
    s=$(date +%s%N)
    curl -sf -X POST "${CLIENT_URL}/ask" \
        -d "prompt=$(python3 -c "import urllib.parse;print(urllib.parse.quote('$p'))")&threshold=0.001" \
        >/dev/null 2>&1
    e=$(date +%s%N)
    ms=$(( (e - s) / 1000000 ))
    query_times+=("$ms")
    printf "  %-45s %6s ms\n" "${p:0:45}" "$ms"
done

total=0; min=999999; max=0
for t in "${query_times[@]}"; do
    total=$((total + t))
    [[ $t -lt $min ]] && min=$t
    [[ $t -gt $max ]] && max=$t
done
avg=$((total / ${#query_times[@]}))
log "Query stats: min=${min}ms max=${max}ms avg=${avg}ms"

echo "Prompt,Time_ms" > "${RESULTS_DIR}/query_speed.csv"
i=0
for p in "${prompts[@]}"; do
    echo "\"$p\",${query_times[$i]}" >> "${RESULTS_DIR}/query_speed.csv"
    ((i++))
done

# ── Phase 5: Concurrent Query Throughput ────────────────────────────────────

hdr "Phase 5: Concurrent Query Throughput"

for concurrency in 1 5 10 20; do
    pids=()
    tmpfiles=()
    s=$(date +%s%N)

    for i in $(seq 1 "$concurrency"); do
        tmpf="${RESULTS_DIR}/cq_${concurrency}_${i}.txt"
        tmpfiles+=("$tmpf")
        (
            curl -sf -X POST "${CLIENT_URL}/ask" \
                -d "prompt=concurrent+test+${i}&threshold=0.001" \
                -o "$tmpf" 2>/dev/null
        ) &
        pids+=($!)
    done

    for pid in "${pids[@]}"; do wait "$pid" 2>/dev/null || true; done
    e=$(date +%s%N)
    elapsed_ms=$(( (e - s) / 1000000 ))

    ok=0
    for tf in "${tmpfiles[@]}"; do
        [[ -f "$tf" ]] && [[ -s "$tf" ]] && ((ok++))
        rm -f "$tf"
    done

    qps=0
    [[ $elapsed_ms -gt 0 ]] && qps=$(echo "scale=1; $concurrency * 1000 / $elapsed_ms" | bc 2>/dev/null || echo "?")
    printf "  concurrency=%-3d  success=%d/%d  time=%6dms  ~%s qps\n" \
        "$concurrency" "$ok" "$concurrency" "$elapsed_ms" "$qps"
done

# ── Phase 6: Memory Growth ──────────────────────────────────────────────────

hdr "Phase 6: Memory Usage After Each Phase"

mem_resp=$(curl -sf "${ADMIN_URL}/progress" 2>/dev/null || echo '{}')
log "Current memory report: $mem_resp"

# ── Summary ──────────────────────────────────────────────────────────────────

echo ""
echo -e "${BOLD}${GREEN}═══ SPEED ANALYSIS COMPLETE ═══${NC}"
echo ""
echo "Results saved to: ${RESULTS_DIR}/"
ls -la "${RESULTS_DIR}/"*.csv 2>/dev/null
echo ""
echo "Key findings from this run are in the CSV files above."
echo "Graph them with: gnuplot, matplotlib, or any spreadsheet tool."
