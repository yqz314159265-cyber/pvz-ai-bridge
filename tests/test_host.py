import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
import threading
import unittest
from unittest import mock

import pvz_ai.host as host_module
from pvz_ai.host import (Host, backup_saves, discover_game, find_saves,
                         _launcher_identity_matches, _stage_bridge_dll, _wait_for_game_process)
from pvz_ai.pipe import BridgeError, PipeClient, _connect_pipe, _read_line, _write_all
from pvz_ai.server import build_mcp_server, mutation_auth_allowed
from pvz_ai.templates import GuardedPlan


class HostTests(unittest.TestCase):
    def test_launcher_version_and_hash_must_match_exactly(self):
        self.assertTrue(_launcher_identity_matches(
            "Plants vs. Zombies", host_module.EXPECTED_VERSION,
            host_module.EXPECTED_LAUNCHER_SHA256.upper()))
        self.assertFalse(_launcher_identity_matches(
            "Plants vs. Zombies", host_module.EXPECTED_VERSION + ".1",
            host_module.EXPECTED_LAUNCHER_SHA256))

    def test_levels_merge_static_catalog_and_native_entry_flags(self):
        host = Host()
        class FakePipe:
            def request(self, op, args=None):
                assert op == "levels"
                return [{"mode_id": 0, "entry_implemented": True, "ai_win_verified": False},
                        {"mode_id": 13, "entry_implemented": False, "ai_win_verified": False}]
        host._pipe = FakePipe()
        levels = host.request("levels")
        self.assertEqual(len(levels), 73)
        self.assertEqual(levels[0]["native_support"], "entry_implemented")
        self.assertFalse(levels[0]["ai_win_verified"])
        self.assertTrue(levels[13]["native_status_known"])
        self.assertFalse(levels[13]["entry_implemented"])
        self.assertFalse(levels[43]["native_status_known"])
        self.assertEqual(levels[43]["native_support"], "not_reported")
        self.assertFalse(_launcher_identity_matches(
            "Plants vs. Zombies", host_module.EXPECTED_VERSION, "0" * 64))

    def test_staged_bridge_copy_is_unique_and_hash_verified(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as td:
            root = Path(td); source = root / "pvz_bridge.dll"
            source.write_bytes(b"verified bridge fixture")
            loaded = root / "loaded"
            first, first_hash = _stage_bridge_dll(source, loaded)
            second, second_hash = _stage_bridge_dll(source, loaded)
            expected = hashlib.sha256(source.read_bytes()).hexdigest()
            self.assertNotEqual(first, second)
            self.assertIn(expected[:16], first.name)
            self.assertEqual(first_hash, expected)
            self.assertEqual(second_hash, expected)
            self.assertEqual(first.read_bytes(), source.read_bytes())
            self.assertEqual(second.read_bytes(), source.read_bytes())

    def test_attach_reuses_ready_bridge_without_staging_or_injection(self):
        host = Host()
        hello = {"version": host_module.EXPECTED_VERSION, "ready": True}
        with mock.patch.object(host, "_verify_target_process"), \
             mock.patch.object(host, "connect", return_value=hello), \
             mock.patch.object(host_module, "_stage_bridge_dll") as stage, \
             mock.patch.object(host_module.subprocess, "run") as run:
            self.assertEqual(host.attach(123), hello)
        stage.assert_not_called()
        run.assert_not_called()

    def test_attach_injects_hash_verified_unique_staged_copy(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as td:
            root = Path(td); native = root / "Release"; native.mkdir()
            injector = native / "pvz_injector.exe"; injector.write_bytes(b"injector fixture")
            dll = native / "pvz_bridge.dll"; dll.write_bytes(b"bridge fixture")
            host = Host(native_dir=native)
            hello = {"version": host_module.EXPECTED_VERSION, "ready": True}
            connect_results = [BridgeError("unavailable", "no existing pipe"), hello]
            with mock.patch.object(host, "_verify_target_process"), \
                 mock.patch.object(host, "connect", side_effect=connect_results), \
                 mock.patch.object(host, "_acquire_lease"), \
                 mock.patch.object(host, "_release_lease"), \
                 mock.patch.object(host_module.subprocess, "run") as run:
                self.assertEqual(host.attach(123), hello)
            args = run.call_args.args[0]
            staged = Path(args[args.index("--dll") + 1])
            self.assertEqual(staged.parent, root / "loaded")
            self.assertNotEqual(staged, dll)
            self.assertEqual(hashlib.sha256(staged.read_bytes()).digest(),
                             hashlib.sha256(dll.read_bytes()).digest())

    def test_host_plan_stops_when_tick_expires(self):
        class FakePipe:
            def __init__(self): self.actions = []; self.tick = 1
            def request(self, op, args=None):
                if op == "state":
                    self.tick += 1
                    return {"tick": self.tick, "mode_id": "adventure", "paused": False,
                            "capabilities": ["action.start_level", "action.continue"]}
                if op == "action": self.actions.append(args); return {"accepted": True}
                return {}
        host = Host(); fake = FakePipe(); host._pipe = fake
        done = host.submit_plan([{"type": "start_level"}, {"type": "continue"}], 2, "adventure")
        self.assertEqual(len(done), 1)
        self.assertEqual([x["type"] for x in fake.actions], ["start_level"])

    def test_backup_fixture_writes_hash_manifest(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as td:
            root = Path(td); src = root / "userdata"; src.mkdir()
            data = b"fixture save bytes"; (src / "user1.dat").write_bytes(data)
            out = root / "backup"
            manifest = backup_saves(src, out)
            self.assertEqual(manifest["files"][0]["sha256"], hashlib.sha256(data).hexdigest())
            saved = json.loads((out / "manifest.json").read_text())
            self.assertEqual((out / "user1.dat").read_bytes(), data)
            self.assertEqual(saved["source"], str(src.resolve()))

    def test_backup_rejects_copied_hash_mismatch(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as td:
            root = Path(td); src = root / "userdata"; src.mkdir()
            (src / "user1.dat").write_bytes(b"original")
            out = root / "backup"
            real_copy = host_module.shutil.copy2
            def corrupt_copy(source, destination):
                real_copy(source, destination)
                Path(destination).write_bytes(b"corrupt")
            with mock.patch.object(host_module.shutil, "copy2", side_effect=corrupt_copy):
                with self.assertRaisesRegex(RuntimeError, "hashes do not match"):
                    backup_saves(src, out)
            self.assertFalse(out.exists())

    def test_discovers_steam_userid_save_root(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as td:
            steam = Path(td) / "steam"
            save = steam / "userdata" / "123456789" / "3590"
            save.mkdir(parents=True); (save / "user1.dat").write_bytes(b"save")
            with mock.patch.object(host_module, "SAVE_DIRS", []), mock.patch.object(host_module, "_steam_roots", return_value=[steam]):
                self.assertEqual(find_saves(Path(td) / "PlantsVsZombies.exe"), save)

    def test_discovers_programdata_userdata_backup_root(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as td:
            save = Path(td) / "Steam" / "PlantsVsZombies" / "userdata_backup"
            save.mkdir(parents=True); (save / "user1.dat").write_bytes(b"backup")
            with mock.patch.object(host_module, "SAVE_DIRS", [save]), mock.patch.object(host_module, "_steam_roots", return_value=[]):
                self.assertEqual(find_saves(), save)

    def test_doctor_is_non_mutating(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as td:
            game = Path(td) / "missing.exe"
            before = list(Path(td).iterdir())
            result = Host(game_path=game, native_dir=Path(td) / "native").doctor()
            self.assertFalse(result["game"]["found"])
            self.assertEqual(before, list(Path(td).iterdir()))

    def test_pipe_refuses_unsupported_platform_without_connecting(self):
        if os.name != "nt":
            with self.assertRaises(BridgeError) as ctx:
                PipeClient(123).request("state")
            self.assertEqual(ctx.exception.code, "platform_unsupported")

    def test_http_mutation_requires_loopback_and_bearer(self):
        self.assertFalse(mutation_auth_allowed("127.0.0.1", "", "secret"))
        self.assertFalse(mutation_auth_allowed("192.0.2.3", "Bearer secret", "secret"))
        self.assertTrue(mutation_auth_allowed("127.0.0.1", "Bearer secret", "secret"))

    def test_mcp_sdk_registers_real_tools(self):
        server = build_mcp_server(Host())
        names = {tool.name for tool in server._tool_manager.list_tools()}
        self.assertTrue({"status", "state", "levels", "capabilities", "action", "pause", "resume",
                         "launch", "attach", "stop", "detach", "run_template"}.issubset(names))

    def test_pipe_line_reader_handles_partial_reads(self):
        chunks = iter((b'{"ok":', b'true}\n'))
        self.assertEqual(_read_line(lambda size: next(chunks), deadline=10,
                                    clock=lambda: 0), b'{"ok":true}')

    def test_pipe_line_reader_times_out_on_nonblocking_empty_reads(self):
        now = [0.0]
        def sleep(delta): now[0] += delta
        with self.assertRaises(BridgeError) as caught:
            _read_line(lambda size: (_ for _ in ()).throw(BlockingIOError()), deadline=0.05,
                       clock=lambda: now[0], sleep=sleep)
        self.assertEqual(caught.exception.code, "timeout")

    def test_pipe_writer_handles_partial_writes_and_timeout(self):
        received = bytearray()
        def write(data):
            part = data[:2]; received.extend(part); return len(part)
        _write_all(write, b"abcdef", deadline=10, clock=lambda: 0)
        self.assertEqual(received, b"abcdef")
        now = [0.0]
        def sleep(delta): now[0] += delta
        with self.assertRaises(BridgeError) as caught:
            _write_all(lambda data: (_ for _ in ()).throw(BlockingIOError()), b"x",
                       deadline=0.05, clock=lambda: now[0], sleep=sleep)
        self.assertEqual(caught.exception.code, "timeout")

    def test_pipe_connect_retries_between_native_instances(self):
        now = [0.0]
        waits = iter((2, 0, 0))  # brief no-instance gap, then CreateFile race, then ready
        opens = iter(((None, 231), ("pipe-handle", 0)))
        def sleep(delta): now[0] += delta
        result = _connect_pipe(lambda ms: next(waits), lambda: next(opens), 1.0,
                               clock=lambda: now[0], sleep=sleep)
        self.assertEqual(result, "pipe-handle")

    def test_pipe_connect_has_a_bounded_deadline(self):
        now = [0.0]
        def sleep(delta): now[0] += delta
        with self.assertRaises(BridgeError) as caught:
            _connect_pipe(lambda ms: 2, lambda: (None, 0), 0.025,
                          clock=lambda: now[0], sleep=sleep)
        self.assertEqual(caught.exception.code, "unavailable")

    def test_pipe_client_serializes_concurrent_requests(self):
        class FakeClient(PipeClient):
            def __init__(self):
                super().__init__(1); self.active = 0; self.peak = 0
            def _request_windows(self, op, args):
                self.active += 1; self.peak = max(self.peak, self.active)
                time.sleep(0.01); self.active -= 1
                return {"op": op}
        client = FakeClient()
        threads = [threading.Thread(target=client.request, args=(str(i),)) for i in range(6)]
        [t.start() for t in threads]; [t.join() for t in threads]
        self.assertEqual(client.peak, 1)

    def test_attach_rejects_unrelated_process_before_injection(self):
        with mock.patch.object(host_module, "_process_image_path", return_value=Path(r"C:\Other\not-game.exe")):
            with self.assertRaisesRegex(RuntimeError, "verified PopCap runtime path"):
                Host().attach(123)

    def test_connect_rejects_native_hello_version_mismatch(self):
        host = Host()
        with mock.patch.object(host, "_verify_target_process"), \
             mock.patch("pvz_ai.pipe.PipeClient") as pipe_client:
            pipe_client.return_value.request.return_value = {"version": "unknown", "capabilities": []}
            with self.assertRaisesRegex(RuntimeError, "hello version mismatch"):
                host.connect(123)
        self.assertIsNone(host._pipe)

    def test_launch_waits_for_new_verified_game_child(self):
        game = Path(r"C:\Steam\Plants Vs Zombies\PlantsVsZombies.exe")
        child = host_module.EXPECTED_RUNTIME_PATH
        polls = iter(([900], [900, 901]))
        with mock.patch.object(host_module, "_verify_launcher_process"), \
             mock.patch.object(host_module, "time") as mocked_time:
            mocked_time.monotonic.side_effect = [0, 0, 0, 1]
            result = _wait_for_game_process({900}, 900, game, timeout=5, poll_interval=0,
                iter_pids=lambda: next(polls), image_path=lambda pid: child if pid == 901 else None,
                parent_pid=lambda pid: 900,
                verify_process=lambda pid: None,
                sleep=lambda _: None)
        self.assertEqual(result, 901)

    def test_diskless_runtime_path_uses_in_memory_fingerprint(self):
        host = Host()
        with mock.patch.object(host_module, "_process_image_path", return_value=host_module.EXPECTED_RUNTIME_PATH), \
             mock.patch.object(host_module, "_process_parent_pid", return_value=31024), \
             mock.patch.object(host_module, "_verify_launcher_process") as launcher_check, \
             mock.patch.object(host, "_verify_runtime_memory") as runtime_check:
            host._verify_target_process(13664, verify_text=True)
        launcher_check.assert_called_once_with(31024, host.game_path)
        runtime_check.assert_called_once_with(13664, verify_text=True)

    def test_connect_rejects_hello_ready_false_and_surfaces_restart(self):
        host = Host()
        with mock.patch.object(host, "_verify_target_process"), \
             mock.patch("pvz_ai.pipe.PipeClient") as pipe_client:
            pipe_client.return_value.request.return_value = {"version": "1.2.0.1096", "ready": False,
                "capabilities": [], "requires_restart": True, "initialization_error": "hook init failed"}
            with self.assertRaisesRegex(RuntimeError, "hook init failed.*requires_restart=true"):
                host.connect(123)
        self.assertIsNone(host._pipe)

    def test_pause_cancels_plan_between_state_and_action(self):
        host = Host()
        class FakePipe:
            def __init__(self): self.actions = []
            def request(self, op, args=None):
                if op == "state":
                    host.request("pause")
                    return {"tick": 1, "mode_id": "adventure", "board_id": "b1", "paused": False}
                if op == "pause": return {"paused": True}
                if op == "action": self.actions.append(args)
        fake = FakePipe(); host._pipe = fake
        result = host.submit_plan([{"type": "start_level"}], 2, "adventure")
        self.assertEqual(result, [])
        self.assertEqual(fake.actions, [])

    def test_guarded_plan_waits_for_condition_and_binds_board(self):
        host = Host()
        class FakePipe:
            def __init__(self): self.tick = 0; self.actions = []
            def request(self, op, args=None):
                if op == "state":
                    self.tick += 1
                    return {"tick": self.tick, "mode_id": "adventure", "board_id": "board-a",
                            "paused": False, "sun": 0 if self.tick == 2 else 25,
                            "capabilities": ["action.start_level"]}
                if op == "action": self.actions.append(args); return {"accepted": True}
                return {}
        fake = FakePipe(); host._pipe = fake
        plan = GuardedPlan("adventure", 10, [{"action": {"type": "start_level"},
            "when": [{"field": "sun", "op": "gt", "value": 0}]}])
        with mock.patch.object(host_module.time, "sleep", return_value=None):
            result = host.run_guarded_plan(plan, max_polls=5)
        self.assertEqual(len(result), 1)
        self.assertEqual(plan.board_id, "board-a")
        self.assertEqual(fake.actions, [{"type": "start_level"}])

    def test_action_refused_without_native_capability_advertisement(self):
        host = Host()
        class FakePipe:
            def __init__(self): self.actions = []
            def request(self, op, args=None):
                if op == "state": return {"capabilities": ["action.start_level"]}
                if op == "action": self.actions.append(args)
        fake = FakePipe(); host._pipe = fake
        with self.assertRaisesRegex(BridgeError, "does not advertise action.enter_garden"):
            host.request("action", {"type": "enter_garden", "garden_id": 0})
        self.assertEqual(fake.actions, [])

    def test_action_arguments_validated_before_capability_query(self):
        host = Host()
        class FakePipe:
            def request(self, op, args=None): raise AssertionError("invalid action must not reach pipe")
        host._pipe = FakePipe()
        with self.assertRaises(ValueError):
            host.request("action", {"type": "enter_garden", "garden_id": 3})


if __name__ == "__main__":
    unittest.main()
def test_unspecified_cli_game_path_uses_exact_default():
    from pvz_ai.host import Host, DEFAULT_GAME
    assert Host(None).game_path == DEFAULT_GAME
