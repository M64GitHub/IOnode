#!/usr/bin/env bash
# IOnode CLI — Output & Color Library
# Matches ionode.io website color scheme (true color 24-bit)

# --- Color Control ---
# Respect: --no-color flag, NO_COLOR env, non-TTY stdout
_IONODE_COLOR=true

if [[ -n "${NO_COLOR:-}" ]] || [[ ! -t 1 ]]; then
    _IONODE_COLOR=false
fi

# Called from main script if --no-color is passed
disable_color() { _IONODE_COLOR=false; }

# --- True Color Definitions (from ionode.io variables.css) ---
_c() {
    if [[ "$_IONODE_COLOR" == true ]]; then
        printf '\e[%sm' "$1"
    fi
}
_c_rgb() {
    if [[ "$_IONODE_COLOR" == true ]]; then
        printf '\e[38;2;%s;%s;%sm' "$1" "$2" "$3"
    fi
}
_c_bg_rgb() {
    if [[ "$_IONODE_COLOR" == true ]]; then
        printf '\e[48;2;%s;%s;%sm' "$1" "$2" "$3"
    fi
}

# Reset
RST="" ; _rst() { if [[ "$_IONODE_COLOR" == true ]]; then printf '\e[0m'; fi; }

# Core palette — website exact colors
c_accent()    { _c_rgb 255 140 0; }      # --accent: #ff8c00 (IOnode orange)
c_blue()      { _c_rgb 74 158 255; }     # --led-blue: #4a9eff
c_green()     { _c_rgb 0 212 170; }      # --led-green: #00d4aa
c_red()       { _c_rgb 255 71 87; }      # --led-red: #ff4757
c_purple()    { _c_rgb 176 106 255; }    # --led-purple: #b06aff
c_text()      { _c_rgb 232 234 240; }    # --text-primary: #e8eaf0
c_dim()       { _c_rgb 139 146 168; }    # --text-secondary: #8b92a8
c_muted()     { _c_rgb 74 80 104; }      # --text-muted: #4a5068
c_bold()      { _c 1; }
c_dim_attr()  { _c 2; }

# --- Semantic Colors ---
c_ok()        { c_green; }
c_err()       { c_red; }
c_warn()      { c_accent; }
c_info()      { c_blue; }
c_label()     { c_dim; }
c_value()     { c_text; }
c_highlight() { c_accent; }
c_device()    { c_accent; c_bold; }
c_sensor()    { c_blue; }
c_actuator()  { c_purple; }
c_unit()      { c_dim; }

# --- Formatted Output Helpers ---

# Print the IOnode brand prefix
brand() {
    printf '%s[IOnode]%s' "$(c_accent)$(c_bold)" "$(_rst)"
}

# Print a section header
header() {
    printf '\n%s%s %s%s\n' "$(c_accent)" "━━" "$1" "$(_rst)"
}

# Print a key-value pair with alignment
kv() {
    local key="$1" val="$2" width="${3:-16}"
    printf '  %s%-*s%s %s%s%s\n' "$(c_label)" "$width" "$key" "$(_rst)" "$(c_value)" "$val" "$(_rst)"
}

# Print a key-value pair with colored value
kv_color() {
    local key="$1" val="$2" color_fn="$3" width="${4:-16}"
    printf '  %s%-*s%s %s%s%s\n' "$(c_label)" "$width" "$key" "$(_rst)" "$($color_fn)" "$val" "$(_rst)"
}

# Print a status line (ok/err/warn)
status_ok()   { printf '  %s●%s %s\n' "$(c_ok)" "$(_rst)" "$1"; }
status_err()  { printf '  %s●%s %s\n' "$(c_err)" "$(_rst)" "$1"; }
status_warn() { printf '  %s●%s %s\n' "$(c_warn)" "$(_rst)" "$1"; }

# Print an error message
err() { printf '%s%s error:%s %s\n' "$(c_err)" "$(c_bold)" "$(_rst)" "$1" >&2; }

# Print a timeout/unreachable message
timeout_msg() { printf '  %s⏱%s  %s%s — no response%s\n' "$(c_muted)" "$(_rst)" "$(c_dim)" "$1" "$(_rst)"; }

