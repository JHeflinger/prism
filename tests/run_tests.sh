#!/bin/bash
# ------------------------------------------------------------------------------
# prism regression test suite
#
# Runs every tests/cases/*.test through the prism headless render executor,
# compares each render against its ground truth with `prism compare`, and appends
# one JSON entry per test to tests/results.json.
#
# usage: tests/run_tests.sh [options] [case ...]
#   case                 only run cases whose name contains this text (repeatable)
#   -b, --bin <path>     prism executable (default: $PRISM_BIN or build/bin.exe)
#   -r, --results <file> results file to append to (default: tests/results.json)
#   -m, --machine <name> machine label stored with results (default: $PRISM_TEST_MACHINE or hostname)
#   -n, --no-record      run and report, but don't write to the results file
#   -u, --update-truth   copy each render over its ground truth (doesn't record results)
#   -t, --timeout <s>    per-test timeout in seconds (default: 1800, needs `timeout`)
#   -h, --help           show this help
# ------------------------------------------------------------------------------

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT" || exit 1

BIN="${PRISM_BIN:-build/bin.exe}"
RESULTS="$SCRIPT_DIR/results.json"
MACHINE="${PRISM_TEST_MACHINE:-$(hostname 2>/dev/null || uname -n)}"
RECORD=1
UPDATE_TRUTH=0
TIMEOUT_S=1800
FILTERS=()

usage() { sed -n '2,19p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
    case "$1" in
        -b|--bin) BIN="$2"; shift 2 ;;
        -r|--results) RESULTS="$2"; shift 2 ;;
        -m|--machine) MACHINE="$2"; shift 2 ;;
        -n|--no-record) RECORD=0; shift ;;
        -u|--update-truth) UPDATE_TRUTH=1; RECORD=0; shift ;;
        -t|--timeout) TIMEOUT_S="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "Unknown option \"$1\""; usage; exit 2 ;;
        *) FILTERS+=("$1"); shift ;;
    esac
done

if [ -t 1 ]; then
    RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; CYAN=$'\033[36m'; DIM=$'\033[2m'; RESET=$'\033[0m'
else
    RED=""; GREEN=""; YELLOW=""; CYAN=""; DIM=""; RESET=""
fi

# ---------------------------------------------------------------- helpers ----

# absolute path for a repo-relative (or already absolute) path
abspath() {
    case "$1" in
        /*) printf '%s' "$1" ;;
        *) printf '%s/%s' "$ROOT" "$1" ;;
    esac
}

# escape a string for embedding inside a JSON string literal
json_str() {
    local s="$1"
    s="${s//\\/\\\\}"
    s="${s//\"/\\\"}"
    s="${s//$'\t'/\\t}"
    s="${s//$'\r'/}"
    s="${s//$'\n'/\\n}"
    printf '"%s"' "$s"
}

# a number, or null when empty / not numeric
json_num() {
    if [[ "$1" =~ ^-?[0-9]+(\.[0-9]+)?([eE][-+]?[0-9]+)?$ ]]; then printf '%s' "$1"; else printf 'null'; fi
}

json_bool() {
    case "$1" in true|TRUE|1|yes|on) printf 'true' ;; *) printf 'false' ;; esac
}

# milliseconds since epoch (falls back to whole seconds on systems without %N)
now_ms() {
    local ns
    ns="$(date +%s%N 2>/dev/null)"
    if [[ "$ns" =~ ^[0-9]+$ ]] && [ ${#ns} -gt 12 ]; then echo $((ns / 1000000)); else echo $(($(date +%s) * 1000)); fi
}

trim() {
    local s="$1"
    s="${s#"${s%%[![:space:]]*}"}"
    s="${s%"${s##*[![:space:]]}"}"
    printf '%s' "$s"
}

# extract a string field from a flat single-line json object
json_field_str() {
    sed -nE "s/.*\"$2\":\"((\\\\.|[^\"\\\\])*)\".*/\\1/p" <<< "$1" | sed -e 's/\\"/"/g' -e 's/\\\\/\\/g'
}
# extract a numeric field from a flat single-line json object
json_field_num() { sed -n "s/.*\"$2\":\\(-\\{0,1\\}[0-9.eE+-]*\\).*/\\1/p" <<< "$1"; }

