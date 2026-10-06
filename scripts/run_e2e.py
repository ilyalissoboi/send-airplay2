#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run noninteractive CLI E2E checks using an already paired Windows profile.

Python 3.10+, standard library only. Never enroll, copy credentials, collect a PIN,
or print/persist child output. Discovery identities exist only for in-memory
snapshot comparison. Fault endpoints bind loopback and discard received bytes.
"""

import argparse
import hashlib
import ipaddress
import json
import platform
import re
import socket
import subprocess
import sys
import threading
import time
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path


VERIFY_OK = ("Peer verification succeeded; encrypted control transport established. "
             "Playback is not implemented.")
EXISTS = "Credentials: Profile already exists; use verify or explicitly forget before pairing again."
MISSING = "Credentials: Stored credential is missing or malformed; no automatic re-pairing was attempted."
REDIRECTED = "Arguments: pair requires an interactive Windows console with hidden PIN input"
DELETED = "Local credentials deleted; receiver pairing is unchanged."
ABSENT = "No local credentials exist for this profile."
# Current private TransportError ordinals; a changed CLI contract must fail review.
TRANSPORT = "Receiver: Receiver transport failed (category {})"
OUTPUT_LIMIT = 8 * 1024 * 1024  # Per stream; never persist these transient buffers.
PROFILE = re.compile(r"[a-z0-9][a-z0-9._-]{0,63}\Z")


@dataclass
class ChildResult:
    exit_code: int | None
    elapsed_ms: int
    stdout: str = field(repr=False, default="")
    stderr: str = field(repr=False, default="")
    issue: str | None = None

    def matches(self, code: int, output: str = "", error: str = "") -> bool:
        """Require exact sanitized CLI checkpoints as well as the exit code."""
        return (self.issue is None and self.exit_code == code
                and self.stdout.strip() == output and self.stderr.strip() == error)


def invoke(command: list[str], timeout_ms: int, output_limit: int = OUTPUT_LIMIT) -> ChildResult:
    """Launch without a shell/PIN input; enforce wall-clock and output bounds.

    The production CLI is a leaf process. Kill/reap it on a runner deadline or
    excess output; this is a FAIL, never a passing native timeout observation.
    Dedicated readers drain both pipes, avoiding stdout/stderr pipe deadlock.
    """
    started = time.monotonic()
    try:
        child = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except OSError:
        return ChildResult(None, round((time.monotonic() - started) * 1000), issue="launch-failed")
    buffers = [bytearray(), bytearray()]
    overflow = threading.Event()

    def drain(stream, buffer):
        with stream:
            while chunk := stream.read(4096):
                if len(buffer) + len(chunk) > output_limit:
                    overflow.set()
                    child.kill()
                    return
                buffer.extend(chunk)

    readers = [threading.Thread(target=drain, args=(stream, buffer), daemon=True)
               for stream, buffer in zip((child.stdout, child.stderr), buffers)]
    for reader in readers:
        reader.start()
    issue = None
    try:
        child.wait(timeout=timeout_ms / 1000)
    except subprocess.TimeoutExpired:
        issue = "runner-deadline"
        child.kill()
        child.wait()
    for reader in readers:
        reader.join(timeout=1)
    if any(reader.is_alive() for reader in readers):
        issue = "pipe-not-closed"
    elif overflow.is_set():
        issue = "output-limit"
    try:
        stdout, stderr = (bytes(buffer).decode("utf-8") for buffer in buffers)
    except UnicodeDecodeError:
        stdout = stderr = ""
        issue = issue or "invalid-utf8"
    return ChildResult(child.returncode, round((time.monotonic() - started) * 1000),
                       stdout, stderr, issue)


class FaultEndpoint:
    """Own one ephemeral IPv4 loopback endpoint; never forward to the receiver.

    refused: select/release a closed port. silent: accept/drain, no response.
    disconnect: close after receiving request bytes. Thread errors are observations
    that fail the case, rather than being confused with successful fault injection.
    """

    def __init__(self, mode: str):
        self.mode = mode
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        self.listener.bind(("127.0.0.1", 0))
        self.port = self.listener.getsockname()[1]
        self.stop = threading.Event()
        self.accepted = False
        self.received = False
        self.failed = False
        self.thread = None

    def __enter__(self):
        if self.mode == "refused":
            # macOS may silently drop connects to a bound/non-listening socket.
            # Close it before connecting. The ephemeral port can theoretically be
            # claimed by another process; unexpected CLI outcomes fail the test.
            self.listener.close()
        else:
            self.listener.listen(1)
            self.listener.settimeout(0.1)
            self.thread = threading.Thread(target=self._serve, daemon=True)
            self.thread.start()
        return self

    def _serve(self):
        try:
            while not self.stop.is_set():
                try:
                    connection, _ = self.listener.accept()
                    break
                except socket.timeout:
                    continue
            else:
                return
            self.accepted = True
            with connection:
                connection.settimeout(0.1)
                while not self.stop.is_set():
                    try:
                        data = connection.recv(4096)
                    except socket.timeout:
                        continue
                    if not data:
                        return
                    self.received = True
                    if self.mode == "disconnect":
                        return
        except OSError:
            self.failed = not self.stop.is_set()

    def __exit__(self, *_):
        self.stop.set()
        if self.thread:
            self.thread.join(timeout=1)
            self.failed = self.failed or self.thread.is_alive()
        self.listener.close()


def address_matches(advertised: str, address: str, scope_id: int) -> bool:
    """Compare canonical addresses; link-local scope must match the chosen interface."""
    host, _, scope = advertised.partition("%")
    try:
        candidate = ipaddress.ip_address(host)
        expected = ipaddress.ip_address(address)
        return candidate == expected and (expected.version != 6 or not expected.is_link_local
                                          or scope == str(scope_id))
    except ValueError:
        return False


def discovery_identity(output: str, address: str, port: int, scope_id: int) -> tuple[str, int, int]:
    """Validate a snapshot and select exactly one target, retaining no report payloads.

    Friendly names are not trust sources. Discovery matching only checks that the
    requested endpoint is advertised; subsequent pinned-key verification supplies
    authentication. Unrelated LAN devices may appear/disappear between scans.
    """
    document = json.loads(output)
    if document["schema_version"] != 1 or not isinstance(document["devices"], list):
        raise ValueError("schema")
    identifiers = set()
    matches = []
    for device in document["devices"]:
        identifier = device["id"]
        if not isinstance(identifier, str) or not identifier or identifier in identifiers:
            raise ValueError("duplicate/invalid identity")
        identifiers.add(identifier)
        services = set()
        target = False
        for service in device["services"]:
            key = (service["type"], service["instance"], service["interface_index"])
            if key in services:
                raise ValueError("duplicate service")
            services.add(key)
            target = target or (service["type"] == "_airplay._tcp.local."
                                and service["port"] == port
                                and any(address_matches(value, address, scope_id)
                                        for value in service["addresses"]))
        if target:
            matches.append(identifier)
    if len(matches) != 1:
        raise ValueError("target absent/ambiguous")
    return matches[0], len(identifiers), len(document["warnings"])


class Suite:
    """Orchestrate separate CLI processes; reports contain only constructed facts."""

    def __init__(self, command: list[str], options):
        self.command = command
        self.options = options
        self.results = []

    def auth(self, action, profile=None, address=None, port=None, timeout=None):
        arguments = [action, "--profile", profile or self.options.profile]
        if action != "forget":
            arguments += ["--address", address or self.options.address,
                          "--port", str(port or self.options.port),
                          "--timeout-ms", str(timeout or self.options.timeout_ms)]
            if address is None and self.options.scope_id:
                arguments += ["--scope-id", str(self.options.scope_id)]
        return invoke(self.command + arguments,
                      (timeout or self.options.timeout_ms) + self.options.process_slack_ms)

    def record(self, name, kind, child=None, passed=False, reason="unexpected-cli-result", **facts):
        result = {"case": name, "evidence_kind": kind,
                  "status": "PASS" if passed else "FAIL",
                  "reason": "expected-result" if passed else (child.issue if child and child.issue else reason)}
        if child:
            result.update(exit_code=child.exit_code, elapsed_ms=child.elapsed_ms)
        result.update(facts)
        self.results.append(result)
        print(f"{result['status']} {name}: {result['reason']}", flush=True)
        return passed

    def skip(self, name, kind, reason):
        self.results.append({"case": name, "evidence_kind": kind, "status": "SKIP", "reason": reason})
        print(f"SKIP {name}: {reason}", flush=True)

    def verify(self, name, profile=None):
        child = self.auth("verify", profile=profile)
        return self.record(name, "live-receiver", child, child.matches(0, VERIFY_OK))

    def discovery(self):
        if self.options.skip_discovery:
            self.skip("discovery-consistency", "live-discovery", "disabled-by-option")
            return
        previous = None
        for index in range(2):
            child = invoke(self.command + ["discover", "--json", "--timeout-ms",
                                           str(self.options.discovery_ms)],
                           self.options.discovery_ms + self.options.process_slack_ms)
            identity = None
            count = warnings = 0
            try:
                if child.issue or child.exit_code != 0 or child.stderr.strip():
                    raise ValueError("CLI failure")
                identity, count, warnings = discovery_identity(
                    child.stdout, self.options.address, self.options.port, self.options.scope_id)
                passed = index == 0 or identity == previous
            except (ValueError, KeyError, TypeError):
                passed = False
            self.record(f"discovery-{index + 1}", "live-discovery", child, passed,
                        reason="invalid-or-inconsistent-discovery", device_count=count,
                        warning_count=warnings)
            previous = identity

    def fault(self, mode, categories):
        # Windows TCP can retry a rejected SYN for ~1 second. Give refusal enough
        # time to report a network error, independently of the short read deadline.
        timeout = max(self.options.fault_timeout_ms, 3000) if mode == "refused" else self.options.fault_timeout_ms
        with FaultEndpoint(mode) as endpoint:
            child = self.auth("verify", address="127.0.0.1", port=endpoint.port,
                              timeout=timeout)
        injected = not endpoint.failed and (mode == "refused" or endpoint.accepted and endpoint.received)
        expected = any(child.matches(1, error=TRANSPORT.format(code)) for code in categories)
        # A runner kill is never evidence that the CLI enforced its own deadline.
        waited = mode != "silent" or child.elapsed_ms >= self.options.fault_timeout_ms - 100
        self.record(f"fault-{mode}", "loopback-fault", child, injected and expected and waited,
                    reason="fault-or-native-failure-not-observed", connection_accepted=endpoint.accepted,
                    request_received=endpoint.received, native_deadline_ms=timeout)
        self.verify(f"recovery-after-{mode}")

    def run(self):
        self.discovery()
        baseline = self.verify("stored-profile-verify")
        for index in range(self.options.repeat):
            if baseline:
                self.verify(f"reconnect-{index + 1}")
            else:
                self.skip(f"reconnect-{index + 1}", "live-receiver", "baseline-failed")
        if baseline:
            child = self.auth("pair")
            self.record("existing-profile-refusal", "local-cli", child, child.matches(1, error=EXISTS))
        else:
            self.skip("existing-profile-refusal", "local-cli", "baseline-failed")
        unused = "e2e-missing-" + uuid.uuid4().hex
        child = self.auth("verify", profile=unused)
        absent = self.record("missing-profile-refusal", "local-cli", child, child.matches(1, error=MISSING))
        if absent:
            child = self.auth("pair", profile=unused)
            self.record("redirected-pin-refusal", "local-cli", child, child.matches(2, error=REDIRECTED))
            child = self.auth("forget", profile=unused)
            self.record("forget-absent-profile", "local-cli", child, child.matches(0, ABSENT))
        else:
            for name in ("redirected-pin-refusal", "forget-absent-profile"):
                self.skip(name, "local-cli", "absence-not-confirmed")
        for mode, categories in (("refused", (5,)), ("silent", (2,)), ("disconnect", (4, 5))):
            if baseline:
                self.fault(mode, categories)
            else:
                self.skip(f"fault-{mode}", "loopback-fault", "baseline-failed")
                self.skip(f"recovery-after-{mode}", "live-receiver", "baseline-failed")
        disposable = self.options.forget_disposable_profile
        if disposable:
            if self.verify("disposable-profile-verify", profile=disposable):
                child = self.auth("forget", profile=disposable)
                deleted = self.record("forget-disposable-profile", "local-cli", child, child.matches(0, DELETED))
                if deleted:
                    child = self.auth("verify", profile=disposable)
                    self.record("forgotten-profile-refusal", "local-cli", child, child.matches(1, error=MISSING))
                    child = self.auth("forget", profile=disposable)
                    self.record("forget-idempotent", "local-cli", child, child.matches(0, ABSENT))
                else:
                    for name in ("forgotten-profile-refusal", "forget-idempotent"):
                        self.skip(name, "local-cli", "deletion-not-confirmed")
            else:
                for name in ("forget-disposable-profile", "forgotten-profile-refusal", "forget-idempotent"):
                    self.skip(name, "local-cli", "disposable-verification-failed")
        else:
            self.skip("forget-disposable-profile", "local-cli", "not-opted-in")
        return self.results


def bounded_int(minimum, maximum):
    def parse(value):
        number = int(value)
        if not minimum <= number <= maximum:
            raise argparse.ArgumentTypeError(f"must be {minimum}..{maximum}")
        return number
    return parse


def file_sha256(path):
    """Fingerprint tested code without including local paths or file contents."""
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_options(arguments=None):
    class SanitizedParser(argparse.ArgumentParser):
        def error(self, _message):
            # argparse normally echoes rejected values, potentially a mistaken PIN.
            self.print_usage(sys.stderr)
            self.exit(2, "Invalid runner arguments; consult --help. No argument values were logged.\n")

    parser = SanitizedParser(description=__doc__)
    parser.add_argument("--cli", required=True, type=Path)
    parser.add_argument("--address", required=True, help="Numeric receiver IP; IPv6 scope is separate")
    parser.add_argument("--port", type=bounded_int(1, 65535), default=7000)
    parser.add_argument("--scope-id", type=bounded_int(0, 2**32 - 1), default=0)
    parser.add_argument("--profile", required=True, help="Existing paired Windows profile; never deleted")
    parser.add_argument("--repeat", type=bounded_int(1, 100), default=3)
    parser.add_argument("--timeout-ms", type=bounded_int(1, 60000), default=10000)
    parser.add_argument("--fault-timeout-ms", type=bounded_int(200, 60000), default=500)
    parser.add_argument("--process-slack-ms", type=bounded_int(100, 10000), default=2500)
    parser.add_argument("--discovery-ms", type=bounded_int(1, 60000), default=5000)
    parser.add_argument("--skip-discovery", action="store_true")
    parser.add_argument("--forget-disposable-profile", help="OPT-IN: delete a separately paired disposable profile")
    parser.add_argument("--report", type=Path, help="New JSON file; existing files are never overwritten")
    options = parser.parse_args(arguments)
    try:
        address = ipaddress.ip_address(options.address)
    except ValueError:
        parser.error("--address must be a numeric IP")
    if "%" in options.address or (address.version == 4 and options.scope_id):
        parser.error("use a separate --scope-id with IPv6 only")
    if address.version == 6 and address.is_link_local and not options.scope_id:
        parser.error("link-local IPv6 requires --scope-id")
    for profile in (options.profile, options.forget_disposable_profile):
        if profile is not None and not PROFILE.fullmatch(profile):
            parser.error("profiles require 1..64 lowercase letters/digits/._-, starting with a letter/digit")
    if options.profile == options.forget_disposable_profile:
        parser.error("the disposable profile must differ from the primary profile")
    options.cli = options.cli.resolve()
    if not options.cli.is_file():
        parser.error("CLI executable does not exist")
    if options.report is None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        options.report = Path("build-e2e") / f"report-{stamp}-{uuid.uuid4().hex[:8]}.json"
    return options


def main(arguments=None):
    options = parse_options(arguments)
    # Reserve output before running checks; exclusive creation avoids data loss.
    try:
        options.report.parent.mkdir(parents=True, exist_ok=True)
        with options.report.open("x", encoding="utf-8") as report:
            document = {"schema_version": 1, "started_at_utc": datetime.now(timezone.utc).isoformat(),
                        "cli_sha256": file_sha256(options.cli), "runner_sha256": file_sha256(Path(__file__)),
                        "host": {"system": platform.system(),
                        "version": platform.version(), "machine": platform.machine()},
                        "receiver": {"address_family": ipaddress.ip_address(options.address).version,
                                     "port": options.port, "scope_used": bool(options.scope_id)},
                        "playback_tested": False, "results": []}
            suite = Suite([str(options.cli)], options)
            document["results"] = suite.results
            try:
                suite.run()
                document["success"] = all(item["status"] != "FAIL" for item in document["results"])
            except Exception:
                # Never serialize an exception that could contain receiver/child data.
                document.update(success=False, runner_error="infrastructure-failed")
                print("FAIL runner: infrastructure-failed", flush=True)
            json.dump(document, report, indent=2)
            report.write("\n")
        print("Report written; child output and LAN identifiers were omitted.")
        return 0 if document["success"] else 1
    except OSError:
        print("Runner configuration/report I/O failed; no child output was logged.")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
