#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Runs a command against a throwaway GNOME Keyring on the current session bus
# (CI only; start it inside dbus-run-session). The keyring is created and
# unlocked with a fixed test password; nothing outside this session uses it.
# Before the command, a probe secret is stored, read and cleared with
# secret-tool, so a broken keyring setup fails here with its own message.
set -euo pipefail

# The store's save/erase lock lives in $XDG_RUNTIME_DIR, which desktop sessions
# provide and CI runners may not; give this session a private one if missing.
if [ -n "${XDG_RUNTIME_DIR:-}" ] && [ -d "$XDG_RUNTIME_DIR" ] && [ -w "$XDG_RUNTIME_DIR" ]; then
    echo 'with_test_keyring: XDG_RUNTIME_DIR is a writable directory'
else
    if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
        echo 'with_test_keyring: XDG_RUNTIME_DIR was unset'
    else
        echo 'with_test_keyring: XDG_RUNTIME_DIR is set but missing or not writable'
    fi
    XDG_RUNTIME_DIR="$(mktemp -d)"
    chmod 700 "$XDG_RUNTIME_DIR"
    export XDG_RUNTIME_DIR
    echo 'with_test_keyring: using a private temporary directory instead'
fi

# --unlock starts the daemon and creates/unlocks the login keyring; --start then
# initializes the Secret Service component in that running daemon, which is
# what registers org.freedesktop.secrets on the bus.
printf '%s' 'ci-test-keyring' | gnome-keyring-daemon --unlock > /dev/null
gnome-keyring-daemon --start --components=secrets > /dev/null

secrets_on_bus() {
    dbus-send --session --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus \
        org.freedesktop.DBus.ListNames | grep -q 'org.freedesktop.secrets'
}
for _ in $(seq 1 50); do # Up to about five seconds for the name to appear.
    secrets_on_bus && break
    sleep 0.1
done
secrets_on_bus \
    || { echo 'with_test_keyring: org.freedesktop.secrets is not on the session bus' >&2; exit 1; }

printf '%s' 'probe' | secret-tool store --label='send-airplay2 CI probe' sap2-ci probe
test "$(secret-tool lookup sap2-ci probe)" = 'probe' \
    || { echo 'with_test_keyring: probe secret did not round-trip' >&2; exit 1; }
secret-tool clear sap2-ci probe
echo 'with_test_keyring: Secret Service ready'
exec "$@"
