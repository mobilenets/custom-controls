from dataclasses import dataclass
from typing import Any
from .node import NodeDef


@dataclass
class Condition:
    source_node: NodeDef
    condition_type: str
    expected: Any
    negate: bool = False

    def evaluate(self, runtime_state: dict) -> bool:
        value = runtime_state.get(self.source_node.node_record_id)

        result = False

        if self.condition_type == "state":
            result = value == self.expected

        elif self.condition_type == "threshold":
            result = value is not None and value > self.expected

        if self.negate:
            result = not result

        return result
