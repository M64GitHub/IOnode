#!/usr/bin/env bash
# IOnode CLI — discover / ls / info / group commands

cmd_discover() {
    if ! has_jq; then
        err "jq is required for discover. Install: apt install jq"
        exit 1
    fi

    spinner_start "Discovering IOnode devices..."
    local raw
    raw=$(nats_req_multi "_ion.discover" "" "3s")
    local rc=$?
    spinner_stop

    if [[ $rc -ne 0 ]] || [[ -z "$raw" ]]; then
        printf '\n  %sNo IOnode devices found on the network.%s\n' "$(c_dim)" "$(_rst)"
        printf '  %sCheck NATS server: %s%s\n\n' "$(c_muted)" "$(get_nats_url)" "$(_rst)"
        return 1
    fi

    header "Fleet Discovery"
    printf '\n'

    # Deduplicate by device name (node may respond via multiple subscriptions)
    local -A seen_devices
    local count=0
    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        [[ "$line" != "{"* ]] && continue

        local dev_name
        dev_name=$(json_str "$line" "device")
        [[ -n "${seen_devices[$dev_name]:-}" ]] && continue
        seen_devices[$dev_name]=1

        _print_node_card "$line"
        count=$((count + 1))
    done <<< "$raw"

    printf '  %s%d node(s) found%s  %s·  %s%s\n\n' \
        "$(c_dim)" "$count" "$(_rst)" \
        "$(c_muted)" "$(get_nats_url)" "$(_rst)"
}

cmd_ls() {
    if ! has_jq; then
        err "jq is required for ls. Install: apt install jq"
        exit 1
    fi

    local tag_filter=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --tag) tag_filter="$2"; shift 2 ;;
            *) shift ;;
        esac
    done

    local subject="_ion.discover"
    if [[ -n "$tag_filter" ]]; then
        subject="_ion.group.${tag_filter}"
    fi

    spinner_start "Scanning fleet..."
    local raw
    raw=$(nats_req_multi "$subject" "" "3s")
    local rc=$?
    spinner_stop

    if [[ $rc -ne 0 ]] || [[ -z "$raw" ]]; then
        printf '\n  %sNo devices found.%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi

    if [[ -n "$tag_filter" ]]; then
        header "Fleet  ·  tag: ${tag_filter}"
    else
        header "Fleet"
    fi
    printf '\n'

    # Table header
    printf '  %s%-18s %-12s %-6s %-8s %-9s %-7s %s%s\n' \
        "$(c_dim)$(c_bold)" \
        "DEVICE" "CHIP" "TAG" "HEAP" "RSSI" "SENS" "ACT" \
        "$(_rst)"
    hr 72

    # Deduplicate by device name
    local -A seen_devices
    local count=0
    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        [[ "$line" != "{"* ]] && continue

        local _dn
        _dn=$(json_str "$line" "device")
        [[ -n "${seen_devices[$_dn]:-}" ]] && continue
        seen_devices[$_dn]=1

        local name chip tag heap rssi
        name=$(json_str "$line" "device")
        chip=$(json_str "$line" "chip")
        tag=$(json_str "$line" "tag")
        heap=$(json_num "$line" "free_heap")
        rssi=$(echo "$line" | jq -r '.rssi // empty' 2>/dev/null)

        local n_sensors n_actuators
        n_sensors=$(echo "$line" | jq '[.devices[] | select(.kind | test("digital_in|analog_in|ntc_10k|ldr|internal_temp|clock_|nats_value|serial_text"))] | length' 2>/dev/null || echo 0)
        n_actuators=$(echo "$line" | jq '[.devices[] | select(.kind | test("digital_out|relay|pwm|rgb_led"))] | length' 2>/dev/null || echo 0)

        # Format heap
        local heap_str
        heap_str=$(fmt_heap "$heap")

        # Format RSSI with signal bar
        local rssi_str=""
        if [[ -n "$rssi" ]] && [[ "$rssi" != "null" ]]; then
            rssi_str="${rssi}dB"
        else
            rssi_str="—"
        fi

        printf '  %s%-18s%s %s%-12s%s %s%-6s%s %s%-8s%s %s%-9s%s %s%-7s%s %s%s%s\n' \
            "$(c_device)" "$name" "$(_rst)" \
            "$(c_text)" "$chip" "$(_rst)" \
            "$(c_dim)" "${tag:-—}" "$(_rst)" \
            "$(c_text)" "$heap_str" "$(_rst)" \
            "$(c_text)" "$rssi_str" "$(_rst)" \
            "$(c_sensor)" "$n_sensors" "$(_rst)" \
            "$(c_actuator)" "$n_actuators" "$(_rst)"

        count=$((count + 1))
    done <<< "$raw"

    printf '\n  %s%d node(s)%s\n\n' "$(c_dim)" "$count" "$(_rst)"
}

