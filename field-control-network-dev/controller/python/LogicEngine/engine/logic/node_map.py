from dataclasses import dataclass
from typing import Optional, Literal

NodeKind = Literal["relay", "input"]

@dataclass(frozen=True)
class NodeMapEntry:
    node_id: str
    module_id: str
    kind: NodeKind
    index: int
    friendly_name: Optional[str] = None
