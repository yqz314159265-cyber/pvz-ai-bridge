import tempfile
import unittest
from pathlib import Path
from unittest import mock

import pvz_ai.host as host_module
from pvz_ai.host import Host, _game_app_manifest, _wait_for_game_launcher


class SteamLaunchTests(unittest.TestCase):
    def _game_install(self, root: Path, launcher_path: str = r"D:\Steam\Steam.exe") -> Path:
        game = root / "steamapps" / "common" / "Plants Vs Zombies" / "PlantsVsZombies.exe"
        game.parent.mkdir(parents=True)
        manifest = game.parent.parent.parent / "appmanifest_3590.acf"
        manifest.write_text(
            '"AppState"\n{\n'
            '  "appid" "3590"\n'
            '  "installdir" "Plants Vs Zombies"\n'
            f'  "LauncherPath" "{launcher_path}"\n'
            '}\n', encoding="utf-8")
        return game

    def test_manifest_requires_exact_appid_install_directory_and_executable_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = self._game_install(Path(tmp))
            manifest, values = _game_app_manifest(game)
            self.assertEqual(manifest.name, "appmanifest_3590.acf")
            self.assertEqual(values["appid"], "3590")
            wrong = game.parent.parent / "Other" / game.name
            with self.assertRaisesRegex(RuntimeError, "exact appmanifest 3590"):
                _game_app_manifest(wrong)
            manifest.write_text('"appid" "123"\n"installdir" "Plants Vs Zombies"', encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "does not identify"):
                _game_app_manifest(game)

    def test_discover_roots_reads_registry_and_libraryfolders_without_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            library = base / "Library"
            game = self._game_install(library, launcher_path=str(base / "Steam" / "Steam.exe"))
            steam_root = base / "Steam"
            steam_root.mkdir()
            (steam_root / "steamapps").mkdir()
            (steam_root / "steamapps" / "libraryfolders.vdf").write_text(
                '"libraryfolders" { "1" { "path" "' + str(library).replace("\\", "\\\\") + '" } }',
                encoding="utf-8")
            with mock.patch.object(host_module, "_registered_steam_roots", return_value=[steam_root]):
                roots = host_module.discover_steam_roots(game)
            normalized = {host_module._normpath(item) for item in roots}
            self.assertIn(host_module._normpath(library), normalized)
            self.assertIn(host_module._normpath(steam_root), normalized)

    def test_launcher_wait_requires_exact_new_image_and_steam_ancestry(self):
        game = Path(r"C:\Library\Plants Vs Zombies\PlantsVsZombies.exe")
        polls = iter(([10, 20, 30], [10, 20, 30]))
        images = {20: game, 30: game}
        parents = {20: 99, 99: 99, 30: 10}
        with mock.patch.object(host_module, "_verify_launcher_process") as verify, \
             mock.patch.object(host_module, "time") as mocked_time:
            mocked_time.monotonic.side_effect = [0, 1]
            pid = _wait_for_game_launcher({10}, {10}, game, timeout=5, poll_interval=0,
                iter_pids=lambda: next(polls), image_path=lambda candidate: images.get(candidate),
                parent_pid=lambda candidate: parents.get(candidate), sleep=lambda _: None)
        self.assertEqual(pid, 30)
        verify.assert_called_once_with(30, game)

    def test_launch_uses_steam_applaunch_and_waits_for_verified_launcher_then_runtime(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = self._game_install(Path(tmp))
            steam = Path(tmp) / "Steam" / "Steam.exe"
            steam.parent.mkdir()
            steam.touch()
            host = Host(game_path=game)
            with mock.patch.object(host_module, "discover_game", return_value={"supported": True}), \
                 mock.patch.object(host_module, "_steam_executable", return_value=steam), \
                 mock.patch.object(host_module, "_iter_process_pids", return_value={111}), \
                 mock.patch.object(host_module, "_processes_at_path", return_value={111}), \
                 mock.patch.object(host_module.subprocess, "Popen") as popen, \
                 mock.patch.object(host_module, "_wait_for_game_launcher", return_value=222) as wait_launcher, \
                 mock.patch.object(host_module, "_wait_for_game_process", return_value=333) as wait_runtime, \
                 mock.patch.object(host, "attach", return_value={"pid": 333}) as attach:
                result = host.launch()

        popen.assert_called_once()
        command = popen.call_args.args[0]
        self.assertEqual(command, [str(steam), "-applaunch", "3590"])
        self.assertEqual(popen.call_args.kwargs["cwd"], str(steam.parent))
        wait_launcher.assert_called_once_with({111}, {111}, game, timeout=host.launch_timeout)
        wait_runtime.assert_called_once_with({111}, 222, game, timeout=host.launch_timeout,
            parent_pid=host_module._process_parent_pid,
            verify_process=mock.ANY)
        attach.assert_called_once_with(333)
        self.assertEqual(result, {"pid": 333})

    def test_launch_refuses_bad_manifest_before_starting_steam(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp) / "PlantsVsZombies.exe"
            host = Host(game_path=game)
            with mock.patch.object(host_module, "discover_game", return_value={"supported": True}), \
                 mock.patch.object(host_module.subprocess, "Popen") as popen:
                with self.assertRaisesRegex(RuntimeError, "appmanifest"):
                    host.launch()
            popen.assert_not_called()


if __name__ == "__main__":
    unittest.main()
