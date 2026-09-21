from dataclasses import dataclass, field
from typing import List
from enum import Enum

from engine.logic.condition_def import ConditionDef


class LogicalOperator(str, Enum):
    AND = "AND"
    OR = "OR"


@dataclass
class LogicRecordDef:
    record_id: str
    output_group: str
    priority: int
    operator: LogicalOperator
    desired_state: bool              # <-- NEW: True=on, False=off
    conditions: List[ConditionDef]

    last_reason: str = ""
    history: List[str] = field(default_factory=list)

    record_type: str = "standard"
    hold_seconds: float = 60
