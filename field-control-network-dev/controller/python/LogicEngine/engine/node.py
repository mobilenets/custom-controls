from dataclasses import dataclass
from enum import Enum
from typing import Optional


class NodeType(str, Enum):
    RELAY = "relay"
    PIR = "pir"
    LIGHT_SENSOR = "light_sensor"
    UNKNOWN = "unknown"


NODE_TYPE_CAPABILITIES = {
    "relay": {"can_be_output": True, "can_be_trigger": False},
    "pir": {"can_be_output": False, "can_be_trigger": True},
    "light_sensor": {"can_be_output": False, "can_be_trigger": True},
    "unknown": {"can_be_output": False, "can_be_trigger": False},
}


@dataclass
class NodeDef:
    node_record_id: str
    module_id: str
    relay_id: str
    node_type: NodeType
    friendly_name: Optional[str] = None

    def can_be_output(self) -> bool:
        return NODE_TYPE_CAPABILITIES[self.node_type.value]["can_be_output"]

    def can_be_trigger(self) -> bool:
        return NODE_TYPE_CAPABILITIES[self.node_type.value]["can_be_trigger"]
