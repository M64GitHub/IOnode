#!/usr/bin/env bash
# IOnode CLI — Hardware access commands (read, write, gpio, adc, pwm, uart, devices)

cmd_read() {
    if [[ -z "${1:-}" ]] || [[ -z "${2:-}" ]]; then
        err "missing arguments"
        printf '  %susage: ionode read <device> <sensor> [--info]%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"
    local sensor="$2"
    local info_flag=false

    if [[ "${3:-}" == "--info" ]]; then
        info_flag=true
    fi

    if [[ "$info_flag" == true ]]; then
        local result
        result=$(nats_req "${device}.hal.${sensor}.info" "" "3s")
        if [[ $? -ne 0 ]] || [[ -z "$result" ]]; then
            timeout_msg "$device"
            return 1
        fi
        if has_jq && [[ "$result" == "{"* ]]; then
            local dname dkind dvalue dunit dpin
            dname=$(json_str "$result" "name")
            dkind=$(json_str "$result" "kind")
            dvalue=$(echo "$result" | jq -r '.value // empty' 2>/dev/null)
            dunit=$(json_str "$result" "unit")
            dpin=$(json_num "$result" "pin")

            local kind_color="c_sensor"
            case "$dkind" in
                digital_out|relay|pwm|rgb_led) kind_color="c_actuator" ;;
            esac

            printf '\n'
            kv "Name:" "$dname"
            kv_color "Kind:" "$dkind" "$kind_color"
            kv "Pin:" "$dpin"
            if [[ -n "$dvalue" ]]; then
                printf '  %s%-16s%s %s%s%s%s%s%s\n' \
                    "$(c_label)" "Value:" "$(_rst)" \
                    "$(c_accent)$(c_bold)" "$dvalue" "$(_rst)" \
                    "$(c_unit)" "${dunit:+ $dunit}" "$(_rst)"
            fi
            printf '\n'
        else
            echo "$result"
        fi
        return
    fi

    local result
    result=$(nats_req "${device}.hal.${sensor}" "" "3s")
    if [[ $? -ne 0 ]] || [[ -z "$result" ]]; then
        timeout_msg "$device"
        return 1
    fi

    # Try to detect unit from device info (quick check)
    local unit=""
    if has_jq; then
        local info
        info=$(nats_req "${device}.hal.${sensor}.info" "" "2s" 2>/dev/null || echo "")
        if [[ -n "$info" ]] && [[ "$info" == "{"* ]]; then
            unit=$(json_str "$info" "unit")
        fi
    fi

    printf '  %s%s%s  %s%s%s%s%s%s\n' \
        "$(c_sensor)" "$sensor" "$(_rst)" \
        "$(c_accent)$(c_bold)" "$result" "$(_rst)" \
        "$(c_unit)" "${unit:+ $unit}" "$(_rst)"
}

cmd_write() {
    if [[ -z "${1:-}" ]] || [[ -z "${2:-}" ]] || [[ -z "${3:-}" ]]; then
        err "missing arguments"
        printf '  %susage: ionode write <device> <actuator> <value>%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"
    local actuator="$2"
    local value="$3"

    local result
    result=$(nats_req "${device}.hal.${actuator}.set" "$value" "3s")
    if [[ $? -ne 0 ]] || [[ -z "$result" ]]; then
        timeout_msg "$device"
        return 1
    fi

    if [[ "$result" == "ok" ]]; then
        printf '  %s%s%s  %s← %s%s\n' \
            "$(c_actuator)" "$actuator" "$(_rst)" \
            "$(c_ok)" "$value" "$(_rst)"
    else
        printf '  %s%s%s\n' "$(c_err)" "$result" "$(_rst)"
        return 1
    fi
}

