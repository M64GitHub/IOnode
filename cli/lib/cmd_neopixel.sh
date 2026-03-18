#!/usr/bin/env bash
# IOnode CLI — NeoPixel LED strip commands

# --- Color name → hex mapping ---
_neo_color() {
    local input="${1:-}"
    # Strip common prefixes
    input="${input#\#}"
    input="${input#0x}"
    input="${input#0X}"

    # Named colors
    case "${input,,}" in
        red)     echo "FF0000" ;;
        green)   echo "00FF00" ;;
        blue)    echo "0000FF" ;;
        white)   echo "FFFFFF" ;;
        off)     echo "000000" ;;
        yellow)  echo "FFFF00" ;;
        cyan)    echo "00FFFF" ;;
        magenta) echo "FF00FF" ;;
        orange)  echo "FF8C00" ;;
        purple)  echo "8000FF" ;;
        *)
            # Validate hex (must be 6 hex chars)
            if [[ "${input}" =~ ^[0-9a-fA-F]{6}$ ]]; then
                echo "${input^^}"
            else
                err "invalid color: ${1} (use 6-digit hex or name: red, green, blue, white, off, yellow, cyan, magenta, orange, purple)"
                return 1
            fi
            ;;
    esac
}

# --- Color swatch (terminal true-color block) ---
_neo_swatch() {
    local hex="$1"
    local r=$((16#${hex:0:2}))
    local g=$((16#${hex:2:2}))
    local b=$((16#${hex:4:2}))
    printf '%s██%s' "$(_c_bg_rgb "$r" "$g" "$b")" "$(_rst)"
}

_neo_help() {
    printf '\n'
    printf '  %s%sNeoPixel Commands%s\n\n' "$(c_accent)" "$(c_bold)" "$(_rst)"
    printf '  %susage: ionode neopixel <node> <strip> <action> [args...]%s\n\n' "$(c_dim)" "$(_rst)"
    printf '  %s%sACTIONS%s\n' "$(c_dim)" "$(c_bold)" "$(_rst)"
    printf '    %sfill%s       %s<color>%s              Fill all pixels\n' "$(c_accent)" "$(_rst)" "$(c_dim)" "$(_rst)"
    printf '    %sset%s        %s<pixel> <color>%s      Set one pixel\n' "$(c_accent)" "$(_rst)" "$(c_dim)" "$(_rst)"
    printf '    %sbrightness%s %s<0-255>%s              Set brightness\n' "$(c_accent)" "$(_rst)" "$(c_dim)" "$(_rst)"
    printf '    %sclear%s                              All off\n' "$(c_accent)" "$(_rst)"
    printf '    %sget%s                                Status\n' "$(c_accent)" "$(_rst)"
    printf '    %sbatch%s      %s<p:c>[,<p:c>,...]%s    Set multiple pixels\n' "$(c_accent)" "$(_rst)" "$(c_dim)" "$(_rst)"
    printf '\n'
    printf '  %s%sCOLORS%s\n' "$(c_dim)" "$(c_bold)" "$(_rst)"
    printf '    %sHex:%s    FF0000, #00FF00, 0x0000FF\n' "$(c_label)" "$(_rst)"
    printf '    %sNamed:%s  red, green, blue, white, off, yellow, cyan, magenta, orange, purple\n' "$(c_label)" "$(_rst)"
    printf '\n'
    printf '  %s%sEXAMPLES%s\n' "$(c_dim)" "$(c_bold)" "$(_rst)"
    printf '    %sionode neopixel mynode led_strip fill red%s\n' "$(c_dim)" "$(_rst)"
    printf '    %sionode neopixel mynode led_strip set 0 00FF00%s\n' "$(c_dim)" "$(_rst)"
    printf '    %sionode neopixel mynode led_strip batch 0:FF0000,1:00FF00,2:0000FF%s\n' "$(c_dim)" "$(_rst)"
    printf '\n'
}

cmd_neopixel() {
    if [[ -z "${1:-}" ]] || [[ -z "${2:-}" ]] || [[ -z "${3:-}" ]]; then
        _neo_help
        return 1
    fi

    local node="$1"
    local strip="$2"
    local action="$3"
    shift 3

    case "$action" in
        fill)
            if [[ -z "${1:-}" ]]; then
                err "missing color"
                printf '  %susage: ionode neopixel <node> <strip> fill <color>%s\n\n' "$(c_dim)" "$(_rst)"
                return 1
            fi
            local color
            if ! color=$(_neo_color "$1"); then
                return 1
            fi

            local result
            if ! result=$(nats_req "${node}.hal.${strip}.set" "{\"fill\":\"${color}\"}" "3s") || [[ -z "$result" ]]; then
                timeout_msg "$node"
                return 1
            fi

            if [[ "$result" == "ok" ]]; then
                printf '  %s%s%s  %sfill%s  %s  %s#%s%s\n' \
                    "$(c_actuator)" "$strip" "$(_rst)" \
                    "$(c_ok)" "$(_rst)" \
                    "$(_neo_swatch "$color")" \
                    "$(c_dim)" "$color" "$(_rst)"
            else
                printf '  %s%s%s\n' "$(c_err)" "$result" "$(_rst)"
                return 1
            fi
            ;;

        set)
            if [[ -z "${1:-}" ]] || [[ -z "${2:-}" ]]; then
                err "missing arguments"
                printf '  %susage: ionode neopixel <node> <strip> set <pixel> <color>%s\n\n' "$(c_dim)" "$(_rst)"
                return 1
            fi
            local pixel="$1"
            local color
            if ! color=$(_neo_color "$2"); then
                return 1
            fi

            local result
            if ! result=$(nats_req "${node}.hal.${strip}.set" "{\"pixel\":${pixel},\"color\":\"${color}\"}" "3s") || [[ -z "$result" ]]; then
                timeout_msg "$node"
                return 1
            fi

            if [[ "$result" == "ok" ]]; then
                printf '  %s%s%s  %spx %s%s  %s  %s#%s%s\n' \
                    "$(c_actuator)" "$strip" "$(_rst)" \
                    "$(c_ok)" "$pixel" "$(_rst)" \
                    "$(_neo_swatch "$color")" \
                    "$(c_dim)" "$color" "$(_rst)"
            else
                printf '  %s%s%s\n' "$(c_err)" "$result" "$(_rst)"
                return 1
            fi
            ;;

        brightness)
            if [[ -z "${1:-}" ]]; then
                err "missing brightness value"
                printf '  %susage: ionode neopixel <node> <strip> brightness <0-255>%s\n\n' "$(c_dim)" "$(_rst)"
                return 1
            fi
            local bri="$1"

            local result
            if ! result=$(nats_req "${node}.hal.${strip}.set" "{\"brightness\":${bri}}" "3s") || [[ -z "$result" ]]; then
                timeout_msg "$node"
                return 1
            fi

            if [[ "$result" == "ok" ]]; then
                local pct=$(( bri * 100 / 255 ))
                printf '  %s%s%s  %s← brightness %s%s/255  %s(%d%%)%s\n' \
                    "$(c_actuator)" "$strip" "$(_rst)" \
                    "$(c_ok)" "$bri" "$(_rst)" \
                    "$(c_dim)" "$pct" "$(_rst)"
            else
                printf '  %s%s%s\n' "$(c_err)" "$result" "$(_rst)"
                return 1
            fi
            ;;

        clear)
            local result
            if ! result=$(nats_req "${node}.hal.${strip}.set" '{"clear":true}' "3s") || [[ -z "$result" ]]; then
                timeout_msg "$node"
                return 1
            fi

            if [[ "$result" == "ok" ]]; then
                printf '  %s%s%s  %scleared%s\n' \
                    "$(c_actuator)" "$strip" "$(_rst)" \
                    "$(c_ok)" "$(_rst)"
            else
                printf '  %s%s%s\n' "$(c_err)" "$result" "$(_rst)"
                return 1
            fi
            ;;

        get)
            local result
            if ! result=$(nats_req "${node}.hal.${strip}.get" "" "3s") || [[ -z "$result" ]]; then
                timeout_msg "$node"
                return 1
            fi

            if has_jq && [[ "$result" == "{"* ]]; then
                local bri count val
                bri=$(json_num "$result" "brightness")
                count=$(json_num "$result" "count")
                val=$(json_num "$result" "value")
                local pct=$(( bri * 100 / 255 ))

                header "NeoPixel  ·  ${node}/${strip}"
                printf '\n'
                kv "Brightness:" "${bri}/255 (${pct}%)"
                kv "Pixels:" "$count"
                kv "Value:" "$val"
                printf '\n'
            else
                echo "$result"
            fi
            ;;

        batch)
            if [[ -z "${1:-}" ]]; then
                err "missing pixel:color pairs"
                printf '  %susage: ionode neopixel <node> <strip> batch <p:c>[,<p:c>,...]%s\n' "$(c_dim)" "$(_rst)"
                printf '  %sexample: ionode neopixel mynode strip batch 0:FF0000,1:00FF00,2:0000FF%s\n\n' "$(c_dim)" "$(_rst)"
                return 1
            fi

            local pairs="$1"
            local failed=0
            local sent=0

            IFS=',' read -ra entries <<< "$pairs"
            for entry in "${entries[@]}"; do
                local pixel="${entry%%:*}"
                local raw_color="${entry#*:}"

                if [[ "$entry" != *":"* ]] || [[ -z "$pixel" ]] || [[ -z "$raw_color" ]]; then
                    err "invalid format: ${entry} (expected pixel:color)"
                    failed=$((failed + 1))
                    continue
                fi

                local color
                if ! color=$(_neo_color "$raw_color"); then
                    failed=$((failed + 1))
                    continue
                fi

                local result
                if ! result=$(nats_req "${node}.hal.${strip}.set" "{\"pixel\":${pixel},\"color\":\"${color}\"}" "3s") || [[ -z "$result" ]]; then
                    err "px ${pixel}: no response"
                    failed=$((failed + 1))
                    continue
                fi

                if [[ "$result" == "ok" ]]; then
                    printf '  %s%s%s  %spx %s%s  %s  %s#%s%s\n' \
                        "$(c_actuator)" "$strip" "$(_rst)" \
                        "$(c_ok)" "$pixel" "$(_rst)" \
                        "$(_neo_swatch "$color")" \
                        "$(c_dim)" "$color" "$(_rst)"
                    sent=$((sent + 1))
                else
                    printf '  %spx %s: %s%s\n' "$(c_err)" "$pixel" "$result" "$(_rst)"
                    failed=$((failed + 1))
                fi
            done

            printf '\n  %s%d set%s' "$(c_dim)" "$sent" "$(_rst)"
            if [[ "$failed" -gt 0 ]]; then
                printf ', %s%d failed%s' "$(c_err)" "$failed" "$(_rst)"
            fi
            printf '\n\n'

            [[ "$failed" -eq 0 ]] || return 1
            ;;

        help|--help|-h)
            _neo_help
            ;;

        *)
            err "unknown neopixel action: ${action}"
            _neo_help
            return 1
            ;;
    esac
}
