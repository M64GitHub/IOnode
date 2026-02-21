#!/usr/bin/env bash
# IOnode CLI — Configuration commands (config, tag, heartbeat, rename, device add/remove)

cmd_config() {
    if [[ -z "${1:-}" ]]; then
        err "missing argument"
        printf '  %susage: ionode config <device>%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"

    if ! has_jq; then
        err "jq is required for config. Install: apt install jq"
        exit 1
    fi

    local result
    if ! result=$(nats_req "${device}.config.get" "" "3s") || [[ -z "$result" ]]; then
        timeout_msg "$device"
        return 1
    fi

    header "Config  ·  ${device}"
    printf '\n'

    local cfg_name cfg_ssid cfg_nats cfg_port cfg_tz cfg_tag cfg_hb
    cfg_name=$(json_str "$result" "device_name")
    cfg_ssid=$(json_str "$result" "wifi_ssid")
    cfg_nats=$(json_str "$result" "nats_host")
    cfg_port=$(json_num "$result" "nats_port")
    cfg_tz=$(json_str "$result" "timezone")
    cfg_tag=$(json_str "$result" "tag")
    cfg_hb=$(json_num "$result" "heartbeat_interval")

    kv "Device name:" "$cfg_name"
    kv "WiFi SSID:" "$cfg_ssid"
    kv "NATS host:" "${cfg_nats}:${cfg_port}"
    kv "Timezone:" "$cfg_tz"
    kv "Tag:" "${cfg_tag:-—}"

    if [[ "$cfg_hb" -eq 0 ]]; then
        kv_color "Heartbeat:" "disabled" "c_muted"
    else
        kv "Heartbeat:" "every ${cfg_hb}s"
    fi

    printf '\n'
}

cmd_tag() {
    if [[ -z "${1:-}" ]]; then
        err "missing argument"
        printf '  %susage: ionode tag <device> [tag]%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"
    local new_tag="${2:-}"

    if [[ -z "$new_tag" ]]; then
        # Get current tag
        local result
        if ! result=$(nats_req "${device}.config.tag.get" "" "3s") || [[ -z "$result" ]]; then
            timeout_msg "$device"
            return 1
        fi

        local tag_val=""
        if has_jq && [[ "$result" == "{"* ]]; then
            tag_val=$(json_str "$result" "tag")
        else
            tag_val="$result"
        fi

        if [[ -z "$tag_val" ]]; then
            printf '  %s%s%s  %stag:%s %s(none)%s\n' \
                "$(c_device)" "$device" "$(_rst)" \
                "$(c_label)" "$(_rst)" \
                "$(c_muted)" "$(_rst)"
        else
            printf '  %s%s%s  %stag:%s %s#%s%s\n' \
                "$(c_device)" "$device" "$(_rst)" \
                "$(c_label)" "$(_rst)" \
                "$(c_accent)" "$tag_val" "$(_rst)"
        fi
    else
        # Set tag
        local result
        if ! result=$(nats_req "${device}.config.tag.set" "$new_tag" "3s") || [[ -z "$result" ]]; then
            timeout_msg "$device"
            return 1
        fi

        if has_jq && [[ "$result" == "{"* ]]; then
            local ok
            ok=$(echo "$result" | jq -r '.ok // empty' 2>/dev/null)
            if [[ "$ok" == "true" ]]; then
                printf '  %s%s%s  %stag → %s#%s%s\n' \
                    "$(c_device)" "$device" "$(_rst)" \
                    "$(c_ok)" "$(c_accent)" "$new_tag" "$(_rst)"
            else
                local error
                error=$(json_str "$result" "error")
                printf '  %s%s%s\n' "$(c_err)" "${error:-$result}" "$(_rst)"
                return 1
            fi
        else
            printf '  %s%s%s\n' "$(c_text)" "$result" "$(_rst)"
        fi
    fi
}

cmd_heartbeat() {
    if [[ -z "${1:-}" ]] || [[ -z "${2:-}" ]]; then
        err "missing arguments"
        printf '  %susage: ionode heartbeat <device> <seconds>%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"
    local seconds="$2"

    if ! [[ "$seconds" =~ ^[0-9]+$ ]] || [[ "$seconds" -lt 0 ]] || [[ "$seconds" -gt 3600 ]]; then
        err "heartbeat interval must be 0-3600 seconds (0=disabled)"
        return 1
    fi

    local result
    if ! result=$(nats_req "${device}.config.heartbeat.set" "$seconds" "3s") || [[ -z "$result" ]]; then
        timeout_msg "$device"
        return 1
    fi

    if has_jq && [[ "$result" == "{"* ]]; then
        local ok
        ok=$(echo "$result" | jq -r '.ok // empty' 2>/dev/null)
        if [[ "$ok" == "true" ]]; then
            if [[ "$seconds" -eq 0 ]]; then
                printf '  %s%s%s  %sheartbeat → %sdisabled%s\n' \
                    "$(c_device)" "$device" "$(_rst)" \
                    "$(c_ok)" "$(c_muted)" "$(_rst)"
            else
                printf '  %s%s%s  %sheartbeat → %severy %ds%s\n' \
                    "$(c_device)" "$device" "$(_rst)" \
                    "$(c_ok)" "$(c_text)" "$seconds" "$(_rst)"
            fi
        else
            local error
            error=$(json_str "$result" "error")
            printf '  %s%s%s\n' "$(c_err)" "${error:-$result}" "$(_rst)"
            return 1
        fi
    fi
}

