import json
import threading

from pvz_ai.runner import basic_adventure_policy, run_strategy


def playing_state(**overrides):
    state = {
        "tick": 100, "scene": 3, "scene_name": "playing", "mode_id": 0,
        "background_id": 0, "conveyor": False, "observation_complete": True,
        "paused": False, "board_id": "b1", "sun": 100,
        "capabilities": ["action.plant", "action.collect"],
        "seed_packets": [], "plants": [], "zombies": [], "collectibles": [],
    }
    state.update(overrides)
    return state


class FakeHost:
    def __init__(self, states, action_results=None, action_error=None, state_error_after_action=None):
        self.states = list(states)
        self.actions = []
        self.calls = []
        self.action_results = list(action_results or [])
        self.action_error = action_error
        self.state_error_after_action = state_error_after_action
    def request(self, op, args=None):
        self.calls.append((op, args))
        if op == "state":
            if self.actions and self.state_error_after_action:
                raise self.state_error_after_action
            return self.states.pop(0) if len(self.states) > 1 else self.states[0]
        if op == "action":
            self.actions.append(args)
            if self.action_error:
                raise self.action_error
            return self.action_results.pop(0) if self.action_results else {"accepted": True}
        return {"ok": True}


class FakeModel:
    def __init__(self, proposals=None, error=None, callback=None):
        self.proposals = proposals or []
        self.error = error
        self.callback = callback
        self.calls = 0
    def propose_actions(self, state, system_prompt=None, max_actions=1):
        self.calls += 1
        assert "Goal:" in system_prompt
        if self.callback:
            self.callback()
        if self.error:
            raise self.error
        return self.proposals


def test_basic_policy_is_limited_and_uses_visible_cost_and_slots():
    state = playing_state(seed_packets=[
        {"slot": 1, "plant_type": 0, "plant_name": "Peashooter", "usable": True, "cooldown_remaining": 0, "cost": 100},
        {"slot": 2, "plant_type": 1, "plant_name": "Sunflower", "usable": True, "cooldown_remaining": 0, "cost": 50},
    ], zombies=[{"id": 9, "row": 2, "x": 700, "y": 100, "health": 100}], sun=100)
    assert basic_adventure_policy(state) == {"type": "plant", "slot": 1, "row": 2, "col": 3}
    assert basic_adventure_policy({**state, "conveyor": True}) is None
    assert basic_adventure_policy({**state, "background_id": 3}) is None
    assert basic_adventure_policy({**state, "observation_complete": False}) is None


def test_basic_policy_collects_visible_sun_and_coin_before_planting():
    state = playing_state(seed_packets=[{"slot": 1, "plant_type": 1, "usable": True, "cooldown_remaining": 0, "cost": 50}],
                          collectibles=[{"id": 5, "kind": "coin", "amount": 10, "x": 20, "y": 30, "being_collected": False}])
    assert basic_adventure_policy(state) == {"type": "collect", "entity_id": 5}


def test_basic_policy_uses_live_costs_for_mine_and_squash_emergencies():
    state = playing_state(seed_packets=[{"slot": 3, "plant_type": 4, "usable": True,
                                        "cooldown_remaining": 0, "cost": 25}],
                          zombies=[{"id": 9, "row": 2, "x": 451, "health": 100}], sun=25)
    assert basic_adventure_policy(state) == {"type": "plant", "slot": 3, "row": 2, "col": 3}
    state = {**state, "seed_packets": [{"slot": 4, "plant_type": 17, "usable": True,
                                        "cooldown_remaining": 0, "cost": 50}],
             "zombies": [{"id": 9, "row": 2, "x": 319, "health": 100}], "sun": 50}
    assert basic_adventure_policy(state) == {"type": "plant", "slot": 4, "row": 2, "col": 4}


