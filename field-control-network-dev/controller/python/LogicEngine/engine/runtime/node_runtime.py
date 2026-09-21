from dataclasses import dataclass
from typing import Any

@dataclass
class NodeRuntime:
    node_id: str
    state: Any = None
