#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Runs a command against a throwaway GNOME Keyring on the current session bus
# (CI only; start it inside dbus-run-session). The keyring is created and
# unlocked with a fixed test password; nothing outside this session uses it.
# Before the command, a probe secret is stored, read and cleared with
# secret-tool, so a broken keyring setup fails here with its own message.
set -euo pipefail

printf '%s' 'ci-test-keyring' | gnome-keyring-daemon --unlock --components=secrets > /dev/null
dbus-send --session --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus \
    org.freedesktop.DBus.ListNames | grep -q 'org.freedesktop.secrets' \
    || { echo 'with_test_keyring: org.freedesktop.secrets is not on the session bus' >&2; exit 1; }
printf '%s' 'probe' | secret-tool store --label='send-airplay2 CI probe' sap2-ci probe
test "$(secret-tool lookup sap2-ci probe)" = 'probe' \
    || { echo 'with_test_keyring: probe secret did not round-trip' >&2; exit 1; }
secret-tool clear sap2-ci probe
echo 'with_test_keyring: Secret Service ready'
exec "$@"