cmd_info() {
    if [[ -z "${1:-}" ]]; then
        err "missing argument"
        printf '  %susage: ionode info <device>%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"

    if ! has_jq; then
        err "jq is required for info. Install: apt install jq"
        exit 1
    fi

    spinner_start "Querying ${device}..."
    local caps
    caps=$(nats_req "${device}.capabilities" "" "3s")
    local rc=$?
    spinner_stop

    if [[ $rc -ne 0 ]] || [[ -z "$caps" ]]; then
        timeout_msg "$device"
        return 1
    fi

    # Also get system details
    local uptime_val rssi_val reset_reason reconnects
    uptime_val=$(nats_req "${device}.hal.system.uptime" "" "2s" 2>/dev/null || echo "")
    rssi_val=$(nats_req "${device}.hal.system.rssi" "" "2s" 2>/dev/null || echo "")
    reset_reason=$(nats_req "${device}.hal.system.reset_reason" "" "2s" 2>/dev/null || echo "")
    reconnects=$(nats_req "${device}.hal.system.nats_reconnects" "" "2s" 2>/dev/null || echo "")

    _print_node_detail "$caps" "$uptime_val" "$rssi_val" "$reset_reason" "$reconnects"
}

cmd_group() {
    if [[ -z "${1:-}" ]]; then
        err "missing argument"
        printf '  %susage: ionode group <tag>%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local tag="$1"
    cmd_ls --tag "$tag"
}

cmd_status_fleet() {
    cmd_ls "$@"
}

cmd_status_node() {
    local device="$1"

    if ! has_jq; then
        err "jq is required for status. Install: apt install jq"
        exit 1
    fi

    spinner_start "Querying ${device}..."

    local temp heap uptime rssi reset_reason reconnects
    temp=$(nats_req "${device}.hal.system.temperature" "" "2s" 2>/dev/null || echo "?")
    heap=$(nats_req "${device}.hal.system.heap" "" "2s" 2>/dev/null || echo "?")
    uptime=$(nats_req "${device}.hal.system.uptime" "" "2s" 2>/dev/null || echo "?")
    rssi=$(nats_req "${device}.hal.system.rssi" "" "2s" 2>/dev/null || echo "?")
    reset_reason=$(nats_req "${device}.hal.system.reset_reason" "" "2s" 2>/dev/null || echo "?")
    reconnects=$(nats_req "${device}.hal.system.nats_reconnects" "" "2s" 2>/dev/null || echo "?")

    spinner_stop

    if [[ "$temp" == "?" ]] && [[ "$heap" == "?" ]]; then
        timeout_msg "$device"
        return 1
    fi

    header "Status  ·  ${device}"
    printf '\n'

    # Temperature
    if [[ "$temp" != "?" ]]; then
        local temp_color="c_green"
        local temp_f
        temp_f=$(printf '%.0f' "$temp" 2>/dev/null || echo 0)
        [[ "$temp_f" -gt 50 ]] && temp_color="c_accent"
        [[ "$temp_f" -gt 65 ]] && temp_color="c_red"
        kv_color "Chip temp:" "${temp}°C" "$temp_color"
    fi

    # Heap
    if [[ "$heap" != "?" ]]; then
        local heap_str
        heap_str=$(fmt_heap "$heap")
        local heap_color="c_green"
        [[ "$heap" -lt 100000 ]] && heap_color="c_accent"
        [[ "$heap" -lt 50000 ]] && heap_color="c_red"
        kv_color "Free heap:" "$heap_str" "$heap_color"
    fi

    # Uptime
    if [[ "$uptime" != "?" ]]; then
        local up_str
        up_str=$(fmt_uptime "$uptime")
        kv "Uptime:" "$up_str"
    fi

    # RSSI
    if [[ "$rssi" != "?" ]]; then
        printf '  %s%-16s%s %s %s%sdBm%s\n' \
            "$(c_label)" "WiFi signal:" "$(_rst)" \
            "$(signal_bar "$rssi")" \
            "$(c_dim)" "$rssi" "$(_rst)"
    fi

    # Reset reason
    if [[ "$reset_reason" != "?" ]]; then
        kv "Last reset:" "$reset_reason"
    fi

    # NATS reconnects
    if [[ "$reconnects" != "?" ]]; then
        kv "NATS reconnects:" "$reconnects"
    fi

    printf '\n'
}

