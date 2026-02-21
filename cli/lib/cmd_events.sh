#!/usr/bin/env bash
# IOnode CLI — Event commands (event set/clear/list)

cmd_event() {
    if [[ -z "${1:-}" ]]; then
        err "missing subcommand"
        printf '  %susage: ionode event set|clear|list ...%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local subcmd="$1"
    shift

    case "$subcmd" in
        set)   cmd_event_set "$@" ;;
        clear) cmd_event_clear "$@" ;;
        list)  cmd_event_list "$@" ;;
        *)
            err "event subcommand must be 'set', 'clear', or 'list'"
            printf '  %susage: ionode event set|clear|list ...%s\n\n' "$(c_dim)" "$(_rst)"
            return 1
            ;;
    esac
}

cmd_event_set() {
    if [[ -z "${1:-}" ]] || [[ -z "${2:-}" ]]; then
        err "missing arguments"
        printf '  %susage: ionode event set <device> <sensor> --above|--below <value> [--cooldown <s>]%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"
    local sensor="$2"
    shift 2

    local threshold="" direction="" cooldown="10"
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --above)
                if [[ $# -lt 2 ]]; then err "--above requires a value"; return 1; fi
                direction="above"; threshold="$2"; shift 2 ;;
            --below)
                if [[ $# -lt 2 ]]; then err "--below requires a value"; return 1; fi
                direction="below"; threshold="$2"; shift 2 ;;
            --cooldown)
                if [[ $# -lt 2 ]]; then err "--cooldown requires a value"; return 1; fi
                cooldown="$2"; shift 2 ;;
            -*) err "unknown option: $1"; return 1 ;;
            *)  err "unexpected argument: $1"; return 1 ;;
        esac
    done

    if [[ -z "$direction" ]] || [[ -z "$threshold" ]]; then
        err "event set requires --above <value> or --below <value>"
        printf '  %susage: ionode event set <device> <sensor> --above|--below <value> [--cooldown <s>]%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi

    local payload
    payload=$(printf '{"n":"%s","t":%s,"d":"%s","cd":%s}' \
        "$sensor" "$threshold" "$direction" "$cooldown")

    local result
    if ! result=$(nats_req "${device}.config.event.set" "$payload" "3s") || [[ -z "$result" ]]; then
        timeout_msg "$device"
        return 1
    fi

    if has_jq && [[ "$result" == "{"* ]]; then
        local ok
        ok=$(echo "$result" | jq -r '.ok // empty' 2>/dev/null)
        if [[ "$ok" == "true" ]]; then
            local dir_symbol
            [[ "$direction" == "above" ]] && dir_symbol="▲" || dir_symbol="▼"

            printf '  %s⚡%s  %s%s%s  %s%s %s%s  %scd=%ss%s\n' \
                "$(c_accent)" "$(_rst)" \
                "$(c_sensor)" "$sensor" "$(_rst)" \
                "$(c_text)" "$dir_symbol" "$threshold" "$(_rst)" \
                "$(c_dim)" "$cooldown" "$(_rst)"
        else
            local error detail
            error=$(json_str "$result" "error")
            detail=$(json_str "$result" "detail")
            printf '  %s✗  %s%s%s\n' "$(c_err)" "${error:-failed}" \
                "${detail:+ — $detail}" "$(_rst)"
            return 1
        fi
    fi
}

cmd_event_clear() {
    if [[ -z "${1:-}" ]] || [[ -z "${2:-}" ]]; then
        err "missing arguments"
        printf '  %susage: ionode event clear <device> <sensor>%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"
    local sensor="$2"

    local payload
    payload=$(printf '{"n":"%s"}' "$sensor")

    local result
    if ! result=$(nats_req "${device}.config.event.clear" "$payload" "3s") || [[ -z "$result" ]]; then
        timeout_msg "$device"
        return 1
    fi

    if has_jq && [[ "$result" == "{"* ]]; then
        local ok
        ok=$(echo "$result" | jq -r '.ok // empty' 2>/dev/null)
        if [[ "$ok" == "true" ]]; then
            printf '  %s−%s  %s%s%s  %sevent cleared%s\n' \
                "$(c_muted)" "$(_rst)" \
                "$(c_sensor)" "$sensor" "$(_rst)" \
                "$(c_dim)" "$(_rst)"
        else
            local error
            error=$(json_str "$result" "error")
            printf '  %s✗  %s%s\n' "$(c_err)" "${error:-failed}" "$(_rst)"
            return 1
        fi
    fi
}

cmd_event_list() {
    if [[ -z "${1:-}" ]]; then
        err "missing argument"
        printf '  %susage: ionode event list <device>%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"

    if ! has_jq; then
        err "jq is required for event list. Install: apt install jq"
        exit 1
    fi

    local result
    if ! result=$(nats_req "${device}.config.event.list" "" "3s") || [[ -z "$result" ]]; then
        timeout_msg "$device"
        return 1
    fi

    local n_events
    n_events=$(echo "$result" | jq 'length' 2>/dev/null || echo 0)

    header "Events  ·  ${device}"
    printf '\n'

    if [[ "$n_events" -eq 0 ]]; then
        printf '  %sNo events configured.%s\n\n' "$(c_dim)" "$(_rst)"
        return
    fi

    echo "$result" | jq -c '.[]' 2>/dev/null | while IFS= read -r ev; do
        local ename ethreshold edir ecooldown earmed
        ename=$(json_str "$ev" "name")
        ethreshold=$(echo "$ev" | jq -r '.threshold' 2>/dev/null)
        edir=$(json_str "$ev" "direction")
        ecooldown=$(json_num "$ev" "cooldown")
        earmed=$(echo "$ev" | jq -r '.armed' 2>/dev/null)

        local dir_symbol armed_indicator
        [[ "$edir" == "above" ]] && dir_symbol="▲" || dir_symbol="▼"

        if [[ "$earmed" == "true" ]]; then
            armed_indicator="$(c_green)● armed$(_rst)"
        else
            armed_indicator="$(c_accent)● fired$(_rst)"
        fi

        printf '  %s⚡%s  %s%-14s%s  %s%s %s%s  %scd=%ss%s  %s\n' \
            "$(c_accent)" "$(_rst)" \
            "$(c_sensor)" "$ename" "$(_rst)" \
            "$(c_text)" "$dir_symbol" "$ethreshold" "$(_rst)" \
            "$(c_dim)" "$ecooldown" "$(_rst)" \
            "$armed_indicator"
    done

    printf '\n  %s%d event(s)%s\n\n' "$(c_dim)" "$n_events" "$(_rst)"
}
