#!/bin/bash
# One case of `pixi run test-linux-probe`, inside the container scripts/test-linux-probe.mjs starts
# with no network, a read-only root, and tmpfs at /tmp (prefixes, game folders, the copied build)
# and /home. It sets the case up, runs the probes, and prints tab-separated lines for the host to
# check: `step` before each probe run, the probe's own lines, `exit` with its exit code, `stderr`
# with what it wrote there, `snap` for each file and folder under the roots a snapshot names, and
# `game-line` for each line of a game file a case prints.
#
#   run-case.sh versions
#   run-case.sh wine <case> <cpp|net35|net472>
#   run-case.sh native <legacy|canonical|nothing> <placement>
set -euo pipefail

HOME_DIR='/home/jösé-日本'
PREFIX_ROAMING='drive_c/users/probe/AppData/Roaming'
SNAPSHOT_PRUNE=()

# Run from Docker Desktop's bind mount of the Windows host, the C++ test binary faulted at its
# entry point under Wine; from tmpfs it runs. So every binary runs from a tmpfs copy.
copy_build() {
    mkdir /tmp/bin
    cp /probe/cpp/cameraunlock_tests.exe /tmp/bin/
    cp -r /probe/net35 /probe/net472 /tmp/bin/
}

exe_for() {
    case "$1" in
        cpp) echo /tmp/bin/cameraunlock_tests.exe ;;
        net35 | net472) echo "/tmp/bin/$1/CameraUnlock.Core.FrameworkTests.exe" ;;
        *) echo "unknown runtime $1" >&2; exit 2 ;;
    esac
}

new_prefix() {
    cp -a /opt/prefix "$1"
    mkdir "$1/drive_c/game"
}

finish_step() {
    printf 'exit\t%s\n' "$1"
    sed 's/^/stderr\t/' /tmp/stderr
}

wine_probe() {
    local label=$1 prefix=$2 runtime=$3
    shift 3
    printf 'step\t%s\n' "$label"
    local code=0
    WINEPREFIX=$prefix /usr/lib/wine/wine64 "$(exe_for "$runtime")" --probe-defaults-ini 'C:\game' "$@" 2>/tmp/stderr || code=$?
    WINEPREFIX=$prefix /usr/lib/wine/wineserver64 --wait
    finish_step "$code"
}

native_probe() {
    local label=$1 build=$2
    shift 2
    printf 'step\t%s\n' "$label"
    local code=0
    mono "$(exe_for "$build")" --probe-defaults-ini /tmp/game "$@" 2>/tmp/stderr || code=$?
    finish_step "$code"
}

snapshot() {
    local label=$1
    shift
    local root path sha
    for root in "$@"; do
        if [ ! -e "$root" ]; then
            printf 'snap\t%s\tabsent\t%s\n' "$label" "$root"
            continue
        fi
        while IFS= read -r -d '' path; do
            if [ -f "$path" ]; then sha=$(sha256sum "$path" | cut -d' ' -f1); else sha=folder; fi
            printf 'snap\t%s\t%s\t%s\t%s\n' "$label" "$path" "$sha" "$(stat -c '%y %a' "$path")"
        done < <(find "$root" "${SNAPSHOT_PRUNE[@]}" \( -type f -o -type d \) -print0 | sort -z)
    done
}

defaults_file() {
    mkdir -p "$1"
    printf '[Network]\r\nUdpPort=%s\r\n' "$2" > "$1/Defaults.ini"
}

legacy_file() {
    printf '; tuned by hand\r\n[General]\r\nPort = %s\r\nYawWorld = false\r\nSmoothng = 0.3\r\n[Position]\r\nPosition = false\r\n' \
        "$2" > "$1/HeadTracking.ini"
}

