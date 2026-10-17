#!/bin/sh
# Reads hardware without changing it; writes only the report and its directory.
# Never invokes sync, poweroff, or a setter.
if [ "$#" -gt 1 ]; then
    printf 'Usage: %s [REPORT_FILE]\n' "$0" >&2
    exit 2
fi
case "${1-}" in
    -h|--help)
        printf 'Usage: %s [REPORT_FILE]\n' "$0"
        printf 'Default: /media/sdcard/epubreader/power-probe-YYYYMMDD-HHMMSS-PID.txt\n'
        exit 0
        ;;
esac

output=${1:-/media/sdcard/epubreader/power-probe-$(date '+%Y%m%d-%H%M%S')-$$.txt}
umask 077
if ! mkdir -p "$(dirname -- "$output")"; then
    printf 'Cannot create report directory for: %s\n' "$output" >&2
    exit 1
fi

report() {
    printf 'EPUB Reader M5.5 power probe (read-only hardware diagnostics)\n'
    printf 'REPORT FILE: %s\n' "$output"
    date
    uname -a
    id
    for supply in /sys/class/power_supply/*; do
        [ -d "$supply" ] || continue
        printf '\nSUPPLY %s -> %s\n' "$supply" "$(readlink -f "$supply")"
        ls -l "$supply/"
        for name in type scope present capacity capacity_level status online voltage_now voltage_min_design voltage_max_design current_now charge_now charge_full energy_now energy_full supplied_to; do
            [ -e "$supply/$name" ] || continue
            printf '%s=' "$name"
            cat "$supply/$name"
        done
    done
    printf '\nUnits: voltage_* uV, current_* uA, charge_* uAh, energy_* uWh (Linux power_supply ABI).\n'
    for executable in /sbin/poweroff /bin/poweroff /bin/sync /usr/bin/sync; do
        [ -e "$executable" ] || continue
        printf '\nEXECUTABLE %s -> %s\n' "$executable" "$(readlink -f "$executable")"
        ls -l "$executable" "$(readlink -f "$executable")"
        [ ! -x "$executable" ] || printf 'executable by current UID (does not prove shutdown authorization)\n'
    done
    printf '\nRun unplugged, connected, and full; record RG350 test readings beside this report.\n'
}

if report > "$output" 2>&1; then
    printf 'Power report saved to: %s\n' "$output"
else
    printf 'Could not write the complete power report to: %s\n' "$output" >&2
    exit 1
fi