# raise the test status, never lower it: pass < no_truth < fail < error
status_rank() { case "$1" in pass) echo 0 ;; no_truth) echo 1 ;; fail) echo 2 ;; *) echo 3 ;; esac; }
raise_status() { [ "$(status_rank "$1")" -gt "$(status_rank "$STATUS")" ] && STATUS="$1"; return 0; }

# float comparison helper: fcmp <a> <op> <b>  (op: lt, gt)
fcmp() { awk -v a="$1" -v b="$3" -v op="$2" 'BEGIN { exit !((op == "lt") ? (a < b) : (a > b)) }'; }

# append one JSON entry (single line) to the results array, keeping one entry per line
append_result() {
    local entry="$1" tmp
    tmp="$(mktemp "${RESULTS}.XXXXXX")" || return 1
    if [ ! -s "$RESULTS" ] || ! grep -q '[^[:space:]]' "$RESULTS" || ! grep -q '^[[:space:]]*\]' "$RESULTS"; then
        printf '[\n  %s\n]\n' "$entry" > "$tmp"
    else
        local last
        last="$(grep -n '^[[:space:]]*\][[:space:]]*$' "$RESULTS" | tail -n 1 | cut -d: -f1)"
        head -n $((last - 1)) "$RESULTS" | sed -e :a -e '/^[[:space:]]*$/{$d;N;ba' -e '}' > "$tmp"
        if grep -q '{' "$tmp"; then
            sed -i.bak '$ s/[[:space:]]*,\{0,1\}[[:space:]]*$/,/' "$tmp" && rm -f "$tmp.bak"
        fi
        printf '  %s\n]\n' "$entry" >> "$tmp"
    fi
    mv "$tmp" "$RESULTS"
}

# ----------------------------------------------------------- environment ----

if [ ! -x "$BIN" ]; then
    echo "${RED}prism executable not found at \"$BIN\"${RESET} - build it first with scripts/build.sh (or pass -b <path>)"
    exit 2
fi
BIN_ABS="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
# tiny runs prism from build/env, which holds the shaders/assets links
RUN_DIR="$ROOT/build/env"
[ -d "$RUN_DIR" ] || RUN_DIR="$(dirname "$BIN_ABS")"

TIMEOUT_CMD=()
if command -v timeout >/dev/null 2>&1; then TIMEOUT_CMD=(timeout "$TIMEOUT_S"); fi

# git state
COMMIT="unknown"; COMMIT_SHORT="unknown"; BRANCH="unknown"; COMMIT_DATE=""; COMMIT_MSG=""; DIRTY="false"; REMOTE=""
if git -C "$ROOT" rev-parse --git-dir >/dev/null 2>&1; then
    COMMIT="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
    COMMIT_SHORT="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    BRANCH="$(git -C "$ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
    COMMIT_DATE="$(git -C "$ROOT" show -s --format=%cI HEAD 2>/dev/null)"
    COMMIT_MSG="$(git -C "$ROOT" show -s --format=%s HEAD 2>/dev/null)"
    REMOTE="$(git -C "$ROOT" remote get-url origin 2>/dev/null)"
    if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no 2>/dev/null)" ]; then DIRTY="true"; fi
fi

# hardware / os
OS_NAME="$(uname -s)"
OS_VERSION="$(uname -r)"
CPU=""; CORES=""; RAM_GB=""; DRIVER=""
if [ -r /etc/os-release ]; then OS_NAME="$(. /etc/os-release && echo "${PRETTY_NAME:-$NAME}")"; fi
case "$(uname -s)" in
    Linux)
        CPU="$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2-)"
        CORES="$(nproc 2>/dev/null)"
        RAM_GB="$(awk '/MemTotal/ { printf "%.1f", $2 / 1048576 }' /proc/meminfo 2>/dev/null)"
        ;;
    Darwin)
        OS_NAME="macOS $(sw_vers -productVersion 2>/dev/null)"
        CPU="$(sysctl -n machdep.cpu.brand_string 2>/dev/null)"
        CORES="$(sysctl -n hw.ncpu 2>/dev/null)"
        RAM_GB="$(sysctl -n hw.memsize 2>/dev/null | awk '{ printf "%.1f", $1 / 1073741824 }')"
        ;;
