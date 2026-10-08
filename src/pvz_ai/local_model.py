"""Small stdlib-only adapter for a locally hosted OpenAI-compatible model."""
from __future__ import annotations

import json
import urllib.error
import urllib.request
from urllib.parse import urlsplit
from typing import Any

from .templates import MAX_ACTIONS, TemplateError, _validate_action


class LocalModelError(RuntimeError):
    """Local model is unavailable or returned an invalid proposal."""


def _local_endpoint(base_url: str) -> str:
    if not isinstance(base_url, str):
        raise ValueError("base_url must be an HTTP URL on this computer.")
    parsed = urlsplit(base_url)
    if parsed.scheme != "http" or parsed.hostname not in {"127.0.0.1", "localhost", "::1"} or parsed.username or parsed.password:
        raise ValueError("Only an unauthenticated HTTP endpoint on localhost is allowed.")
    return base_url.rstrip("/") + "/chat/completions"


def validate_action_proposal(value: Any, max_actions: int = 8) -> list[dict]:
    if not isinstance(value, list) or len(value) > max_actions or len(value) > MAX_ACTIONS:
        raise LocalModelError(f"Model must return a list of at most {min(max_actions, MAX_ACTIONS)} actions.")
    try:
        return [_validate_action(item) for item in value]
    except (TemplateError, TypeError, KeyError) as exc:
        raise LocalModelError(f"Model proposed an invalid semantic action: {exc}") from None


def parse_chat_completion(payload: Any, max_actions: int = 8) -> list[dict]:
    """Accept one tool call named propose_actions, or JSON action array content."""
    try:
        message = payload["choices"][0]["message"]
        calls = message.get("tool_calls") or []
        if calls:
            if len(calls) != 1 or calls[0]["function"]["name"] != "propose_actions":
                raise LocalModelError("Model returned unsupported or multiple tool calls.")
            arguments = calls[0]["function"]["arguments"]
            if isinstance(arguments, str):
                arguments = json.loads(arguments)
            if isinstance(arguments, dict):
                arguments = arguments.get("actions")
        else:
            arguments = json.loads(message.get("content", ""))
            if isinstance(arguments, dict):
                arguments = arguments.get("actions")
    except LocalModelError:
        raise
    except (KeyError, IndexError, TypeError, json.JSONDecodeError):
        raise LocalModelError("Local model response did not contain a readable action proposal.") from None
    return validate_action_proposal(arguments, max_actions)


class LocalModelClient:
    def __init__(self, base_url: str, model: str, timeout: float = 20.0):
        if not isinstance(model, str) or not model.strip():
            raise ValueError("A local model name is required.")
        self.endpoint = _local_endpoint(base_url)
        self.model = model.strip()
        self.timeout = timeout

    def propose_actions(self, state: dict, system_prompt: str | None = None, max_actions: int = 8) -> list[dict]:
        """Request a short proposal from current visible state; this never executes it."""
        if not isinstance(state, dict):
            raise ValueError("state must be a current state object.")
        if not 1 <= max_actions <= 8:
            raise ValueError("max_actions must be from 1 to 8.")
        prompt = system_prompt or (
            "You propose ordinary Plants vs. Zombies semantic actions from the supplied visible state. "
            "Respect normal sun, cooldown, unlock, and board rules. Never infer hidden information. "
            "Return at most the requested number of actions. The host validates and may reject every action."
        )
        # The provider schema stays portable; the host validates every proposed action.
        tool = {"type": "function", "function": {"name": "propose_actions", "description": "Propose a short bounded list of semantic game actions.",
                 "parameters": {"type": "object", "properties": {"actions": {"type": "array", "maxItems": max_actions, "items": {"type": "object"}}}, "required": ["actions"], "additionalProperties": False}}}
        body = json.dumps({"model": self.model, "messages": [
            {"role": "system", "content": prompt},
            {"role": "user", "content": json.dumps({"state": state, "max_actions": max_actions}, ensure_ascii=False)},
        ], "tools": [tool], "tool_choice": {"type": "function", "function": {"name": "propose_actions"}}}, ensure_ascii=False).encode("utf-8")
        request = urllib.request.Request(self.endpoint, data=body, headers={"Content-Type": "application/json"}, method="POST")
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                result = json.loads(response.read(2_000_001).decode("utf-8"))
        except (urllib.error.URLError, TimeoutError, OSError, UnicodeDecodeError, json.JSONDecodeError):
            # Do not include request headers, tokens, or raw response bodies in errors.
            raise LocalModelError("Could not obtain a valid response from the configured local model.") from None
        return parse_chat_completion(result, max_actions)