def test_basic_policy_pea_targets_and_back_row_farming_reserve():
    packet = {"slot": 1, "plant_type": 0, "usable": True, "cooldown_remaining": 0, "cost": 100}
    state = playing_state(seed_packets=[packet], zombies=[{"id": 1, "row": 1, "x": 241, "health": 100}], sun=100)
    assert basic_adventure_policy(state) == {"type": "plant", "slot": 1, "row": 1, "col": 3}
    state = {**state, "zombies": [{"id": 1, "row": 1, "x": 200, "health": 100}]}
    assert basic_adventure_policy(state) == {"type": "plant", "slot": 1, "row": 1, "col": 1}
    state = playing_state(seed_packets=[packet, {"slot": 2, "plant_type": 1, "usable": True,
                                                  "cooldown_remaining": 0, "cost": 50}],
                          zombies=[{"id": 1, "row": 1, "x": 800, "health": 100}], sun=75)
    assert basic_adventure_policy(state) is None  # reserve sun for the undefended row
    state = playing_state(seed_packets=[{"slot": 2, "plant_type": 1, "usable": True,
                                        "cooldown_remaining": 0, "cost": 50}], sun=100)
    assert basic_adventure_policy(state) == {"type": "plant", "slot": 2, "row": 1, "col": 1}


def test_basic_policy_stages_second_pea_for_multiple_visible_zombies():
    state = playing_state(
        seed_packets=[{"slot": 1, "plant_type": 0, "usable": True, "cooldown_remaining": 0, "cost": 100}],
        plants=[{"id": 20, "row": 2, "col": 3, "plant_type": 0, "health": 300}],
        zombies=[{"id": 1, "row": 2, "x": 600, "health": 100},
                 {"id": 2, "row": 2, "x": 500, "health": 100}], sun=100)
    assert basic_adventure_policy(state) == {"type": "plant", "slot": 1, "row": 2, "col": 5}


def test_basic_policy_collects_named_sun_and_coin_variants():
    for kind in ("small_sun", "large-sun", "coin_silver", "gold", "diamond_coin"):
        state = playing_state(collectibles=[{"id": 7, "kind": kind, "being_collected": False}])
        assert basic_adventure_policy(state) == {"type": "collect", "entity_id": 7}


def test_runner_dispatches_only_advertised_action_and_traces(tmp_path):
    state = playing_state(capabilities=["action.plant"])
    host = FakeHost([state, dict(state)])
    model = FakeModel([{"type": "collect", "entity_id": 5}, {"type": "plant", "slot": 1, "row": 3, "col": 1}])
    trace = tmp_path / "run.jsonl"
    result = run_strategy(host, model, "defend the lawn", max_steps=1, max_actions_per_proposal=2, trace_path=trace)
    assert result["actions"] == 1
    assert host.actions == [{"type": "plant", "slot": 1, "row": 3, "col": 1}]
    events = [json.loads(line)["event"] for line in trace.read_text(encoding="utf-8").splitlines()]
    assert events[0] == "start" and "action_accepted" in events and events[-1] == "finish"


def test_runner_waits_when_observation_is_incomplete():
    state = playing_state(observation_complete=False)
    host = FakeHost([state])
    model = FakeModel([{"type": "plant", "slot": 1, "row": 3, "col": 1}])
    result = run_strategy(host, model, "play", max_steps=1, poll_interval=0)
    assert result["actions"] == 0 and model.calls == 0 and host.actions == []


def test_runner_allows_only_menu_actions_on_incomplete_chooser_scene():
    state = {"tick": 1, "scene": 2, "scene_name": "seedchooser", "mode_id": 0,
             "board_id": None, "paused": False, "observation_complete": False,
             "capabilities": ["action.start_level", "action.plant"]}
    host = FakeHost([state, dict(state)])
    model = FakeModel([{"type": "plant", "slot": 1, "row": 1, "col": 1}, {"type": "start_level"}])
    run_strategy(host, model, "start", max_steps=1, max_actions_per_proposal=2)
    assert host.actions == [{"type": "start_level"}]


def test_runner_discards_proposal_when_board_or_tick_deadline_changes():
    state = playing_state()
    changed = {**state, "tick": 101, "board_id": "new-board"}
    host = FakeHost([state, changed])
    model = FakeModel([{"type": "plant", "slot": 1, "row": 3, "col": 1}])
    result = run_strategy(host, model, "play", max_steps=1)
    assert result["actions"] == 0 and host.actions == []


def test_runner_can_pause_on_model_error_only_when_capability_advertised():
    state = playing_state(capabilities=["pause", "action.plant"])
    host = FakeHost([state])
    model = FakeModel(error=RuntimeError("local model failed"))
    result = run_strategy(host, model, "play", max_steps=1, pause_on_model_error=True)
    assert result["outcome"] == "paused_after_model_error"
    assert any(op == "pause" for op, _ in host.calls)