cmd_rename() {
    if [[ -z "${1:-}" ]] || [[ -z "${2:-}" ]]; then
        err "missing arguments"
        printf '  %susage: ionode rename <device> <new_name>%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"
    local new_name="$2"

    printf '  %s⚠  Rename will reboot the device.%s\n' "$(c_warn)" "$(_rst)"

    local result
    if ! result=$(nats_req "${device}.config.name.set" "$new_name" "3s") || [[ -z "$result" ]]; then
        timeout_msg "$device"
        return 1
    fi

    if has_jq && [[ "$result" == "{"* ]]; then
        local ok
        ok=$(echo "$result" | jq -r '.ok // empty' 2>/dev/null)
        if [[ "$ok" == "true" ]]; then
            printf '  %s%s%s  %s→  %s%s%s  %s(rebooting...)%s\n' \
                "$(c_device)" "$device" "$(_rst)" \
                "$(c_ok)" "$(c_device)" "$new_name" "$(_rst)" \
                "$(c_dim)" "$(_rst)"
        else
            local error
            error=$(json_str "$result" "error")
            printf '  %s%s%s\n' "$(c_err)" "${error:-$result}" "$(_rst)"
            return 1
        fi
    fi
}

cmd_device_add() {
    if [[ -z "${1:-}" ]] || [[ -z "${2:-}" ]] || [[ -z "${3:-}" ]] || [[ -z "${4:-}" ]]; then
        err "missing arguments"
        printf '  %susage: ionode device add <device> <name> <kind> <pin> [--unit U] [--inverted] [--baud N] [--nats subj]%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"
    local dev_name="$2"
    local kind="$3"
    local pin="$4"
    shift 4

    # Parse optional flags
    local unit="" inverted=false baud="" nats_subj=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --unit)
                if [[ $# -lt 2 ]]; then err "--unit requires a value"; return 1; fi
                unit="$2"; shift 2 ;;
            --inverted) inverted=true; shift ;;
            --baud)
                if [[ $# -lt 2 ]]; then err "--baud requires a value"; return 1; fi
                baud="$2"; shift 2 ;;
            --nats)
                if [[ $# -lt 2 ]]; then err "--nats requires a value"; return 1; fi
                nats_subj="$2"; shift 2 ;;
            -*) err "unknown option: $1"; return 1 ;;
            *)  err "unexpected argument: $1"; return 1 ;;
        esac
    done

    # Build JSON payload
    local payload
    payload=$(printf '{"n":"%s","k":"%s","p":%s,"u":"%s","i":%s' \
        "$dev_name" "$kind" "$pin" "$unit" "$inverted")

    if [[ -n "$nats_subj" ]]; then
        payload+=",\"ns\":\"${nats_subj}\""
    fi
    if [[ -n "$baud" ]]; then
        payload+=",\"bd\":${baud}"
    fi
    payload+="}"

    local result
    if ! result=$(nats_req "${device}.config.device.add" "$payload" "3s") || [[ -z "$result" ]]; then
        timeout_msg "$device"
        return 1
    fi

    if has_jq && [[ "$result" == "{"* ]]; then
        local ok
        ok=$(echo "$result" | jq -r '.ok // empty' 2>/dev/null)
        if [[ "$ok" == "true" ]]; then
            local kind_color="c_sensor"
            case "$kind" in
                digital_out|relay|pwm|rgb_led) kind_color="c_actuator" ;;
            esac
            printf '  %s+%s  %s%s%s  %s%s%s  %spin %s%s\n' \
                "$(c_ok)" "$(_rst)" \
                "$($kind_color)" "$dev_name" "$(_rst)" \
                "$(c_dim)" "$kind" "$(_rst)" \
                "$(c_muted)" "$pin" "$(_rst)"
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

cmd_device_remove() {
    if [[ -z "${1:-}" ]] || [[ -z "${2:-}" ]]; then
        err "missing arguments"
        printf '  %susage: ionode device remove <device> <name>%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"
    local dev_name="$2"

    local payload
    payload=$(printf '{"n":"%s"}' "$dev_name")

    local result
    if ! result=$(nats_req "${device}.config.device.remove" "$payload" "3s") || [[ -z "$result" ]]; then
        timeout_msg "$device"
        return 1
    fi

    if has_jq && [[ "$result" == "{"* ]]; then
        local ok
        ok=$(echo "$result" | jq -r '.ok // empty' 2>/dev/null)
        if [[ "$ok" == "true" ]]; then
            printf '  %s−%s  %s%s%s  %sremoved%s\n' \
                "$(c_err)" "$(_rst)" \
                "$(c_dim)" "$dev_name" "$(_rst)" \
                "$(c_muted)" "$(_rst)"
        else
            local error
            error=$(json_str "$result" "error")
            printf '  %s✗  %s%s\n' "$(c_err)" "${error:-not found}" "$(_rst)"
            return 1
        fi
    fi
}

cmd_device_list() {
    if [[ -z "${1:-}" ]]; then
        err "missing argument"
        printf '  %susage: ionode device list <device>%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"
    cmd_devices "$device"
}
