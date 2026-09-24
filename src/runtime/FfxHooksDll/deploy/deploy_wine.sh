#!/usr/bin/env bash
# deploy_wine.sh — deploy ffx-hooks.dll into the Linux (Proton) FFX install.
#
# Safety contract (docs/ai/WINE_PROTON_PORT_PLAN_2026-09-05.md §9):
#   - touches exactly one path: <game>/modules/ffx-hooks.dll
#   - refuses to run while FFX.exe is running
#   - verifies the game root by FFX.exe presence before writing
#   - prints SHA-256 of source and destination
#   - --uninstall removes only modules/ffx-hooks.dll
#
# Usage:
#   ./deploy_wine.sh [--artifact <ffx-hooks.dll>] [--game-root <dir>] [--uninstall] [--dry-run]
#
# Default artifact: ../bin/Release/ffx-hooks.dll (relative to this script).
# Default game root: auto-detected from Steam libraryfolders.vdf.

set -euo pipefail

readonly GAME_DIR_NAME="FINAL FANTASY FFX&FFX-2 HD Remaster"
readonly APPID=359870
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly DEFAULT_ARTIFACT="$SCRIPT_DIR/../bin/Release/ffx-hooks.dll"

artifact=""
game_root=""
mode="install"
dry_run=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --artifact)  artifact="$2"; shift 2 ;;
        --game-root) game_root="$2"; shift 2 ;;
        --uninstall) mode="uninstall"; shift ;;
        --dry-run)   dry_run=1; shift ;;
        -h|--help)   grep '^# ' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "error: unknown argument: $1" >&2; exit 2 ;;
    esac
done

# --- locate the game root ---------------------------------------------------
find_game_root() {
    local vdf="$HOME/.steam/steam/steamapps/libraryfolders.vdf"
    local -a libs=()
    [[ -f "$vdf" ]] && libs+=($(grep '"path"' "$vdf" | cut -d'"' -f4))
    [[ -d "$HOME/.local/share/Steam" ]] && libs+=("$HOME/.local/share/Steam")
    for lib in "${libs[@]}"; do
        local cand="$lib/steamapps/common/$GAME_DIR_NAME"
        if [[ -f "$cand/FFX.exe" ]]; then
            printf '%s' "$cand"
            return 0
        fi
    done
    return 1
}

if [[ -z "$game_root" ]]; then
    game_root="$(find_game_root)" || {
        echo "error: game root not found (no library has '$GAME_DIR_NAME/FFX.exe')." >&2
        echo "       Install the game via Steam (AppID $APPID) or pass --game-root." >&2
        exit 1
    }
fi

if [[ ! -f "$game_root/FFX.exe" ]]; then
    echo "error: '$game_root' does not contain FFX.exe — refusing." >&2
    exit 1
fi

target="$game_root/modules/ffx-hooks.dll"

# --- safety gates -----------------------------------------------------------
if pgrep -fi 'FFX\.exe' >/dev/null 2>&1; then
    echo "error: FFX.exe appears to be running — close the game before deploying." >&2
    exit 1
fi

run() {
    if [[ $dry_run -eq 1 ]]; then
        echo "[dry-run] $*"
    else
        "$@"
    fi
}

case "$mode" in
    uninstall)
        if [[ ! -f "$target" ]]; then
            echo "nothing to uninstall: $target absent."
            exit 0
        fi
        run rm -f -- "$target"
        echo "uninstalled: $target"
        echo "rollback note: also remove 'WINEDLLOVERRIDES=...dinput8...' launch options if W-line is being abandoned."
        ;;
    install)
        if [[ -z "$artifact" ]]; then
            artifact="$DEFAULT_ARTIFACT"
        fi
        if [[ ! -f "$artifact" ]]; then
            echo "error: artifact not found: $artifact" >&2
            echo "       Build on Windows (build_hooks.ps1 -WithPolyHook -Release) or pass --artifact." >&2
            exit 1
        fi
        if [[ ! -f "$game_root/dinput8.dll" ]]; then
            echo "warning: no dinput8.dll (FF10 Module Loader) in game root —" >&2
            echo "         our modules\\ffx-hooks.dll will not load without it." >&2
        fi
        mkdir_check=0
        if [[ ! -d "$game_root/modules" ]]; then
            run mkdir -p -- "$game_root/modules"
            mkdir_check=1
        fi
        run cp -f -- "$artifact" "$target"
        echo "deployed: $target"
        echo "source : $(realpath "$artifact")"
        if [[ $dry_run -eq 0 ]]; then
            echo "sha256 (source)      : $(sha256sum "$artifact" | cut -d' ' -f1)"
            echo "sha256 (destination) : $(sha256sum "$target" | cut -d' ' -f1)"
            [[ $mkdir_check -eq 1 ]] && echo "note: modules/ was created (it did not exist)."
        fi
        echo "next: launch with WINEDLLOVERRIDES=\"dinput8=n,b\" and watch tail_log_wine.sh"
        ;;
esac
