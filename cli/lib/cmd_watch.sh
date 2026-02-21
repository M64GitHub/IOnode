#!/usr/bin/env bash
# IOnode CLI — watch command (live heartbeat + event monitoring)

cmd_watch() {
    local mode="all"  # all, heartbeats, events
    local tag_filter=""

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --heartbeats) mode="heartbeats"; shift ;;
            --events)     mode="events"; shift ;;
            --tag)        tag_filter="$2"; shift 2 ;;
            *) shift ;;
        esac
    done

    if ! has_jq; then
        err "jq is required for watch. Install: apt install jq"
        exit 1
    fi

    # Print header
    header "Live Monitor"
    printf '  %sMode:%s %s' "$(c_label)" "$(_rst)" "$(c_text)"
    case "$mode" in
        all)        printf 'heartbeats + events' ;;
        heartbeats) printf 'heartbeats only' ;;
        events)     printf 'events only' ;;
    esac
    printf '%s\n' "$(_rst)"

    if [[ -n "$tag_filter" ]]; then
        printf '  %sFilter:%s %s#%s%s\n' \
            "$(c_label)" "$(_rst)" "$(c_accent)" "$tag_filter" "$(_rst)"
    fi

    printf '  %sServer:%s %s%s%s\n' \
        "$(c_label)" "$(_rst)" "$(c_dim)" "$(get_nats_url)" "$(_rst)"
    printf '  %sPress Ctrl+C to stop%s\n' "$(c_muted)" "$(_rst)"
    hr 60
    printf '\n'

    # Create temp directory for FIFOs
    local tmpdir
    tmpdir=$(mktemp -d "/tmp/ionode-watch.XXXXXX")
    local fifo_hb="${tmpdir}/heartbeats"
    local fifo_ev="${tmpdir}/events"

    # Cleanup on exit
    trap '_watch_cleanup' EXIT INT TERM

    _WATCH_PIDS=()
    _WATCH_TMPDIR="$tmpdir"

    # Start subscriptions based on mode
    if [[ "$mode" != "events" ]]; then
        mkfifo "$fifo_hb"
        nats sub "_ion.heartbeat" \
            --server="$(get_nats_url)" \
            --raw 2>/dev/null > "$fifo_hb" &
        _WATCH_PIDS+=($!)
    fi

    if [[ "$mode" != "heartbeats" ]]; then
        mkfifo "$fifo_ev"
        nats sub "*.events.>" \
            --server="$(get_nats_url)" \
            --raw 2>/dev/null > "$fifo_ev" &
        _WATCH_PIDS+=($!)
    fi

    # Process streams
    if [[ "$mode" == "heartbeats" ]]; then
        _watch_heartbeats "$fifo_hb" "$tag_filter"
    elif [[ "$mode" == "events" ]]; then
        _watch_events "$fifo_ev" "$tag_filter"
    else
        # Merge both streams
        _watch_merged "$fifo_hb" "$fifo_ev" "$tag_filter"
    fi
}

_WATCH_PIDS=()
_WATCH_TMPDIR=""

_watch_cleanup() {
    for pid in "${_WATCH_PIDS[@]}"; do
        kill "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
    done
    if [[ -n "$_WATCH_TMPDIR" ]] && [[ -d "$_WATCH_TMPDIR" ]]; then
        rm -rf "$_WATCH_TMPDIR"
    fi
}

_watch_heartbeats() {
    local fifo="$1" tag_filter="$2"

    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        [[ "$line" != "{"* ]] && continue

        # Tag filter
        if [[ -n "$tag_filter" ]]; then
            local tag
            tag=$(json_str "$line" "tag")
            [[ "$tag" != "$tag_filter" ]] && continue
        fi

        _format_heartbeat "$line"
    done < "$fifo"
}

_watch_events() {
    local fifo="$1" tag_filter="$2"

    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        [[ "$line" != "{"* ]] && continue

        # Tag filter (events don't have tag field, but device name might suffice)
        _format_event "$line"
    done < "$fifo"
}

