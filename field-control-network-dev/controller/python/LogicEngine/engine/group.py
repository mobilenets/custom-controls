from dataclasses import dataclass
from typing import List
from .node import NodeDef


@dataclass
class Group:
    name: str
    nodes: List[NodeDef]
    state: str = "off"

    def apply_state(self, desired_state: str):
        for node in self.nodes:
            if node.can_be_output():
                # hardware adapter would live here later
                pass
        self.state = desired_state
