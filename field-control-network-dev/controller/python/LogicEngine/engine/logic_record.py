from dataclasses import dataclass
from datetime import datetime
from enum import Enum
from typing import List, Optional

from engine.group import Group
from engine.condition import Condition
from engine.runtime.input_state import InputState

class LogicalOperator(str, Enum):
    AND = "and"
    OR = "or"


@dataclass
class LogicRecord:
    id: str
    output_group: Group
    conditions: List[Condition]
    operator: LogicalOperator
    desired_state: str

    # --- NEW (optional, runtime-only) ---
    trigger: str = "level"   # "level", "rising_edge", "falling_edge"
    input_key: Optional[str] = None
    _input_state: InputState = InputState()

    last_fired: Optional[datetime] = None
    last_reason: str = ""
    last_result: bool = False

    def resolve(self, runtime_state: dict) -> dict:
            # --- NEW: edge tracking (if configured) ---
        if self.input_key:
            raw_value = runtime_state.get(self.input_key)
            self._input_state.update(raw_value)

            # First tick guard
            if self._input_state.previous is None:
                return self._build_result(False, "Initializing input state")

            if self.trigger == "rising_edge" and not self._input_state.rising_edge():
                return self._build_result(False, "No rising edge")

            if self.trigger == "falling_edge" and not self._input_state.falling_edge():
                return self._build_result(False, "No falling edge")

        # --- EXISTING CONDITION LOGIC (unchanged) ---
        results = [c.evaluate(runtime_state) for c in self.conditions]

        if self.operator == LogicalOperator.AND:
            final = all(results)
        else:
            final = any(results)

        self.last_result = final

        if final:
            self.output_group.apply_state(self.desired_state)
            self.last_fired = datetime.utcnow()
            self.last_reason = "Conditions met"
        else:
            self.last_reason = f"Conditions not met: {results}"

        return {
            "logicRecordId": self.id,
            "group": self.output_group.name,
            "requested_state": self.desired_state if final else "pass",
            "actual_state": self.output_group.state,
            "result": final,
            "reason": self.last_reason,
        }

    def _build_result(self, final: bool, reason: str) -> dict:
        return {
            "logicRecordId": self.id,
            "group": self.output_group.name,
            "requested_state": self.desired_state if final else "pass",
            "actual_state": self.output_group.state,
            "result": final,
            "reason": reason,
        }