_watch_merged() {
    local fifo_hb="$1" fifo_ev="$2" tag_filter="$3"

    # Read from both FIFOs concurrently using background readers
    (
        while IFS= read -r line; do
            [[ -z "$line" ]] && continue
            [[ "$line" != "{"* ]] && continue
            if [[ -n "$tag_filter" ]]; then
                local tag
                tag=$(json_str "$line" "tag")
                [[ "$tag" != "$tag_filter" ]] && continue
            fi
            _format_heartbeat "$line"
        done < "$fifo_hb"
    ) &
    _WATCH_PIDS+=($!)

    (
        while IFS= read -r line; do
            [[ -z "$line" ]] && continue
            [[ "$line" != "{"* ]] && continue
            _format_event "$line"
        done < "$fifo_ev"
    ) &
    _WATCH_PIDS+=($!)

    # Wait for either to finish (usually Ctrl+C)
    wait
}

_format_heartbeat() {
    local json="$1"

    local device tag uptime heap rssi sensors actuators events_fired version
    device=$(json_str "$json" "device")
    tag=$(json_str "$json" "tag")
    uptime=$(json_num "$json" "uptime")
    heap=$(json_num "$json" "heap")
    rssi=$(echo "$json" | jq -r '.rssi // empty' 2>/dev/null)
    sensors=$(json_num "$json" "sensors")
    actuators=$(json_num "$json" "actuators")
    events_fired=$(json_num "$json" "events_fired")
    version=$(json_str "$json" "version")

    local up_str heap_str
    up_str=$(fmt_uptime "$uptime")
    heap_str=$(fmt_heap "$heap")

    local ts
    ts=$(date '+%H:%M:%S')

    # Compact one-line heartbeat display
    printf '  %s%s%s  %s♥%s  %s%-14s%s' \
        "$(c_muted)" "$ts" "$(_rst)" \
        "$(c_green)" "$(_rst)" \
        "$(c_device)" "$device" "$(_rst)"

    if [[ -n "$tag" ]]; then
        printf '  %s#%s%s' "$(c_dim)" "$tag" "$(_rst)"
    fi

    printf '  %s↑%s%s' "$(c_dim)" "$up_str" "$(_rst)"
    printf '  %s⬡%s%s' "$(c_dim)" "$heap_str" "$(_rst)"

    if [[ -n "$rssi" ]] && [[ "$rssi" != "null" ]]; then
        printf '  %s' "$(signal_bar "$rssi")"
    fi

    if [[ "$events_fired" -gt 0 ]]; then
        printf '  %s⚡%s%s' "$(c_accent)" "$events_fired" "$(_rst)"
    fi

    printf '\n'
}

_format_event() {
    local json="$1"

    local event_type device sensor value threshold direction unit
    event_type=$(json_str "$json" "event")
    device=$(json_str "$json" "device")
    sensor=$(json_str "$json" "sensor")
    value=$(echo "$json" | jq -r '.value // empty' 2>/dev/null)
    threshold=$(echo "$json" | jq -r '.threshold // empty' 2>/dev/null)
    direction=$(json_str "$json" "direction")
    unit=$(json_str "$json" "unit")

    local ts
    ts=$(date '+%H:%M:%S')

    local dir_symbol
    [[ "$direction" == "above" ]] && dir_symbol="▲" || dir_symbol="▼"

    if [[ "$event_type" == "threshold" ]]; then
        printf '  %s%s%s  %s⚡ EVENT%s  %s%s%s  %s%s%s  %s%s %s → %s%s%s\n' \
            "$(c_muted)" "$ts" "$(_rst)" \
            "$(c_accent)$(c_bold)" "$(_rst)" \
            "$(c_device)" "$device" "$(_rst)" \
            "$(c_sensor)" "$sensor" "$(_rst)" \
            "$(c_text)" "$dir_symbol" "$threshold" "$value" \
            "$(c_unit)" "${unit}" "$(_rst)"
    elif [[ "$event_type" == "online" ]]; then
        local ip version
        ip=$(json_str "$json" "ip")
        version=$(json_str "$json" "version")
        printf '  %s%s%s  %s● ONLINE%s  %s%s%s  %sv%s  %s%s%s\n' \
            "$(c_muted)" "$ts" "$(_rst)" \
            "$(c_green)$(c_bold)" "$(_rst)" \
            "$(c_device)" "$device" "$(_rst)" \
            "$(c_dim)" "$version" "$(_rst)" \
            "$(c_dim)" "$ip" "$(_rst)"
    else
        # Unknown event type — just print raw
        printf '  %s%s%s  %s⚡%s  %s%s%s\n' \
            "$(c_muted)" "$ts" "$(_rst)" \
            "$(c_accent)" "$(_rst)" \
            "$(c_text)" "$json" "$(_rst)"
    fi
}
