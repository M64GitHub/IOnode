#!/usr/bin/env bash
# IOnode — OpenClaw hardware access helper
#
# Usage:
#   ion.sh discover                             — Find all IOnode devices
#   ion.sh caps <device>                        — Query device capabilities
#   ion.sh read <device> <sensor>               — Read a registered sensor
#   ion.sh set  <device> <actuator> <value>     — Set a registered actuator
#   ion.sh gpio <device> <pin> get|set [value]  — Raw GPIO read/write
#   ion.sh adc  <device> <pin>                  — Raw ADC read
#   ion.sh pwm  <device> <pin> <value>          — Raw PWM write
#   ion.sh sub  <device>                        — Subscribe to event stream
#
# Examples:
#   ion.sh discover
#   ion.sh read ionode-01 temperature
#   ion.sh set  ionode-01 fan 1
#   ion.sh gpio ionode-01 4 get
#   ion.sh gpio ionode-01 4 set 1
#   ion.sh adc  ionode-01 2
#   ion.sh pwm  ionode-01 3 128
#   ion.sh caps ionode-01
#   ion.sh sub  ionode-01
#
# Environment:
#   IONODE_NATS_URL  — NATS server URL (default: nats://localhost:4222)

set -euo pipefail

NATS_URL="${IONODE_NATS_URL:-nats://localhost:4222}"

usage() {
    echo "Usage:"
    echo "  ion.sh discover                             — Find all IOnode devices"
    echo "  ion.sh caps <device>                        — Query capabilities"
    echo "  ion.sh read <device> <sensor>               — Read a registered sensor"
    echo "  ion.sh set  <device> <actuator> <value>     — Set a registered actuator"
    echo "  ion.sh gpio <device> <pin> get|set [value]  — Raw GPIO"
    echo "  ion.sh adc  <device> <pin>                  — Raw ADC read"
    echo "  ion.sh pwm  <device> <pin> <value>          — Raw PWM write"
    echo "  ion.sh sub  <device>                        — Subscribe to events"
    exit 1
}

cmd_discover() {
    exec nats req "_ion.discover" "" \
        --server="${NATS_URL}" \
        --replies=0 \
        --timeout=3s
}

cmd_caps() {
    local device="${1:?caps requires <device>}"
    exec nats req "${device}.capabilities" "" \
        --server="${NATS_URL}" \
        --timeout=5s
}

cmd_read() {
    local device="${1:?read requires <device> <sensor>}"
    local sensor="${2:?read requires <device> <sensor>}"
    exec nats req "${device}.hal.${sensor}" "" \
        --server="${NATS_URL}" \
        --timeout=5s
}

cmd_set() {
    local device="${1:?set requires <device> <actuator> <value>}"
    local actuator="${2:?set requires <device> <actuator> <value>}"
    local value="${3:?set requires <device> <actuator> <value>}"
    exec nats req "${device}.hal.${actuator}.set" "${value}" \
        --server="${NATS_URL}" \
        --timeout=5s
}

cmd_gpio() {
    local device="${1:?gpio requires <device> <pin> get|set [value]}"
    local pin="${2:?gpio requires <device> <pin> get|set [value]}"
    local action="${3:?gpio requires <device> <pin> get|set [value]}"

    case "$action" in
        get)
            exec nats req "${device}.hal.gpio.${pin}.get" "" \
                --server="${NATS_URL}" \
                --timeout=5s
            ;;
        set)
            local value="${4:?gpio set requires a value (0 or 1)}"
            exec nats req "${device}.hal.gpio.${pin}.set" "${value}" \
                --server="${NATS_URL}" \
                --timeout=5s
            ;;
        *)
            echo "gpio action must be 'get' or 'set'"; usage ;;
    esac
}

cmd_adc() {
    local device="${1:?adc requires <device> <pin>}"
    local pin="${2:?adc requires <device> <pin>}"
    exec nats req "${device}.hal.adc.${pin}.read" "" \
        --server="${NATS_URL}" \
        --timeout=5s
}

cmd_pwm() {
    local device="${1:?pwm requires <device> <pin> <value>}"
    local pin="${2:?pwm requires <device> <pin> <value>}"
    local value="${3:?pwm requires <device> <pin> <value>}"
    exec nats req "${device}.hal.pwm.${pin}.set" "${value}" \
        --server="${NATS_URL}" \
        --timeout=5s
}

cmd_sub() {
    local device="${1:?sub requires <device>}"
    exec nats sub "${device}.events" \
        --server="${NATS_URL}"
}

# --- Main ---

if [ $# -lt 1 ]; then
    usage
fi

case "$1" in
    discover) shift; cmd_discover "$@" ;;
    caps)     shift; cmd_caps "$@" ;;
    read)     shift; cmd_read "$@" ;;
    set)      shift; cmd_set "$@" ;;
    gpio)     shift; cmd_gpio "$@" ;;
    adc)      shift; cmd_adc "$@" ;;
    pwm)      shift; cmd_pwm "$@" ;;
    sub)      shift; cmd_sub "$@" ;;
    -h|--help) usage ;;
    *)        echo "Unknown command: $1"; usage ;;
esac
