import contextlib
import io
from pathlib import Path
import tempfile
from unittest import mock

from pvz_ai import __main__ as cli


def test_token_is_persisted_without_printing_secret():
    with tempfile.TemporaryDirectory(dir=Path.cwd()) as td:
        token_file = Path(td) / "private" / "token"
        fake = type("FakeServer", (), {
            "server_port": 8765,
            "serve_forever": lambda self: None,
            "server_close": lambda self: None,
        })()
        captured = io.StringIO()
        with mock.patch.object(cli, "_protect_token_file"), \
             mock.patch.object(cli, "serve_http", side_effect=lambda host, port, token: (fake, token)), \
             contextlib.redirect_stdout(captured):
            assert cli.main(["http", "--token-file", str(token_file)]) == 0
        token = token_file.read_text(encoding="utf-8").strip()
        output = captured.getvalue()
        import json
        record = json.loads(output)
        assert token and len(token) >= 32
        assert token not in output
        assert "bearer_token" not in output
        assert record["token_file"] == str(token_file)
        assert record["url"] == "http://127.0.0.1:8765"


def test_attach_running_uses_unique_verified_pid_before_server_start():
    fake_host = mock.Mock()
    fake_host.find_running_game.return_value = 123
    fake_server = type("FakeServer", (), {
        "server_port": 8765,
        "serve_forever": lambda self: None,
        "server_close": lambda self: None,
    })()
    with tempfile.TemporaryDirectory(dir=Path.cwd()) as td, \
         mock.patch.object(cli, "Host", return_value=fake_host), \
         mock.patch.object(cli, "_protect_token_file"), \
         mock.patch.object(cli, "serve_http", return_value=(fake_server, "unused")):
        assert cli.main(["http", "--attach-running", "--token-file", str(Path(td) / "token")]) == 0
    fake_host.find_running_game.assert_called_once_with()
    fake_host.attach.assert_called_once_with(123)


def test_token_file_is_reused():
    with tempfile.TemporaryDirectory(dir=Path.cwd()) as td:
        root = Path(td); token_file = root / "token"
        with mock.patch.object(cli, "_protect_token_file"):
            first = cli._load_or_create_token(token_file)
            second = cli._load_or_create_token(token_file)
        assert first == second
        assert len(first) >= 32
