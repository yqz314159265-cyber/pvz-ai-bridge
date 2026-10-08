from pvz_ai.health import check_health


class StalledHost:
    def status(self):
        return {"attached": True, "pid": 123}

    def request(self, operation):
        raise TimeoutError("Game thread did not execute the request")


def test_cached_attachment_does_not_report_a_stalled_game_as_healthy():
    value = check_health(StalledHost())
    assert value["attached"] is True
    assert value["responsive"] is False
    assert value["reason"] == "game_thread_unavailable"
    assert value["latency_ms"] >= 0


def test_health_probes_game_thread_and_exposes_fresh_tick():
    class LiveHost(StalledHost):
        def request(self, operation):
            assert operation == "state"
            return {"scene_name": "menu", "bridge_ticks": 321}
    assert check_health(LiveHost())["bridge_ticks"] == 321
    assert check_health(LiveHost())["responsive"] is True
