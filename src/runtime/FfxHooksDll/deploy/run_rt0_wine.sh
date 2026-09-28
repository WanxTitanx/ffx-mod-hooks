#!/usr/bin/env bash
# run_rt0_wine.sh — run a Windows-built harness exe (e.g. F8RuntimeRt0.exe)
# inside the game's Proton prefix, so Config/INI/flag-file resolution is
# exercised under Wine filesystem semantics without the game (plan §7 W2).
#
# Usage:
#   ./run_rt0_wine.sh <path-to-exe> [args...]
#   ./run_rt0_wine.sh --dry-run <path-to-exe> [args...]
#   ./run_rt0_wine.sh --prefix <custom-prefix> <exe> [args...]
#   ./run_rt0_wine.sh --proton "Proton 11.0" <exe> [args...]
#
# Defaults: Proton "Proton 10.0", WINEPREFIX = the game's compatdata prefix.

set -euo pipefail

readonly APPID=359870
readonly DEFAULT_PROTON="Proton 10.0"

proton="$DEFAULT_PROTON"
prefix=""
dry_run=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --proton)  proton="$2"; shift 2 ;;
        --prefix)  prefix="$2"; shift 2 ;;
        --dry-run) dry_run=1; shift ;;
        -h|--help) grep '^# ' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) break ;;
    esac
done

if [[ $# -lt 1 ]]; then
    echo "error: no exe given. See --help." >&2
    exit 2
fi
exe="$1"; shift
[[ -f "$exe" ]] || { echo "error: exe not found: $exe" >&2; exit 1; }

wine_bin=""
for lib in "$HOME/.steam/steam" "$HOME/.local/share/Steam"; do
    cand="$lib/steamapps/common/$proton/files/bin/wine"
    if [[ -x "$cand" ]]; then
        wine_bin="$cand"
        break
    fi
done
if [[ -z "$wine_bin" ]]; then
    if command -v wine >/dev/null 2>&1; then
        wine_bin="$(command -v wine)"
        echo "note: Proton wine not found, falling back to system wine: $wine_bin" >&2
    else
        echo "error: no wine binary (Proton '$proton' or system). Install Proton via Steam." >&2
        exit 1
    fi
fi

if [[ -z "$prefix" ]]; then
    for lib in "$HOME/.steam/steam" "$HOME/.local/share/Steam"; do
        cand="$lib/steamapps/compatdata/$APPID/pfx"
        if [[ -d "$cand" ]]; then
            prefix="$cand"
            break
        fi
    done
fi
if [[ -z "$prefix" || ! -d "$prefix" ]]; then
    echo "error: game prefix not found (compatdata/$APPID/pfx) and none given." >&2
    echo "       Run the game once (W0) so Steam creates the prefix, or pass --prefix." >&2
    exit 1
fi

# W2 is a lab: no DLL overrides needed, but pass through if the caller set them.
echo "wine     : $wine_bin"
echo "prefix   : $prefix"
echo "exe      : $exe"
echo "args     : $*"
if [[ $dry_run -eq 1 ]]; then
    echo "[dry-run] WINEPREFIX=\"$prefix\" \"$wine_bin\" \"$exe\" $*"
    exit 0
fi
export WINEPREFIX="$prefix"
exec "$wine_bin" "$exe" "$@"