esac
CPU="$(trim "$CPU")"
if command -v nvidia-smi >/dev/null 2>&1; then
    DRIVER="$(nvidia-smi --query-gpu=driver_version --format=csv,noheader 2>/dev/null | head -n 1)"
elif command -v vulkaninfo >/dev/null 2>&1; then
    DRIVER="$(vulkaninfo --summary 2>/dev/null | grep -m1 'driverInfo' | cut -d= -f2-)"
fi
DRIVER="$(trim "$DRIVER")"
# fallback gpu name in case a run fails before prism reports it
GPU_FALLBACK=""
if command -v lspci >/dev/null 2>&1; then
    GPU_FALLBACK="$(lspci 2>/dev/null | grep -m1 -Ei 'vga|3d|display' | cut -d: -f3-)"
    GPU_FALLBACK="$(trim "$GPU_FALLBACK")"
fi
GPU_SEEN=""

SESSION_TS="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
SESSION="$(date -u +%Y%m%dT%H%M%SZ)-$(printf '%s' "$MACHINE" | tr -c 'A-Za-z0-9_.-' '_')"
OUT_DIR="$SCRIPT_DIR/output/$SESSION"
mkdir -p "$OUT_DIR"

echo "${CYAN}prism test suite${RESET}"
echo "  session : $SESSION"
echo "  commit  : $COMMIT_SHORT ($BRANCH)$( [ "$DIRTY" = "true" ] && echo " ${YELLOW}+ uncommitted changes${RESET}")"
echo "  machine : $MACHINE - $OS_NAME, ${CPU:-unknown cpu}"
echo "  output  : ${OUT_DIR#"$ROOT/"}"
[ $RECORD -eq 0 ] && echo "  ${YELLOW}results will not be recorded${RESET}"
echo

# ------------------------------------------------------------------ cases ----