cmd_gpio() {
    if [[ -z "${1:-}" ]] || [[ -z "${2:-}" ]] || [[ -z "${3:-}" ]]; then
        err "missing arguments"
        printf '  %susage: ionode gpio <device> <pin> get|set [value]%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"
    local pin="$2"
    local action="$3"

    case "$action" in
        get)
            local result
            result=$(nats_req "${device}.hal.gpio.${pin}.get" "" "3s")
            if [[ $? -ne 0 ]] || [[ -z "$result" ]]; then
                timeout_msg "$device"
                return 1
            fi
            local state_str state_color
            if [[ "$result" == "1" ]]; then
                state_str="HIGH"
                state_color="$(c_green)"
            else
                state_str="LOW"
                state_color="$(c_dim)"
            fi
            printf '  %sGPIO %s%s  %s%s%s  %s(%s)%s\n' \
                "$(c_label)" "$pin" "$(_rst)" \
                "$state_color$(c_bold)" "$result" "$(_rst)" \
                "$(c_dim)" "$state_str" "$(_rst)"
            ;;
        set)
            if [[ -z "${4:-}" ]]; then
                err "missing value"
                printf '  %susage: ionode gpio <device> <pin> set <0|1>%s\n\n' "$(c_dim)" "$(_rst)"
                return 1
            fi
            local value="$4"
            local result
            result=$(nats_req "${device}.hal.gpio.${pin}.set" "$value" "3s")
            if [[ $? -ne 0 ]] || [[ -z "$result" ]]; then
                timeout_msg "$device"
                return 1
            fi
            if [[ "$result" == "ok" ]]; then
                local state_str
                [[ "$value" == "1" ]] && state_str="HIGH" || state_str="LOW"
                printf '  %sGPIO %s%s  %s← %s%s  %s(%s)%s\n' \
                    "$(c_label)" "$pin" "$(_rst)" \
                    "$(c_ok)" "$value" "$(_rst)" \
                    "$(c_dim)" "$state_str" "$(_rst)"
            else
                printf '  %s%s%s\n' "$(c_err)" "$result" "$(_rst)"
                return 1
            fi
            ;;
        *)
            err "gpio action must be 'get' or 'set'"
            printf '  %susage: ionode gpio <device> <pin> get|set [value]%s\n\n' "$(c_dim)" "$(_rst)"
            return 1
            ;;
    esac
}

cmd_adc() {
    if [[ -z "${1:-}" ]] || [[ -z "${2:-}" ]]; then
        err "missing arguments"
        printf '  %susage: ionode adc <device> <pin>%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"
    local pin="$2"

    local result
    result=$(nats_req "${device}.hal.adc.${pin}.read" "" "3s")
    if [[ $? -ne 0 ]] || [[ -z "$result" ]]; then
        timeout_msg "$device"
        return 1
    fi

    # Show a mini bar for ADC (0-4095 range)
    local pct=0
    if [[ "$result" =~ ^[0-9]+$ ]]; then
        pct=$(( result * 100 / 4095 ))
    fi

    local bar_len=$(( pct / 5 ))  # 20 chars max
    local bar=""
    for ((i=0; i<bar_len; i++)); do bar+="█"; done
    for ((i=bar_len; i<20; i++)); do bar+="░"; done

    printf '  %sADC %s%s  %s%s%s  %s%s%s  %s%d%%%s\n' \
        "$(c_label)" "$pin" "$(_rst)" \
        "$(c_accent)$(c_bold)" "$result" "$(_rst)" \
        "$(c_blue)" "$bar" "$(_rst)" \
        "$(c_dim)" "$pct" "$(_rst)"
}

cmd_pwm() {
    if [[ -z "${1:-}" ]] || [[ -z "${2:-}" ]] || [[ -z "${3:-}" ]]; then
        err "missing arguments"
        printf '  %susage: ionode pwm <device> <pin> get|set [value]%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"
    local pin="$2"
    local action="$3"

    case "$action" in
        get)
            local result
            result=$(nats_req "${device}.hal.pwm.${pin}.get" "" "3s")
            if [[ $? -ne 0 ]] || [[ -z "$result" ]]; then
                timeout_msg "$device"
                return 1
            fi

            local pct=0
            if [[ "$result" =~ ^[0-9]+$ ]]; then
                pct=$(( result * 100 / 255 ))
            fi
            printf '  %sPWM %s%s  %s%s%s/255  %s(%d%%)%s\n' \
                "$(c_label)" "$pin" "$(_rst)" \
                "$(c_accent)$(c_bold)" "$result" "$(_rst)" \
                "$(c_dim)" "$pct" "$(_rst)"
            ;;
        set)
            if [[ -z "${4:-}" ]]; then
                err "missing value"
                printf '  %susage: ionode pwm <device> <pin> set <0-255>%s\n\n' "$(c_dim)" "$(_rst)"
                return 1
            fi
            local value="$4"
            local result
            result=$(nats_req "${device}.hal.pwm.${pin}.set" "$value" "3s")
            if [[ $? -ne 0 ]] || [[ -z "$result" ]]; then
                timeout_msg "$device"
                return 1
            fi
            if [[ "$result" == "ok" ]]; then
                local pct=$(( value * 100 / 255 ))
                printf '  %sPWM %s%s  %s← %s%s/255  %s(%d%%)%s\n' \
                    "$(c_label)" "$pin" "$(_rst)" \
                    "$(c_ok)" "$value" "$(_rst)" \
                    "$(c_dim)" "$pct" "$(_rst)"
            else
                printf '  %s%s%s\n' "$(c_err)" "$result" "$(_rst)"
                return 1
            fi
            ;;
        *)
            err "pwm action must be 'get' or 'set'"
            printf '  %susage: ionode pwm <device> <pin> get|set [value]%s\n\n' "$(c_dim)" "$(_rst)"
            return 1
            ;;
    esac
}