def test_runner_cancellation_stops_before_state_request():
    cancel = threading.Event(); cancel.set()
    host = FakeHost([playing_state()])
    result = run_strategy(host, FakeModel(), "play", cancel_event=cancel)
    assert result["outcome"] == "cancelled"
    assert host.calls == [("state", None)]  # read final capability/state before optional pause


def test_runner_discards_model_result_when_wall_clock_deadline_passes():
    now = [0.0]
    host = FakeHost([playing_state(), playing_state()])
    model = FakeModel([{"type": "plant", "slot": 1, "row": 3, "col": 1}],
                      callback=lambda: now.__setitem__(0, 2.0))
    result = run_strategy(host, model, "play", max_steps=2, max_seconds=1,
                          clock=lambda: now[0], sleep=lambda _: None)
    assert result["outcome"] == "time_limit" and result["actions"] == 0
    assert host.actions == []


def test_runner_does_not_count_native_rejected_action_and_paces_retry():
    state = playing_state(capabilities=["action.plant"])
    host = FakeHost([state, dict(state), dict(state), dict(state)], action_results=[{"accepted": False, "reason": "invalid_cell"}])
    model = FakeModel([{"type": "plant", "slot": 1, "row": 3, "col": 1}])
    waits = []
    result = run_strategy(host, model, "play", max_steps=1, poll_interval=0.2,
                          sleep=lambda delay: waits.append(delay), pause_on_finish=False)
    assert result["actions"] == 0
    assert waits and waits[-1] == 0.2


def test_runner_halts_after_transport_uncertainty_and_forces_pause_without_retry():
    from pvz_ai.pipe import BridgeError
    state = playing_state(capabilities=["action.plant", "pause"])
    host = FakeHost([state, dict(state), dict(state)], action_error=BridgeError("timeout", "reply timed out"))
    model = FakeModel([{"type": "plant", "slot": 1, "row": 3, "col": 1}])
    result = run_strategy(host, model, "play", max_steps=5, pause_on_finish=False)
    assert result["outcome"] == "action_outcome_unknown_paused"
    assert len(host.actions) == 1
    assert sum(op == "action" for op, _ in host.calls) == 1
    assert any(op == "pause" for op, _ in host.calls)


def test_runner_attempts_pause_from_last_safe_state_if_final_state_is_unavailable():
    from pvz_ai.pipe import BridgeError
    state = playing_state(capabilities=["action.plant", "pause"])
    host = FakeHost([state, dict(state)], action_error=BridgeError("read_failed", "reply read failed"),
                    state_error_after_action=BridgeError("unavailable", "pipe unavailable"))
    model = FakeModel([{"type": "plant", "slot": 1, "row": 3, "col": 1}])
    run_strategy(host, model, "play", max_steps=5)
    assert sum(op == "action" for op, _ in host.calls) == 1
    assert any(op == "pause" for op, _ in host.calls)


def test_runner_counts_verification_pending_acceptance_without_replaying_action():
    state = playing_state(capabilities=["action.plant"])
    host = FakeHost([state, dict(state)], action_results=[{"accepted": True, "verification_pending": True}])
    model = FakeModel([{"type": "plant", "slot": 1, "row": 3, "col": 1}])
    result = run_strategy(host, model, "play", max_steps=1, pause_on_finish=False)
    assert result["actions"] == 1
    assert sum(op == "action" for op, _ in host.calls) == 1


def test_runner_stops_on_lost_and_award_without_claiming_goal_success():
    for state, expected in ((playing_state(scene=4, scene_name="lost", result="lost"), "lost"),
                            (playing_state(scene=5, scene_name="award", result="award"), "award"),
                            (playing_state(scene=3, scene_name="playing", result="in_progress"), "step_limit")):
        host = FakeHost([state])
        result = run_strategy(host, FakeModel(), "survive", max_steps=1, poll_interval=0)
        assert result["outcome"].removesuffix("_paused") == expected
        assert not any(op == "action" for op, _ in host.calls)


def test_runner_pauses_at_end_by_default_but_explicit_continue_suppresses_pause():
    state = playing_state(capabilities=["pause"])
    host = FakeHost([state, dict(state), dict(state)])
    run_strategy(host, FakeModel(), "play", max_steps=1, poll_interval=0)
    assert any(op == "pause" for op, _ in host.calls)
    host = FakeHost([state, dict(state), dict(state)])
    run_strategy(host, FakeModel(), "play", max_steps=1, poll_interval=0, pause_on_finish=False)
    assert not any(op == "pause" for op, _ in host.calls)