# --- Internal: Print a node discovery card ---
_print_node_card() {
    local json="$1"

    local name chip heap ip tag version
    name=$(json_str "$json" "device")
    chip=$(json_str "$json" "chip")
    heap=$(json_num "$json" "free_heap")
    ip=$(json_str "$json" "ip")
    tag=$(json_str "$json" "tag")
    version=$(json_str "$json" "version")

    local heap_str
    heap_str=$(fmt_heap "$heap")

    # Device name + chip
    printf '  %s%s%s' "$(c_device)" "$name" "$(_rst)"
    printf '  %s%s%s' "$(c_dim)" "$chip" "$(_rst)"
    if [[ -n "$tag" ]]; then
        printf '  %s#%s%s' "$(c_accent)" "$tag" "$(_rst)"
    fi
    printf '\n'

    # Details
    printf '    %sIP%s %-16s  %sheap%s %-10s  %sv%s%s\n' \
        "$(c_muted)" "$(_rst)" "$ip" \
        "$(c_muted)" "$(_rst)" "$heap_str" \
        "$(c_muted)" "$(_rst)" "$version"

    # Devices summary
    local devices_json
    devices_json=$(echo "$json" | jq -c '.devices // []' 2>/dev/null)
    local n_devs
    n_devs=$(echo "$devices_json" | jq 'length' 2>/dev/null || echo 0)

    if [[ "$n_devs" -gt 0 ]]; then
        printf '    '
        echo "$devices_json" | jq -r '.[] | "\(.kind):\(.name)"' 2>/dev/null | while IFS=: read -r kind dname; do
            case "$kind" in
                digital_out|relay|pwm|rgb_led)
                    printf '%s%s%s ' "$(c_actuator)" "$dname" "$(_rst)" ;;
                *)
                    printf '%s%s%s ' "$(c_sensor)" "$dname" "$(_rst)" ;;
            esac
        done
        printf '\n'
    fi

    printf '\n'
}

# --- Internal: Print detailed node info ---
_print_node_detail() {
    local json="$1" uptime_val="$2" rssi_val="$3" reset_reason="$4" reconnects="$5"

    local name chip heap ip tag version
    name=$(json_str "$json" "device")
    chip=$(json_str "$json" "chip")
    heap=$(json_num "$json" "free_heap")
    ip=$(json_str "$json" "ip")
    tag=$(json_str "$json" "tag")
    version=$(json_str "$json" "version")

    header "Node Detail  ·  ${name}"
    printf '\n'

    kv "Firmware:" "IOnode v${version}"
    kv "Chip:" "$chip"
    kv "IP:" "$ip"
    kv "Tag:" "${tag:-—}"

    local heap_str
    heap_str=$(fmt_heap "$heap")
    local heap_color="c_green"
    [[ "$heap" -lt 100000 ]] && heap_color="c_accent"
    [[ "$heap" -lt 50000 ]] && heap_color="c_red"
    kv_color "Free heap:" "$heap_str" "$heap_color"

    if [[ -n "$uptime_val" ]]; then
        local up_str
        up_str=$(fmt_uptime "$uptime_val")
        kv "Uptime:" "$up_str"
    fi

    if [[ -n "$rssi_val" ]]; then
        printf '  %s%-16s%s %s %s%sdBm%s\n' \
            "$(c_label)" "WiFi signal:" "$(_rst)" \
            "$(signal_bar "$rssi_val")" \
            "$(c_dim)" "$rssi_val" "$(_rst)"
    fi

    if [[ -n "$reset_reason" ]]; then
        kv "Last reset:" "$reset_reason"
    fi

    if [[ -n "$reconnects" ]]; then
        kv "NATS reconnects:" "$reconnects"
    fi

    # HAL capabilities
    local hal_json
    hal_json=$(echo "$json" | jq -c '.hal // {}' 2>/dev/null)
    if [[ -n "$hal_json" ]] && [[ "$hal_json" != "{}" ]]; then
        printf '\n'
        printf '  %sHAL:%s ' "$(c_label)" "$(_rst)"
        for cap in gpio adc pwm dac uart system_temp; do
            local val
            val=$(echo "$hal_json" | jq -r ".${cap} // false" 2>/dev/null)
            if [[ "$val" == "true" ]]; then
                printf '%s%s%s ' "$(c_green)" "$cap" "$(_rst)"
            else
                printf '%s%s%s ' "$(c_muted)" "$cap" "$(_rst)"
            fi
        done
        printf '\n'
    fi

    # Devices
    local devices_json
    devices_json=$(echo "$json" | jq -c '.devices // []' 2>/dev/null)
    local n_devs
    n_devs=$(echo "$devices_json" | jq 'length' 2>/dev/null || echo 0)

    if [[ "$n_devs" -gt 0 ]]; then
        printf '\n'
        printf '  %s%sDevices:%s\n' "$(c_label)" "$(c_bold)" "$(_rst)"

        echo "$devices_json" | jq -c '.[]' 2>/dev/null | while IFS= read -r dev; do
            local dname dkind dvalue dunit
            dname=$(json_str "$dev" "name")
            dkind=$(json_str "$dev" "kind")
            dvalue=$(echo "$dev" | jq -r '.value // empty' 2>/dev/null)
            dunit=$(json_str "$dev" "unit")

            local kind_color="c_sensor"
            case "$dkind" in
                digital_out|relay|pwm|rgb_led) kind_color="c_actuator" ;;
            esac

            printf '    %s%-16s%s %s%-14s%s' \
                "$($kind_color)" "$dname" "$(_rst)" \
                "$(c_muted)" "$dkind" "$(_rst)"

            if [[ -n "$dvalue" ]]; then
                printf ' %s%s%s' "$(c_text)" "$dvalue" "$(_rst)"
                if [[ -n "$dunit" ]]; then
                    printf '%s%s%s' "$(c_unit)" "$dunit" "$(_rst)"
                fi
            fi
            printf '\n'
        done
    fi

    printf '\n'
}