cmd_uart() {
    if [[ -z "${1:-}" ]] || [[ -z "${2:-}" ]]; then
        err "missing arguments"
        printf '  %susage: ionode uart <device> read|write [text]%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"
    local action="$2"

    case "$action" in
        read)
            local result
            result=$(nats_req "${device}.hal.uart.read" "" "3s")
            if [[ $? -ne 0 ]] || [[ -z "$result" ]]; then
                timeout_msg "$device"
                return 1
            fi
            printf '  %sUART ←%s  %s%s%s\n' \
                "$(c_label)" "$(_rst)" \
                "$(c_text)" "$result" "$(_rst)"
            ;;
        write)
            if [[ -z "${3:-}" ]]; then
                err "missing text"
                printf '  %susage: ionode uart <device> write <text>%s\n\n' "$(c_dim)" "$(_rst)"
                return 1
            fi
            local text="$3"
            local result
            result=$(nats_req "${device}.hal.uart.write" "$text" "3s")
            if [[ $? -ne 0 ]] || [[ -z "$result" ]]; then
                timeout_msg "$device"
                return 1
            fi
            if [[ "$result" == "ok" ]]; then
                printf '  %sUART →%s  %s%s%s\n' \
                    "$(c_ok)" "$(_rst)" \
                    "$(c_text)" "$text" "$(_rst)"
            else
                printf '  %s%s%s\n' "$(c_err)" "$result" "$(_rst)"
            fi
            ;;
        *)
            err "uart action must be 'read' or 'write'"
            printf '  %susage: ionode uart <device> read|write [text]%s\n\n' "$(c_dim)" "$(_rst)"
            return 1
            ;;
    esac
}

cmd_devices() {
    if [[ -z "${1:-}" ]]; then
        err "missing argument"
        printf '  %susage: ionode devices <device>%s\n\n' "$(c_dim)" "$(_rst)"
        return 1
    fi
    local device="$1"

    if ! has_jq; then
        err "jq is required for devices. Install: apt install jq"
        exit 1
    fi

    local result
    result=$(nats_req "${device}.hal.device.list" "" "3s")
    if [[ $? -ne 0 ]] || [[ -z "$result" ]]; then
        timeout_msg "$device"
        return 1
    fi

    header "Devices  ·  ${device}"
    printf '\n'

    local n_devs
    n_devs=$(echo "$result" | jq 'length' 2>/dev/null || echo 0)

    if [[ "$n_devs" -eq 0 ]]; then
        printf '  %sNo devices registered.%s\n\n' "$(c_dim)" "$(_rst)"
        return
    fi

    # Table header
    printf '  %s%-16s %-14s %-10s %s%s\n' \
        "$(c_dim)$(c_bold)" \
        "NAME" "KIND" "VALUE" "UNIT" \
        "$(_rst)"
    hr 52

    echo "$result" | jq -c '.[]' 2>/dev/null | while IFS= read -r dev; do
        local dname dkind dvalue dunit
        dname=$(json_str "$dev" "name")
        dkind=$(json_str "$dev" "kind")
        dvalue=$(echo "$dev" | jq -r '.value // empty' 2>/dev/null)
        dunit=$(json_str "$dev" "unit")

        local kind_color="c_sensor"
        case "$dkind" in
            digital_out|relay|pwm|rgb_led) kind_color="c_actuator" ;;
        esac

        printf '  %s%-16s%s %s%-14s%s %s%-10s%s %s%s%s\n' \
            "$($kind_color)" "$dname" "$(_rst)" \
            "$(c_dim)" "$dkind" "$(_rst)" \
            "$(c_text)$(c_bold)" "${dvalue:-—}" "$(_rst)" \
            "$(c_unit)" "${dunit:-}" "$(_rst)"
    done

    printf '\n  %s%d device(s)%s\n\n' "$(c_dim)" "$n_devs" "$(_rst)"
}