shopt -s nullglob
CASES=("$SCRIPT_DIR"/cases/*.test)
shopt -u nullglob
if [ ${#CASES[@]} -eq 0 ]; then
    echo "${YELLOW}No test cases found in tests/cases/${RESET}"
    exit 0
fi

PASSED=0; FAILED=0; ERRORED=0; SKIPPED=0
SUMMARY=()

for case_file in "${CASES[@]}"; do
    # defaults
    T_NAME="$(basename "$case_file" .test)"
    T_DESC=""; T_SCENE=""; T_TRUTH=""
    T_SAMPLES=64
    WARNINGS=()
    T_MIN_SSIM=""; T_MIN_PSNR=""; T_MAX_RMSE=""; T_MAX_TIME=""; T_ENABLED="true"; T_TAGS=""

    while IFS= read -r line || [ -n "$line" ]; do
        line="${line%%#*}"
        line="${line//$'\r'/}"
        [[ "$line" == *=* ]] || continue
        key="$(trim "${line%%=*}")"
        val="$(trim "${line#*=}")"
        case "$key" in
            name) T_NAME="$val" ;;
            description) T_DESC="$val" ;;
            scene) T_SCENE="$val" ;;
            truth|ground_truth) T_TRUTH="$val" ;;
            samples) T_SAMPLES="$val" ;;
            width|height|direct_lighting|direct_only)
                WARNINGS+=("\"$key\" is ignored - render settings come from the scene file") ;;
            min_ssim) T_MIN_SSIM="$val" ;;
            min_psnr) T_MIN_PSNR="$val" ;;
            max_rmse) T_MAX_RMSE="$val" ;;
            max_render_time) T_MAX_TIME="$val" ;;
            enabled) T_ENABLED="$val" ;;
            tags) T_TAGS="$val" ;;
            *) WARNINGS+=("unknown key \"$key\"") ;;
        esac
    done < "$case_file"

    # filters
    if [ ${#FILTERS[@]} -gt 0 ]; then
        match=0
        for f in ${FILTERS[@]+"${FILTERS[@]}"}; do [[ "$T_NAME" == *"$f"* ]] && match=1; done
        [ $match -eq 1 ] || continue
    fi
    if [ "$(json_bool "$T_ENABLED")" = "false" ]; then
        echo "${DIM}- $T_NAME (disabled)${RESET}"
        SKIPPED=$((SKIPPED + 1))
        continue
    fi

    echo "${CYAN}> $T_NAME${RESET} ${DIM}${T_SAMPLES} spp${RESET}"
    for w in ${WARNINGS[@]+"${WARNINGS[@]}"}; do echo "  ${YELLOW}warning:${RESET} $w (${case_file#"$ROOT/"})"; done

    SAFE_NAME="$(printf '%s' "$T_NAME" | tr -c 'A-Za-z0-9_.-' '_')"
    RENDER_OUT="$OUT_DIR/$SAFE_NAME.png"
    DIFF_OUT="$OUT_DIR/$SAFE_NAME.diff.png"
    LOG_OUT="$OUT_DIR/$SAFE_NAME.log"
    STATUS="pass"; REASONS=(); EXIT_CODE=0
    RENDER_JSON=""; SETTINGS_JSON=""; COMPARE_JSON=""; RENDER_S=""; WALL_S=""; RES=""

    if [ -z "$T_SCENE" ]; then
        STATUS="error"; REASONS+=("no scene specified")
    elif [ ! -f "$(abspath "$T_SCENE")" ]; then
        STATUS="error"; REASONS+=("scene file not found: $T_SCENE")
    else
        start_ms="$(now_ms)"
        (
            cd "$RUN_DIR" && ${TIMEOUT_CMD[@]+"${TIMEOUT_CMD[@]}"} "$BIN_ABS" render "$(abspath "$T_SCENE")" "$RENDER_OUT" "$T_SAMPLES"
        ) > "$LOG_OUT" 2>&1
        EXIT_CODE=$?
        end_ms="$(now_ms)"
        WALL_S="$(awk -v a="$start_ms" -v b="$end_ms" 'BEGIN { printf "%.3f", (b - a) / 1000.0 }')"
        RENDER_JSON="$(grep -m1 '^PRISM_RENDER_JSON ' "$LOG_OUT" | sed 's/^PRISM_RENDER_JSON //')"
        RENDER_S="$(json_field_num "$RENDER_JSON" render_seconds)"
        SETTINGS_JSON="$(grep -m1 '^PRISM_SETTINGS_JSON ' "$LOG_OUT" | sed 's/^PRISM_SETTINGS_JSON //')"
        if [ -n "$SETTINGS_JSON" ]; then RES="$(json_field_num "$SETTINGS_JSON" width)x$(json_field_num "$SETTINGS_JSON" height)"; fi
        g="$(json_field_str "$RENDER_JSON" gpu)"
        [ -n "$g" ] && GPU_SEEN="$g"

        if [ $EXIT_CODE -eq 124 ]; then
            STATUS="error"; REASONS+=("timed out after ${TIMEOUT_S}s")
        elif [ $EXIT_CODE -ne 0 ]; then
            STATUS="error"; REASONS+=("prism exited with code $EXIT_CODE")
        elif [ ! -f "$RENDER_OUT" ]; then
            STATUS="error"; REASONS+=("prism did not write a render")
        fi
        if [ "$STATUS" = "error" ]; then
            # surface the actual cause: executor errors, failed asserts, fatals
            while IFS= read -r cause; do
                cause="$(printf '%s' "$cause" | sed -e 's/\x1b\[[0-9;]*m//g' -e 's/^PRISM_EXECUTOR_ERROR //')"
                REASONS+=("$(trim "$cause")")
            done < <(grep -E '^PRISM_EXECUTOR_ERROR |\[(FAIL|FATAL|ERROR)\]' "$LOG_OUT" | head -n 3)
            [ -f "$RENDER_OUT" ] && REASONS+=("a render was written before the failure - metrics still recorded")
        fi
    fi

    if [ "$STATUS" != "error" ] && [ $UPDATE_TRUTH -eq 1 ]; then
        if [ -z "$T_TRUTH" ]; then
            echo "  ${YELLOW}no truth path set - nothing to update${RESET}"
        else
            mkdir -p "$(dirname "$(abspath "$T_TRUTH")")"
            cp "$RENDER_OUT" "$(abspath "$T_TRUTH")"
            echo "  ${GREEN}ground truth updated${RESET} -> $T_TRUTH"
        fi
    elif [ $UPDATE_TRUTH -eq 0 ] && [ -f "$RENDER_OUT" ]; then
        if [ -z "$T_TRUTH" ]; then
            raise_status no_truth; REASONS+=("no ground truth specified")
        elif [ ! -f "$(abspath "$T_TRUTH")" ]; then
            raise_status no_truth; REASONS+=("ground truth not found: $T_TRUTH (create it with -u)")
        else
            "$BIN_ABS" compare "$RENDER_OUT" "$(abspath "$T_TRUTH")" "$DIFF_OUT" >> "$LOG_OUT" 2>&1
            cmp_code=$?
            COMPARE_JSON="$(grep -m1 '^PRISM_COMPARE_JSON ' "$LOG_OUT" | sed 's/^PRISM_COMPARE_JSON //')"
            if [ $cmp_code -ne 0 ] || [ -z "$COMPARE_JSON" ] || [[ "$COMPARE_JSON" == *'"error"'* ]]; then
                raise_status error; REASONS+=("image comparison failed (code $cmp_code) - see log"); COMPARE_JSON=""
            else
                ssim="$(json_field_num "$COMPARE_JSON" ssim)"
                psnr="$(json_field_num "$COMPARE_JSON" psnr)"
                rmse="$(json_field_num "$COMPARE_JSON" rmse)"
                if [ -n "$T_MIN_SSIM" ] && fcmp "$ssim" lt "$T_MIN_SSIM"; then raise_status fail; REASONS+=("ssim $ssim < $T_MIN_SSIM"); fi
                if [ -n "$T_MIN_PSNR" ] && fcmp "$psnr" lt "$T_MIN_PSNR"; then raise_status fail; REASONS+=("psnr $psnr < $T_MIN_PSNR"); fi
                if [ -n "$T_MAX_RMSE" ] && fcmp "$rmse" gt "$T_MAX_RMSE"; then raise_status fail; REASONS+=("rmse $rmse > $T_MAX_RMSE"); fi
            fi
        fi
        if [ -n "$T_MAX_TIME" ] && [ -n "$RENDER_S" ] && fcmp "$RENDER_S" gt "$T_MAX_TIME"; then
            raise_status fail; REASONS+=("render time ${RENDER_S}s > ${T_MAX_TIME}s")
        fi
    fi

    case "$STATUS" in
        pass) PASSED=$((PASSED + 1)); colour="$GREEN" ;;
        fail) FAILED=$((FAILED + 1)); colour="$RED" ;;
        error) ERRORED=$((ERRORED + 1)); colour="$RED" ;;
        *) SKIPPED=$((SKIPPED + 1)); colour="$YELLOW" ;;
    esac
    detail=""
    [ -n "$RENDER_S" ] && detail="${RES:+$RES, }render ${RENDER_S}s"
    if [ -n "$COMPARE_JSON" ] && [[ "$COMPARE_JSON" != *'"error"'* ]]; then
        detail="$detail, ssim $(json_field_num "$COMPARE_JSON" ssim), psnr $(json_field_num "$COMPARE_JSON" psnr) dB"
    fi
    echo "  ${colour}${STATUS}${RESET} ${DIM}${detail}${RESET}"
    for r in ${REASONS[@]+"${REASONS[@]}"}; do echo "    - $r"; done
    if [ "$STATUS" = "error" ] && [ -s "$LOG_OUT" ]; then
        echo "    ${DIM}last lines of ${LOG_OUT#"$ROOT/"}:${RESET}"
        tail -n 8 "$LOG_OUT" | tr -d '\b' | sed -e 's/^/      | /'
    fi
    SUMMARY+=("$(printf '%-8s %s' "$STATUS" "$T_NAME")")

    [ $RECORD -eq 1 ] || continue

    reasons_json="["
    for i in ${REASONS[@]+"${!REASONS[@]}"}; do
        [ "$i" -gt 0 ] && reasons_json+=","
        reasons_json+="$(json_str "${REASONS[$i]}")"
    done
    reasons_json+="]"

    gpu_name="$(json_field_str "$RENDER_JSON" gpu)"
    [ -z "$gpu_name" ] && gpu_name="${GPU_SEEN:-$GPU_FALLBACK}"
    [ -z "$gpu_name" ] && gpu_name="unknown"

    entry="{"
    entry+="\"session\":$(json_str "$SESSION"),"
    entry+="\"timestamp\":$(json_str "$(date -u +%Y-%m-%dT%H:%M:%SZ)"),"
    entry+="\"session_start\":$(json_str "$SESSION_TS"),"
    entry+="\"test\":$(json_str "$T_NAME"),"
    entry+="\"description\":$(json_str "$T_DESC"),"
    entry+="\"tags\":$(json_str "$T_TAGS"),"
    entry+="\"status\":$(json_str "$STATUS"),"
    entry+="\"reasons\":$reasons_json,"
    entry+="\"exit_code\":$EXIT_CODE,"
    entry+="\"commit\":{\"hash\":$(json_str "$COMMIT"),\"short\":$(json_str "$COMMIT_SHORT"),\"branch\":$(json_str "$BRANCH"),\"date\":$(json_str "$COMMIT_DATE"),\"message\":$(json_str "$COMMIT_MSG"),\"dirty\":$DIRTY,\"remote\":$(json_str "$REMOTE")},"
    entry+="\"device\":{\"gpu\":$(json_str "$gpu_name"),\"driver\":$(json_str "$DRIVER"),\"cpu\":$(json_str "$CPU"),\"cores\":$(json_num "$CORES"),\"ram_gb\":$(json_num "$RAM_GB"),\"os\":$(json_str "$OS_NAME"),\"os_version\":$(json_str "$OS_VERSION"),\"machine\":$(json_str "$MACHINE")},"
    entry+="\"scene\":$(json_str "$T_SCENE"),"
    entry+="\"ground_truth\":$(json_str "$T_TRUTH"),"
    entry+="\"output\":$(json_str "$( [ -f "$RENDER_OUT" ] && echo "${RENDER_OUT#"$ROOT/"}")"),"
    entry+="\"diff\":$(json_str "$( [ -f "$DIFF_OUT" ] && echo "${DIFF_OUT#"$ROOT/"}")"),"
    entry+="\"resolution\":$(json_str "$RES"),"
    if [ -n "$SETTINGS_JSON" ]; then
        entry+="\"settings\":$SETTINGS_JSON,"
    else
        entry+="\"settings\":{\"samples\":$(json_num "$T_SAMPLES")},"
    fi
    entry+="\"thresholds\":{\"min_ssim\":$(json_num "$T_MIN_SSIM"),\"min_psnr\":$(json_num "$T_MIN_PSNR"),\"max_rmse\":$(json_num "$T_MAX_RMSE"),\"max_render_time\":$(json_num "$T_MAX_TIME")},"
    entry+="\"render_time\":$(json_num "$RENDER_S"),"
    entry+="\"wall_time\":$(json_num "$WALL_S"),"
    if [ -n "$COMPARE_JSON" ] && [[ "$COMPARE_JSON" != *'"error"'* ]]; then
        entry+="\"metrics\":$COMPARE_JSON"
    else
        entry+="\"metrics\":null"
    fi
    entry+="}"
    append_result "$entry" || echo "  ${RED}unable to write results to $RESULTS${RESET}"
done

# ---------------------------------------------------------------- summary ----

echo
echo "${CYAN}summary${RESET} ${DIM}($SESSION)${RESET}"
for s in ${SUMMARY[@]+"${SUMMARY[@]}"}; do echo "  $s"; done
echo
echo "  ${GREEN}$PASSED passed${RESET}, ${RED}$FAILED failed${RESET}, ${RED}$ERRORED errored${RESET}, ${YELLOW}$SKIPPED skipped / without ground truth${RESET}"
[ $RECORD -eq 1 ] && echo "  results appended to ${RESULTS#"$ROOT/"}"

[ $FAILED -eq 0 ] && [ $ERRORED -eq 0 ]
