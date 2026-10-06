# SPDX-License-Identifier: Apache-2.0
"""Offline runner contracts: independent subprocess peer, never a hardware claim."""

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import run_e2e as runner


# Original public synthetic data. This deliberately does not import the runner's
# expected strings; a contract change must be reviewed rather than self-validating.
FAKE_CLI = r'''
import json, socket, sys
from pathlib import Path
state_path, log_path, mode = sys.argv[1:4]
arguments = sys.argv[4:]
command = arguments[0]
options = dict(zip(arguments[1::2], arguments[2::2])) if command != "discover" else {}
with open(log_path, "a") as log:
    log.write(json.dumps(arguments) + "\n")
state = json.loads(Path(state_path).read_text())
def finish(code, output="", error=""):
    if output: print(output)
    if error: print(error, file=sys.stderr)
    sys.exit(code)
if command == "discover":
    service = {"type":"_airplay._tcp.local.", "instance":"private-instance-marker",
               "interface_index":7, "port":7000, "addresses":["192.0.2.10"],
               "txt":{"pk":"private-key-marker"}}
    finish(0, json.dumps({"schema_version":1, "warnings":[], "devices":[
        {"id":"private-identity-marker", "services":[service]}]}))
profile = options["--profile"]
present = state.get(profile, False) or mode == "collision" and profile.startswith("e2e-missing-")
if command == "forget":
    state[profile] = False
    Path(state_path).write_text(json.dumps(state))
    finish(0, "Local credentials deleted; receiver pairing is unchanged." if present
           else "No local credentials exist for this profile.")
if command == "pair":
    if present:
        finish(1, error="Credentials: Profile already exists; use verify or explicitly forget before pairing again.")
    finish(2, error="Arguments: pair requires an interactive Windows console with hidden PIN input")
if not present:
    finish(1, error="Credentials: Stored credential is missing or malformed; no automatic re-pairing was attempted.")
if mode == "false-success": finish(0, "private-output-marker")
if options["--address"] == "127.0.0.1":
    try:
        with socket.create_connection(("127.0.0.1", int(options["--port"])),
                                      int(options["--timeout-ms"])/1000) as connection:
            connection.sendall(b"POST /pair-verify HTTP/1.1\r\nContent-Length: 0\r\n\r\n")
            connection.recv(1)
        category = 4
    except socket.timeout: category = 2
    except OSError: category = 5
    finish(1, error="Receiver: Receiver transport failed (category %d)" % category)
finish(0, "Peer verification succeeded; encrypted control transport established. Playback is not implemented.")
'''


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="airplay-e2e-contract-")
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.fake = root / "fake CLI with spaces.py"
        self.fake.write_text(FAKE_CLI, encoding="utf-8")
        self.state = root / "state.json"
        self.state.write_text(json.dumps({"primary": True, "disposable": True}))
        self.log = root / "calls.jsonl"
        self.report = root / "report.json"

    def options(self, *additional):
        return runner.parse_options(["--cli", sys.executable, "--address", "192.0.2.10",
                                     "--profile", "primary", "--repeat", "1",
                                     "--fault-timeout-ms", "200", "--discovery-ms", "1",
                                     "--report", str(self.report), *additional])

    def command(self, mode):
        return [sys.executable, str(self.fake), str(self.state), str(self.log), mode]

    def run_suite(self, mode="normal", *additional):
        suite = runner.Suite(self.command(mode), self.options(*additional))
        with contextlib.redirect_stdout(io.StringIO()):
            suite.run()
        return suite.results

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def test_complete_suite_and_loopback_faults(self):
        results = self.run_suite()
        self.assertTrue(all(case["status"] == "PASS" for case in results[:-1]), results)
        by_name = {case["case"]: case for case in results}
        self.assertEqual(by_name["fault-silent"]["exit_code"], 1)
        self.assertTrue(by_name["fault-silent"]["request_received"])
        self.assertGreaterEqual(by_name["fault-silent"]["elapsed_ms"], 100)
        self.assertTrue(by_name["fault-disconnect"]["connection_accepted"])
        self.assertEqual(by_name["forget-disposable-profile"]["status"], "SKIP")
        self.assertTrue(json.loads(self.state.read_text())["primary"])
        self.assertFalse(any(call[:3] == ["forget", "--profile", "primary"] for call in self.calls()))

    def test_exit_zero_without_checkpoint_is_failure_and_skips_profile_dependent_cases(self):
        results = self.run_suite("false-success")
        by_name = {case["case"]: case for case in results}
        self.assertEqual(by_name["stored-profile-verify"]["status"], "FAIL")
        self.assertEqual(by_name["existing-profile-refusal"]["status"], "SKIP")
        self.assertEqual(by_name["fault-silent"]["status"], "SKIP")
        self.assertTrue(any(call[0] == "pair" and call[2].startswith("e2e-missing-") for call in self.calls()))
        self.assertFalse(any(call[:3] == ["pair", "--profile", "primary"] for call in self.calls()))

    def test_missing_profile_collision_never_forgets_or_pairs_that_profile(self):
        results = self.run_suite("collision")
        by_name = {case["case"]: case for case in results}
        self.assertEqual(by_name["missing-profile-refusal"]["status"], "FAIL")
        self.assertEqual(by_name["forget-absent-profile"]["status"], "SKIP")
        self.assertFalse(any(call[0] in ("pair", "forget") and call[2].startswith("e2e-missing-")
                             for call in self.calls()))

    def test_disposable_profile_opt_in_and_idempotence(self):
        results = self.run_suite("normal", "--forget-disposable-profile", "disposable")
        self.assertTrue(all(case["status"] == "PASS" for case in results), results)
        state = json.loads(self.state.read_text())
        self.assertTrue(state["primary"])
        self.assertFalse(state["disposable"])
        self.assertEqual(sum(call == ["forget", "--profile", "disposable"] for call in self.calls()), 2)

    def test_report_and_console_never_include_raw_child_values(self):
        real_suite = runner.Suite
        with mock.patch.object(runner, "Suite", side_effect=lambda _, options:
                               real_suite(self.command("normal"), options)):
            console = io.StringIO()
            with contextlib.redirect_stdout(console):
                status = runner.main(["--cli", sys.executable, "--address", "192.0.2.10",
                                      "--profile", "primary", "--repeat", "1",
                                      "--fault-timeout-ms", "200", "--discovery-ms", "1",
                                      "--report", str(self.report)])
        self.assertEqual(status, 0)
        document = json.loads(self.report.read_text())
        self.assertTrue(document["success"])
        self.assertFalse(document["playback_tested"])
        content = self.report.read_text() + console.getvalue()
        for forbidden in ("private-", "192.0.2.10", "primary", "fake CLI"):
            self.assertNotIn(forbidden, content)
        original = self.report.read_bytes()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(runner.main(["--cli", sys.executable, "--address", "192.0.2.10",
                                          "--profile", "primary", "--report", str(self.report)]), 2)
        self.assertEqual(self.report.read_bytes(), original)

    def test_same_profile_deletion_and_invalid_endpoints_are_rejected_before_execution(self):
        for arguments in (("--forget-disposable-profile", "primary"), ("--scope-id", "7"),
                          ("--address", "fe80::1"), ("--address", "fe80::1%7"),
                          ("--repeat", "0"), ("--profile", "UPPERCASE")):
            with self.subTest(arguments=arguments), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    self.options(*arguments)
        self.assertFalse(self.log.exists())

    def test_infrastructure_failure_preserves_partial_report_and_hides_exception(self):
        real_suite = runner.Suite
        console = io.StringIO()
        with mock.patch.object(runner, "Suite", side_effect=lambda _, options:
                               real_suite(self.command("normal"), options)), \
                mock.patch.object(runner, "FaultEndpoint", side_effect=OSError("private-error-marker")), \
                contextlib.redirect_stdout(console):
            status = runner.main(["--cli", sys.executable, "--address", "192.0.2.10",
                                  "--profile", "primary", "--repeat", "1", "--skip-discovery",
                                  "--report", str(self.report)])
        self.assertEqual(status, 1)
        document = json.loads(self.report.read_text())
        self.assertFalse(document["success"])
        self.assertEqual(document["runner_error"], "infrastructure-failed")
        self.assertTrue(document["results"])
        self.assertNotIn("private-error-marker", self.report.read_text() + console.getvalue())

    def test_process_deadline_output_bound_and_launch_failure(self):
        timed = runner.invoke([sys.executable, "-c", "import time; time.sleep(30)"], 100)
        self.assertEqual(timed.issue, "runner-deadline")
        self.assertLess(timed.elapsed_ms, 2500)
        excess = runner.invoke([sys.executable, "-c", "print('x'*100000)"], 2500, output_limit=1000)
        self.assertEqual(excess.issue, "output-limit")
        self.assertLessEqual(len(excess.stdout), 1000)
        missing = runner.invoke([str(Path(self.directory.name) / "missing.exe")], 100)
        self.assertEqual(missing.issue, "launch-failed")
        self.assertIsNone(missing.exit_code)

    def test_discovery_duplicates_ambiguity_and_scoped_addresses(self):
        self.assertTrue(runner.address_matches("169.254.1.2", "169.254.1.2", 0))
        service = {"type": "_airplay._tcp.local.", "instance": "synthetic", "interface_index": 7,
                   "port": 7000, "addresses": ["fe80:0:0:0:0:0:0:1%7"]}
        device = {"id": "synthetic", "services": [service]}
        document = {"schema_version": 1, "devices": [device], "warnings": []}
        self.assertEqual(runner.discovery_identity(json.dumps(document), "fe80::1", 7000, 7),
                         ("synthetic", 1, 0))
        with self.assertRaises(ValueError):
            runner.discovery_identity(json.dumps(document), "fe80::1", 7000, 8)
        document["devices"] = [device, device]
        with self.assertRaises(ValueError):
            runner.discovery_identity(json.dumps(document), "fe80::1", 7000, 7)
        document["devices"] = [device, {"id": "other", "services": [service]}]
        with self.assertRaises(ValueError):
            runner.discovery_identity(json.dumps(document), "fe80::1", 7000, 7)
        document["devices"] = [{"id": "synthetic", "services": [service, service]}]
        with self.assertRaises(ValueError):
            runner.discovery_identity(json.dumps(document), "fe80::1", 7000, 7)

    def test_failure_reports_do_not_log_child_output_or_rejected_pin_arguments(self):
        real_suite = runner.Suite
        console = io.StringIO()
        with mock.patch.object(runner, "Suite", side_effect=lambda _, options:
                               real_suite(self.command("false-success"), options)):
            with contextlib.redirect_stdout(console):
                status = runner.main(["--cli", sys.executable, "--address", "192.0.2.10",
                                      "--profile", "primary", "--skip-discovery",
                                      "--repeat", "1", "--report", str(self.report)])
        self.assertEqual(status, 1)
        self.assertNotIn("private-output-marker", console.getvalue() + self.report.read_text())
        self.assertFalse(json.loads(self.report.read_text())["success"])
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors), self.assertRaises(SystemExit):
            self.options("--pin", "private-pin-marker-0123")
        self.assertNotIn("private-pin-marker-0123", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