# Horizontal rule
hr() {
    local width="${1:-60}"
    printf '%s' "$(c_muted)"
    printf '%.0s─' $(seq 1 "$width")
    printf '%s\n' "$(_rst)"
}

# Table header row
table_header() {
    # args: col1_width col1_label col2_width col2_label ...
    local fmt=""
    printf '%s' "$(c_dim)$(c_bold)"
    while [[ $# -ge 2 ]]; do
        printf '  %-*s' "$1" "$2"
        shift 2
    done
    printf '%s\n' "$(_rst)"
}

# --- Argument Validation ---
# Usage: need_arg "$1" "<device>" "info"
#   Checks if $1 is set, prints clean error if not
need_args() {
    local cmd="$1"; shift
    local usage="$1"; shift
    for arg in "$@"; do
        if [[ -z "$arg" ]]; then
            err "missing arguments"
            printf '  %susage: ionode %s %s%s\n\n' "$(c_dim)" "$cmd" "$usage" "$(_rst)"
            exit 1
        fi
    done
}

# --- Spinner (for long operations) ---
_spinner_pid=""

spinner_start() {
    local msg="${1:-Working...}"
    if [[ "$_IONODE_COLOR" != true ]] || [[ ! -t 1 ]]; then
        return
    fi
    (
        local frames=('⠋' '⠙' '⠹' '⠸' '⠼' '⠴' '⠦' '⠧' '⠇' '⠏')
        local i=0
        while true; do
            printf '\r  %s%s%s %s' "$(c_accent)" "${frames[$i]}" "$(_rst)" "$msg"
            i=$(( (i + 1) % ${#frames[@]} ))
            sleep 0.08
        done
    ) &
    _spinner_pid=$!
    disown "$_spinner_pid" 2>/dev/null
}

spinner_stop() {
    if [[ -n "$_spinner_pid" ]]; then
        kill "$_spinner_pid" 2>/dev/null
        wait "$_spinner_pid" 2>/dev/null
        _spinner_pid=""
        printf '\r\e[K'  # clear the spinner line
    fi
}

# --- Signal Bar (WiFi RSSI visualization) ---
signal_bar() {
    local rssi="$1"
    local bars=""
    if [[ "$rssi" -ge -50 ]]; then
        bars="$(c_green)▂▄▆█$(_rst)"
    elif [[ "$rssi" -ge -60 ]]; then
        bars="$(c_green)▂▄▆$(c_muted)█$(_rst)"
    elif [[ "$rssi" -ge -70 ]]; then
        bars="$(c_accent)▂▄$(c_muted)▆█$(_rst)"
    elif [[ "$rssi" -ge -80 ]]; then
        bars="$(c_red)▂$(c_muted)▄▆█$(_rst)"
    else
        bars="$(c_red)▂$(c_muted)▄▆█$(_rst)"
    fi
    printf '%s' "$bars"
}

# --- Uptime formatting ---
fmt_uptime() {
    local secs="$1"
    local days=$(( secs / 86400 ))
    local hours=$(( (secs % 86400) / 3600 ))
    local mins=$(( (secs % 3600) / 60 ))
    if [[ "$days" -gt 0 ]]; then
        printf '%dd %dh %dm' "$days" "$hours" "$mins"
    elif [[ "$hours" -gt 0 ]]; then
        printf '%dh %dm' "$hours" "$mins"
    else
        printf '%dm %ds' "$mins" $(( secs % 60 ))
    fi
}

# --- Heap formatting ---
fmt_heap() {
    local bytes="$1"
    if [[ "$bytes" -ge 1048576 ]]; then
        printf '%.1f MB' "$(echo "scale=1; $bytes / 1048576" | bc)"
    elif [[ "$bytes" -ge 1024 ]]; then
        printf '%.0f KB' "$(echo "scale=0; $bytes / 1024" | bc)"
    else
        printf '%d B' "$bytes"
    fi
}

# --- Online/Offline indicator ---
indicator_online()  { printf '%s● online%s'  "$(c_green)" "$(_rst)"; }
indicator_offline() { printf '%s● offline%s' "$(c_red)"   "$(_rst)"; }
