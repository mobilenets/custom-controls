import time
from datetime import datetime
from typing import List, Dict, Optional


# -----------------------------
# Core domain objects
# -----------------------------

class Node:
    def __init__(self, node_id: str, node_type: str):
        self.node_id = node_id
        self.node_type = node_type
        self.state = "off"

    def __repr__(self):
        return f"<Node {self.node_id} ({self.node_type}) state={self.state}>"


class Group:
    def __init__(self, name: str, nodes: List[Node]):
        self.name = name
        self.nodes = nodes
        self.state = "off"

    def apply_state(self, state: str):
        self.state = state
        for n in self.nodes:
            n.state = state

    def __repr__(self):
        return f"<Group {self.name} state={self.state} nodes={[n.node_id for n in self.nodes]}>"


class Event:
    """
    Simple event abstraction:
    - edge_switch
    - pir
    - schedule
    """
    def __init__(self, event_type: str, source: str, value: Optional[str] = None):
        self.event_type = event_type
        self.source = source
        self.value = value
        self.timestamp = datetime.utcnow()

    def __repr__(self):
        return f"<Event {self.event_type} from {self.source} value={self.value}>"



class LogicRecord:
    def __init__(
        self,
        record_id: str,
        output_group: Group,
        trigger_type: str,
        desired_state: str,
    ):
        self.record_id = record_id
        self.output_group = output_group
        self.trigger_type = trigger_type  # event, schedule, sensor
        self.desired_state = desired_state

        # Runtime state
        self.last_requested_state = "undefined"
        self.last_actual_state = "undefined"
        self.last_reason = "none"
        self.validated = False
        self.history = []

    def evaluate(self, event: Optional[Event] = None):
        """
        Minimal resolution logic
        """
        reason = "no_trigger"

        if event and event.event_type == self.trigger_type:
            reason = f"Triggered by {event.event_type} ({event.source})"
            self.last_requested_state = self.desired_state
        else:
            self.last_requested_state = "pass"

        # Apply resolution
        if self.last_requested_state != "pass":
            self.output_group.apply_state(self.last_requested_state)
            self.last_actual_state = self.output_group.state
            self.validated = True
        else:
            self.validated = False

        record = {
            "timestamp": datetime.utcnow().isoformat(),
            "record_id": self.record_id,
            "group": self.output_group.name,
            "requested_state": self.last_requested_state,
            "actual_state": self.last_actual_state,
            "reason": reason,
            "validated": self.validated,
        }

        self.history.append(record)
        if len(self.history) > 10:
            self.history.pop(0)

        return record


# -----------------------------
# Logic Engine
# -----------------------------

class LogicEngine:
    def __init__(self):
        self.nodes: Dict[str, Node] = {}
        self.groups: Dict[str, Group] = {}
        self.logic_records: List[LogicRecord] = []

    def add_node(self, node: Node):
        self.nodes[node.node_id] = node

    def add_group(self, group: Group):
        self.groups[group.name] = group

    def add_logic_record(self, record: LogicRecord):
        self.logic_records.append(record)

    def tick(self, event: Optional[Event] = None):
        print("\n--- ENGINE TICK ---")
        for record in self.logic_records:
            result = record.evaluate(event)
            print(result)


# -----------------------------
# Demo / bootstrap
# -----------------------------

def main():
    engine = LogicEngine()

    # Create nodes
    relay1 = Node("relay1", "relay")
    relay2 = Node("relay2", "relay")
    pir1 = Node("pir1", "pir")

    engine.add_node(relay1)
    engine.add_node(relay2)
    engine.add_node(pir1)

    # Create group
    shop_lights = Group("Shop Lights", [relay1, relay2])
    engine.add_group(shop_lights)

    # Create logic record (event-driven)
    pir_logic = LogicRecord(
        record_id="pir_turn_on_lights",
        output_group=shop_lights,
        trigger_type="pir",
        desired_state="on",
    )

    engine.add_logic_record(pir_logic)

    # Run engine loop
    step = 0
    while step < 5:
        if step == 2:
            event = Event(event_type="pir", source="pir1")
            print(f"\n>>> EVENT OCCURRED: {event}")
            engine.tick(event)
        else:
            engine.tick()

        time.sleep(1)
        step += 1


if __name__ == "__main__":
    main()