run_wine() {
    local case_name=$1 runtime=$2
    local a=/tmp/prefix-a b=/tmp/prefix-b
    SNAPSHOT_PRUNE=(-path "$HOME_DIR/.cache" -prune -o)
    export HOME=$HOME_DIR
    mkdir "$HOME"
    unset XDG_CONFIG_HOME
    new_prefix "$a"
    local roots=("$HOME" "$a/$PREFIX_ROAMING" "$a/drive_c/game")
    case "$case_name" in
        migrate)
            mkdir -p "$HOME/.config/CameraUnlock"
            printf '[Network]\r\nUdpPort=5151\r\n[Hotkeys]\r\nToggleKey=F8\r\n' > "$HOME/.config/CameraUnlock/Defaults.ini"
            legacy_file "$a/drive_c/game" 5151
            snapshot before "${roots[@]}"
            wine_probe migrate "$a" "$runtime" --probe-legacy
            snapshot migrate "${roots[@]}"
            sed 's/\r$//; s/^/game-line\t/' "$a/drive_c/game/CameraUnlock.ini"
            ;;
        home)
            mkdir "$HOME/.config"
            wine_probe create "$a" "$runtime"
            snapshot create "${roots[@]}"
            wine_probe save "$a" "$runtime" --probe-save
            snapshot save "${roots[@]}"
            ;;
        xdg)
            mkdir -p "$HOME/.config" "$HOME/.var/app/com.valvesoftware.Steam/config"
            export XDG_CONFIG_HOME="$HOME/.var/app/com.valvesoftware.Steam/config"
            wine_probe create "$a" "$runtime"
            snapshot create "${roots[@]}"
            ;;
        xdg-relative)
            mkdir "$HOME/.config"
            export XDG_CONFIG_HOME=relative/config
            wine_probe create "$a" "$runtime"
            snapshot create "${roots[@]}"
            ;;
        xdg-missing-parent)
            mkdir "$HOME/.config"
            export XDG_CONFIG_HOME="$HOME/missing/config"
            # Wine's menu builder creates $XDG_CONFIG_HOME/menus when a session starts, which would
            # give the folder its parent before the probe runs; xdg-missing-parent-menu-builder
            # runs with it.
            export WINEDLLOVERRIDES=winemenubuilder.exe=d
            wine_probe create "$a" "$runtime"
            snapshot create "${roots[@]}"
            ;;
        xdg-missing-parent-menu-builder)
            mkdir "$HOME/.config"
            export XDG_CONFIG_HOME="$HOME/missing/config"
            wine_probe create "$a" "$runtime"
            snapshot create "${roots[@]}"
            ;;
        host-unwritable)
            mkdir "$HOME/.config"
            chmod 555 "$HOME/.config"
            wine_probe create "$a" "$runtime"
            snapshot create "${roots[@]}"
            ;;
        two-prefixes)
            mkdir "$HOME/.config"
            new_prefix "$b"
            wine_probe first "$a" "$runtime"
            snapshot first "${roots[@]}" "$b/$PREFIX_ROAMING" "$b/drive_c/game"
            wine_probe second "$b" "$runtime"
            snapshot second "${roots[@]}" "$b/$PREFIX_ROAMING" "$b/drive_c/game"
            ;;
        prefix-then-host)
            # An unwritable ~/.config, not a missing one, keeps the first start off the host: the
            # menu builder creates ~/.config/menus when the session starts.
            mkdir "$HOME/.config"
            chmod 555 "$HOME/.config"
            wine_probe prefix "$a" "$runtime"
            snapshot prefix "${roots[@]}"
            chmod 755 "$HOME/.config"
            defaults_file "$HOME/.config/CameraUnlock" 5151
            wine_probe host "$a" "$runtime"
            snapshot host "${roots[@]}"
            ;;
        *)
            echo "unknown Wine case $case_name" >&2
            exit 2
            ;;
    esac
}

run_native() {
    local state=$1 placement=$2
    local game=/tmp/game xdg=
    mkdir "$HOME_DIR" "$game"
    export HOME=$HOME_DIR
    unset XDG_CONFIG_HOME
    case "$placement" in
        xdg)
            xdg="$HOME/xdg"
            defaults_file "$xdg/CameraUnlock" 5151
            ;;
        config)
            defaults_file "$HOME/.config/CameraUnlock" 5151
            ;;
        library)
            defaults_file "$HOME/Library/Application Support/CameraUnlock" 5151
            ;;
        xdg-and-config)
            xdg="$HOME/xdg"
            defaults_file "$xdg/CameraUnlock" 5151
            defaults_file "$HOME/.config/CameraUnlock" 6262
            ;;
        config-and-library)
            defaults_file "$HOME/.config/CameraUnlock" 5151
            defaults_file "$HOME/Library/Application Support/CameraUnlock" 6262
            ;;
        none) ;;
        home-unset)
            defaults_file "$HOME/.config/CameraUnlock" 6262
            ;;
        home-unset-xdg)
            xdg=/tmp/xdg
            defaults_file "$xdg/CameraUnlock" 5151
            ;;
        *)
            echo "unknown placement $placement" >&2
            exit 2
            ;;
    esac
    [ -n "$xdg" ] && export XDG_CONFIG_HOME=$xdg

    local flags=(--probe-save)
    case "$state" in
        legacy)
            legacy_file "$game" 5555
            flags+=(--probe-legacy)
            ;;
        canonical)
            cp /probe/example/CameraUnlock.ini "$game/CameraUnlock.ini"
            ;;
        nothing) ;;
        *)
            echo "unknown state $state" >&2
            exit 2
            ;;
    esac
    find "$game" -exec touch -d '2020-01-02 03:04:05' {} +

    local roots=("$HOME_DIR" "$game")
    [ "$xdg" = /tmp/xdg ] && roots+=("$xdg")
    [ "${placement#home-unset}" != "$placement" ] && unset HOME
    snapshot before "${roots[@]}"
    for build in net35 net472; do
        native_probe "$build" "$build" "${flags[@]}"
        snapshot "$build" "${roots[@]}"
    done
}

case "${1:-}" in
    versions)
        printf 'version\twine\t%s\n' "$(/usr/lib/wine/wine64 --version)"
        printf 'version\tmono\t%s\n' "$(mono --version | head -n 1)"
        printf 'version\twine-mono\t%s\n' "$(ls /usr/share/wine/mono)"
        printf 'version\tdebian\t%s\n' "$(cat /etc/debian_version)"
        dpkg-query -W -f 'version\tpackage\t${Package} ${Version}\n' wine wine64 libwine mono-runtime libmono-corlib4.5-dll libmono-system-core4.0-cil
        ;;
    wine)
        copy_build
        run_wine "$2" "$3"
        ;;
    native)
        copy_build
        run_native "$2" "$3"
        ;;
    *)
        echo "usage: run-case.sh versions | wine <case> <runtime> | native <state> <placement>" >&2
        exit 2
        ;;
esac
