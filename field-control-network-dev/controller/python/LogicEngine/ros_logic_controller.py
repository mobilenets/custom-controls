#!/usr/bin/env python3
import re
from typing import Dict, Tuple, Optional, List

import rclpy
from rclpy.node import Node
from std_msgs.msg import String
from rclpy.qos import QoSProfile, ReliabilityPolicy
from config.xml_loader import load_logic_config
from engine.runtime_engine import LogicEngine
import json
from pathlib import Path
from datetime import datetime


# ----------------------------
# Parse helpers
# ----------------------------
_MODRET_RE = re.compile(
    r"^\s*(?P<module>[^;]+)\s*;\s*R:(?P<r>[^;]*)\s*;\s*I:(?P<i>[^;]*)\s*$"
)


def _parse_bool_list(csv: str) -> List[bool]:
    """
    Accepts "0,1,0,1" or "false,true,..." etc.
    """
    out: List[bool] = []
    csv = (csv or "").strip()
    if not csv:
        return out

    for tok in csv.split(","):
        t = tok.strip().lower()
        if t in ("1", "true", "on", "high"):
            out.append(True)
        elif t in ("0", "false", "off", "low"):
            out.append(False)
        else:
            out.append(False)

    return out


def parse_modulereturn(payload: str) -> Optional[Tuple[str, List[bool], List[bool]]]:
    """
    Returns (module_id, relays, inputs) or None if parse fails.
    """
    payload = (payload or "").strip()

    if payload.lower().startswith("data:"):
        payload = payload.split(":", 1)[1].strip()

    m = _MODRET_RE.match(payload)
    if not m:
        return None

    module_id = m.group("module").strip()
    relays = _parse_bool_list(m.group("r"))
    inputs = _parse_bool_list(m.group("i"))
    return module_id, relays, inputs


# ----------------------------
# ROS <-> Engine Adapter Node
# ----------------------------
class RosLogicController(Node):
    def __init__(self):
        super().__init__("ros_logic_controller")
        qos = QoSProfile(
                depth=10,
                reliability=ReliabilityPolicy.BEST_EFFORT
            )
        self.debug = {
            "step":0,
            "last_modulereturn":None,
            "last_actionrequest":None,
        }
        self.status_path = Path("/home/dev/Projects/BasicLogicEngine/LogicEngine/runtime_status.json")
        # Load engine config
        nodes, groups, logic_records, node_map = load_logic_config("config/logic_config.xml")
        self.engine = LogicEngine(nodes, groups, logic_records)
        self.node_map = node_map

        # ROS IO
        self.sub = self.create_subscription(String, "/modulereturn", self.on_modulereturn, qos)
        self.pub = self.create_publisher(String, "/actionrequest", qos)

        # Tick loop at 500ms
        self.timer = self.create_timer(0.5, self.on_tick)

        # Track last output state we commanded, to avoid spamming
        self.last_commanded: Dict[Tuple[str, int], Optional[bool]] = {}

        self.get_logger().info("ROS Logic Controller started (tick=500ms)")

    def write_status_snapshot(self):
        snapshot = {
            "time": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "last_update": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "groups": {k: str(v) for k, v in self.engine.group_states.items()},
            "flags": dict(getattr(self.engine, "flags", {})),
            "nodes": {k: self.engine.nodes[k].state for k in self.engine.nodes},
            "last_modulereturn": self.debug.get("last_modulereturn"),
            "last_actionrequest": self.debug.get("last_actionrequest"),
        }

        with open(self.status_path, "w", encoding="utf-8") as f:
            json.dump(snapshot, f, indent=2)


    # -------- Inbound aggregated status --------
    def on_modulereturn(self, msg: String):
        parsed = parse_modulereturn(msg.data)
        if not parsed:
            self.get_logger().warn(f"Could not parse /modulereturn: {msg.data!r}")
            return

        module_id, relays, inputs = parsed
        self.debug["last_modulereturn"] = msg.data
        for node_id, entry in self.node_map.items():
            if entry.module_id != module_id:
                continue

            if entry.kind == "input":
                if entry.index < len(inputs):
                    self.engine.nodes[node_id].state = inputs[entry.index]

            # elif entry.kind == "relay":
            #     if entry.index < len(relays):
            #         self.engine.nodes[node_id].state = relays[entry.index]

    # -------- Outbound command helper --------
    def publish_action(self, module_id: str, relay_index: int, state: bool):
        msg = String()
        msg.data = f"{module_id};{relay_index};{'ON' if state else 'OFF'}"
        self.debug["last_modulereturn"] = msg.data
        self.pub.publish(msg)
        self.get_logger().info(f"Published /actionrequest: {msg.data}")

    # -------- Engine tick --------
    def on_tick(self):
        if not hasattr(self, "_step"):
            self._step = 0

        self.engine.tick(self._step)
        self._step += 1

        for node_id, entry in self.node_map.items():
            module_id = entry.module_id
            kind = entry.kind
            index = entry.index

            if kind != "relay":
                continue

            desired = self.engine.nodes[node_id].state
            if desired is None:
                continue
            if not isinstance(desired, bool):
                continue

            key = (module_id, index)
            last = self.last_commanded.get(key)

            if last is None or last != desired:
                self.publish_action(module_id, index, desired)
                self.last_commanded[key] = desired

        self.debug["step"] += 1
        self.write_status_snapshot()

def main():
    rclpy.init()
    node = RosLogicController()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
