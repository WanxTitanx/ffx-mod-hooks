#!/usr/bin/env bash
# tail_log_wine.sh — locate and tail the ffx-hooks.log inside the Proton prefix.
#
# The DLL logs to %TEMP%\ffx-hooks.log; under Proton (AppID 359870) that maps to:
#   <library>/steamapps/compatdata/359870/pfx/drive_c/users/steamuser/
#       AppData/Local/Temp/ffx-hooks.log
#
# Usage:
#   ./tail_log_wine.sh [--find] [--list]
#   ./tail_log_wine.sh            # tail -f the first log found
#   --find                        # print resolved path(s), no tailing
#   --list                        # also list .oldN rotations

set -euo pipefail

readonly APPID=359870
readonly REL="steamapps/compatdata/$APPID/pfx/drive_c/users/steamuser/AppData/Local/Temp/ffx-hooks.log"

action="tail"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --find) action="find"; shift ;;
        --list) action="list"; shift ;;
        -h|--help) grep '^# ' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "error: unknown argument: $1" >&2; exit 2 ;;
    esac
done

declare -a found=()
collect() {
    local vdf="$HOME/.steam/steam/steamapps/libraryfolders.vdf"
    local -a libs=()
    [[ -f "$vdf" ]] && libs+=($(grep '"path"' "$vdf" | cut -d'"' -f4))
    [[ -d "$HOME/.local/share/Steam" ]] && libs+=("$HOME/.local/share/Steam")
    local lib log
    for lib in "${libs[@]}"; do
        log="$lib/$REL"
        if [[ -f "$log" ]]; then
            found+=("$log")
        fi
    done
}
collect

if [[ ${#found[@]} -eq 0 ]]; then
    echo "no ffx-hooks.log found in any Steam library prefix (AppID $APPID)." >&2
    echo "expected at: <lib>/$REL" >&2
    echo "if the game never ran with the DLL deployed, the log will not exist." >&2
    exit 1
fi

for log in "${found[@]}"; do
    case "$action" in
        find) echo "$log" ;;
        list)
            echo "$log"
            ls -l "${log%.log}".log* 2>/dev/null | sed 's/^/    /'
            ;;
        tail)
            echo "tailing: $log (Ctrl-C to stop)"
            tail -f "$log"
            ;;
    esac
done
