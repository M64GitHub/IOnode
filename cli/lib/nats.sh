#!/usr/bin/env bash
# IOnode CLI — NATS Communication Layer

# --- Config Resolution ---
# Priority: --server flag > IONODE_NATS_URL env > config file > default

_IONODE_NATS_URL=""

resolve_nats_url() {
    # Already set by --server flag
    if [[ -n "$_IONODE_NATS_URL" ]]; then
        return
    fi

    # Environment variable
    if [[ -n "${IONODE_NATS_URL:-}" ]]; then
        _IONODE_NATS_URL="$IONODE_NATS_URL"
        return
    fi

    # XDG config file
    local config_file="${XDG_CONFIG_HOME:-$HOME/.config}/ionode/config"
    if [[ -f "$config_file" ]]; then
        local url
        url=$(grep -E '^NATS_URL=' "$config_file" 2>/dev/null | head -1 | cut -d'=' -f2-)
        if [[ -n "$url" ]]; then
            _IONODE_NATS_URL="$url"
            return
        fi
    fi

    # Default
    _IONODE_NATS_URL="nats://localhost:4222"
}

set_nats_url() {
    _IONODE_NATS_URL="$1"
}

get_nats_url() {
    if [[ -z "$_IONODE_NATS_URL" ]]; then
        resolve_nats_url
    fi
    echo "$_IONODE_NATS_URL"
}

# --- NATS Dependency Check ---
check_nats_cli() {
    if ! command -v nats &>/dev/null; then
        err "nats CLI not found. Install from: https://github.com/nats-io/natscli"
        exit 1
    fi
}

# --- Core NATS Operations ---

# Request/reply with timeout. Returns response on stdout, exit code on failure.
nats_req() {
    local subject="$1"
    local payload="${2:-}"
    local timeout="${3:-2s}"

    nats req "$subject" "$payload" \
        --server="$(get_nats_url)" \
        --timeout="$timeout" \
        --raw \
        2>/dev/null
}

# Request expecting multiple replies (discovery, group queries)
nats_req_multi() {
    local subject="$1"
    local payload="${2:-}"
    local timeout="${3:-2s}"

    nats req "$subject" "$payload" \
        --server="$(get_nats_url)" \
        --replies=0 \
        --timeout="$timeout" \
        --raw \
        2>/dev/null
}

# Subscribe (blocking, streams to stdout)
nats_sub() {
    local subject="$1"
    nats sub "$subject" \
        --server="$(get_nats_url)" \
        --raw \
        2>/dev/null
}

# --- JSON Helpers (minimal, no jq dependency for core ops) ---
# For complex parsing we use jq, but basic field extraction works without it

_HAS_JQ=""
has_jq() {
    if [[ -z "$_HAS_JQ" ]]; then
        if command -v jq &>/dev/null; then
            _HAS_JQ=true
        else
            _HAS_JQ=false
        fi
    fi
    [[ "$_HAS_JQ" == true ]]
}

# Extract a string field from JSON (requires jq)
json_str() {
    local json="$1" field="$2"
    echo "$json" | jq -r ".$field // empty" 2>/dev/null
}

# Extract a number field from JSON (requires jq)
json_num() {
    local json="$1" field="$2"
    echo "$json" | jq -r ".$field // 0" 2>/dev/null
}

# Extract array length
json_len() {
    local json="$1" field="$2"
    echo "$json" | jq -r ".$field | length" 2>/dev/null
}

# Pretty-print JSON
json_pretty() {
    if has_jq; then
        jq -r '.' 2>/dev/null
    else
        cat
    fi
}
