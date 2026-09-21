import tkinter as tk
import time
import xml.etree.ElementTree as ET
from pathlib import Path
from datetime import datetime
import json 
import os
import tempfile
from tkinter import messagebox

class AppState:
    def __init__(self):
        self.ros_connected = False
        self.agent_connected = False
        self.logic_enabled = True
        self.modules = {}
        self.groups = {}
        self.logic_records = {}
        self.last_update = None

class LogicConfigLoader:
    def __init__(self, config_path="logic_config.xml"):
        self.config_path = Path(config_path)
        self.node_map = {}
        self.groups={}
        self.logic_records={}
    def load_modules(self):
        modules = {}

        if not self.config_path.exists():
            print(f"Config file not found: {self.config_path}")
            return modules

        try:
            tree = ET.parse(self.config_path)
            root = tree.getroot()

            # First pass: load physical modules
            for mod in root.findall(".//modules/module"):
                module_id = mod.get("id")
                if not module_id:
                    continue

                relay_count = self._safe_int(mod.get("relayCount"), 0)
                input_count = self._safe_int(mod.get("inputCount"), 0)

                module_key = f"module_{module_id}"

                modules[module_key] = {
                    "module_id": module_id,
                    "name": mod.get("friendlyName", f"Module {module_id}"),
                    "role": mod.get("role", ""),
                    "location": mod.get("location", ""),
                    "host": mod.get("host", ""),
                    "ip": mod.get("ip", ""),
                    "enabled": mod.get("enabled", "true").lower() == "true",
                    "relay_count": relay_count,
                    "input_count": input_count,
                    "configured": True,
                    "online": False,
                    "last_seen": None,
                    "relays": {},
                    "inputs": {},
                    "relay_states": {},
                    "input_states": {},
                }

            # Second pass: attach logical nodes to modules
            for node in root.findall(".//nodes/node"):
                module_id = node.get("moduleId")
                node_id = node.get("id")
                kind = node.get("kind")
                index = self._safe_int(node.get("index"), None)
                friendly_name = node.get("friendlyName", node_id or "Unnamed")

                if not module_id or kind not in ("relay", "input") or index is None:
                    continue

                module_key = f"module_{module_id}"

                if module_key not in modules:
                    modules[module_key] = {
                        "module_id": module_id,
                        "name": f"Module {module_id}",
                        "role": "",
                        "location": "",
                        "host": "",
                        "ip": "",
                        "enabled": True,
                        "relay_count": 0,
                        "input_count": 0,
                        "configured": False,
                        "online": False,
                        "last_seen": None,
                        "relays": {},
                        "inputs": {},
                        "relay_states": {},
                        "input_states": {},
                    }

                entry = {
                    "node_id": node_id,
                    "index": index,
                    "friendly_name": friendly_name,
                    "kind": kind,
                }

                self.node_map[node_id] = {
                    "module_key": module_key,
                    "module_id": module_id,
                    "kind": kind,
                    "index": index,
                }

                if kind == "relay":
                    modules[module_key]["relays"][index] = entry
                    modules[module_key]["relay_states"].setdefault(index, 0)
                else:
                    modules[module_key]["inputs"][index] = entry
                    modules[module_key]["input_states"].setdefault(index, 0)

            self.groups = {}

            for group in root.findall(".//groups/group"):
                group_id = group.get("id")
                aggregation = group.get("aggregation", "any")
                if not group_id:
                    continue

                members = []
                for member in group.findall("./member"):
                    ref = member.get("ref")
                    if ref:
                        members.append(ref)

                self.groups[group_id] = {
                    "id": group_id,
                    "aggregation": aggregation,
                    "members": members,
                }

            self.logic_records = {}

            for record in root.findall(".//logicEngine/logicRecords/logicRecord"):
                record_id = record.get("id")
                if not record_id:
                    continue

                conditions = []
                for cond in record.findall("./condition"):
                    conditions.append({
                        "id": cond.get("id", ""),
                        "source": cond.get("source", ""),
                        "operator": cond.get("operator", ""),
                        "value": cond.get("value", ""),
                    })

               
                self.logic_records[record_id] = {
                    "id": record_id,
                    "recordType": record.get("recordType", "standard"),
                    "outputGroup": record.get("outputGroup", ""),
                    "priority": record.get("priority", ""),
                    "operator": record.get("operator", "AND"),
                    "desiredState": record.get("desiredState", ""),
                    "holdSeconds": record.get("holdSeconds", ""),
                    "conditions": conditions,
                }

            return modules

        except Exception as e:
            print(f"Failed to parse logic config: {e}")
            return {}

    @staticmethod
    def _safe_int(value, default):
        try:
            return int(value)
        except (TypeError, ValueError):
            return default

class LogicConfigWriter:
    def __init__(self, config_path):
        self.config_path = Path(config_path)

    def add_module(
        self,
        module_id,
        friendly_name,
        location="",
        host="",
        ip="",
        relay_count=8,
        input_count=4,
        enabled=True,
        role=""
    ):
        module_id = str(module_id).strip()
        friendly_name = str(friendly_name).strip()

        if not module_id:
            raise ValueError("Module ID cannot be empty")
        if not friendly_name:
            raise ValueError("Friendly name cannot be empty")
        if relay_count < 0 or input_count < 0:
            raise ValueError("Relay and input counts must be zero or greater")

        if not self.config_path.exists():
            raise FileNotFoundError(f"Config file not found: {self.config_path}")

        tree = ET.parse(self.config_path)
        root = tree.getroot()

        modules_elem = root.find(".//modules")
        if modules_elem is None:
            config_elem = root.find(".//config")
            if config_elem is None:
                raise ValueError("Invalid XML: <config> section not found")
            modules_elem = ET.SubElement(config_elem, "modules")

        for mod in modules_elem.findall("./module"):
            if mod.get("id") == module_id:
                raise ValueError(f"Module ID '{module_id}' already exists")

        attrs = {
            "id": module_id,
            "friendlyName": friendly_name,
            "location": str(location).strip(),
            "host": str(host).strip(),
            "ip": str(ip).strip(),
            "relayCount": str(relay_count),
            "inputCount": str(input_count),
            "enabled": "true" if enabled else "false",
        }

        role = str(role).strip()
        if role:
            attrs["role"] = role

        ET.SubElement(modules_elem, "module", attrs)
        self._write_atomic(tree)

    def _write_atomic(self, tree):
        self._indent(tree.getroot())

        with tempfile.NamedTemporaryFile(
            "w",
            encoding="utf-8",
            delete=False,
            dir=str(self.config_path.parent),
            suffix=".tmp"
        ) as tmp_file:
            tmp_path = Path(tmp_file.name)
            tree.write(tmp_file, encoding="unicode", xml_declaration=False)

        os.replace(tmp_path, self.config_path)

    def _indent(self, elem, level=0):
        indent = "\n" + ("  " * level)

        if len(elem):
            if not elem.text or not elem.text.strip():
                elem.text = indent + "  "
            for child in elem:
                self._indent(child, level + 1)
            if not elem[-1].tail or not elem[-1].tail.strip():
                elem[-1].tail = indent
        if level and (not elem.tail or not elem.tail.strip()):
            elem.tail = indent

    def add_node(
        self,
        node_id,
        module_id,
        kind,
        index,
        friendly_name=""
    ):
        node_id = str(node_id).strip()
        module_id = str(module_id).strip()
        kind = str(kind).strip().lower()
        friendly_name = str(friendly_name).strip()

        if not node_id:
            raise ValueError("Node ID cannot be empty")
        if not module_id:
            raise ValueError("Module ID cannot be empty")
        if kind not in ("relay", "input"):
            raise ValueError("Node kind must be 'relay' or 'input'")
        if index < 0:
            raise ValueError("Index must be zero or greater")

        if not self.config_path.exists():
            raise FileNotFoundError(f"Config file not found: {self.config_path}")

        tree = ET.parse(self.config_path)
        root = tree.getroot()

        config_elem = root.find(".//config")
        if config_elem is None:
            raise ValueError("Invalid XML: <config> section not found")

        modules_elem = root.find(".//modules")
        if modules_elem is None:
            raise ValueError("Invalid XML: <modules> section not found")

        nodes_elem = root.find(".//nodes")
        if nodes_elem is None:
            nodes_elem = ET.SubElement(config_elem, "nodes")

        # Make sure module exists
        module_exists = any(mod.get("id") == module_id for mod in modules_elem.findall("./module"))
        if not module_exists:
            raise ValueError(f"Module ID '{module_id}' does not exist")

        # Prevent duplicate node IDs
        for node in nodes_elem.findall("./node"):
            if node.get("id") == node_id:
                raise ValueError(f"Node ID '{node_id}' already exists")

        # Prevent duplicate kind/index on same module
        for node in nodes_elem.findall("./node"):
            if (
                node.get("moduleId") == module_id and
                node.get("kind") == kind and
                node.get("index") == str(index)
            ):
                raise ValueError(
                    f"Module {module_id} already has a {kind} at index {index}"
                )

        attrs = {
            "id": node_id,
            "moduleId": module_id,
            "kind": kind,
            "index": str(index),
            "friendlyName": friendly_name or node_id,
        }

        ET.SubElement(nodes_elem, "node", attrs)
        self._write_atomic(tree)

    def add_group(self, group_id, aggregation="any", members=None):
        group_id = str(group_id).strip()
        aggregation = str(aggregation).strip().lower()
        members = members or []

        if not group_id:
            raise ValueError("Group ID cannot be empty")
        if aggregation not in ("any", "all"):
            raise ValueError("Aggregation must be 'any' or 'all'")

        if not self.config_path.exists():
            raise FileNotFoundError(f"Config file not found: {self.config_path}")

        tree = ET.parse(self.config_path)
        root = tree.getroot()

        config_elem = root.find(".//config")
        if config_elem is None:
            raise ValueError("Invalid XML: <config> section not found")

        groups_elem = root.find(".//groups")
        if groups_elem is None:
            groups_elem = ET.SubElement(config_elem, "groups")

        for group in groups_elem.findall("./group"):
            if group.get("id") == group_id:
                raise ValueError(f"Group ID '{group_id}' already exists")

        # Validate member refs against existing nodes
        valid_node_ids = {
            node.get("id")
            for node in root.findall(".//nodes/node")
            if node.get("id")
        }

        for member in members:
            if member not in valid_node_ids:
                raise ValueError(f"Unknown node ref '{member}'")

        group_elem = ET.SubElement(groups_elem, "group", {
            "id": group_id,
            "aggregation": aggregation,
        })

        for member in members:
            ET.SubElement(group_elem, "member", {"ref": member})

        self._write_atomic(tree)

    def update_group(self, original_group_id, new_group_id, aggregation="any", members=None):
        original_group_id = str(original_group_id).strip()
        new_group_id = str(new_group_id).strip()
        aggregation = str(aggregation).strip().lower()
        members = members or []

        if not original_group_id:
            raise ValueError("Original group ID cannot be empty")
        if not new_group_id:
            raise ValueError("New group ID cannot be empty")
        if aggregation not in ("any", "all"):
            raise ValueError("Aggregation must be 'any' or 'all'")

        if not self.config_path.exists():
            raise FileNotFoundError(f"Config file not found: {self.config_path}")

        tree = ET.parse(self.config_path)
        root = tree.getroot()

        groups_elem = root.find(".//groups")
        if groups_elem is None:
            raise ValueError("Invalid XML: <groups> section not found")

        target_group = None
        for group in groups_elem.findall("./group"):
            if group.get("id") == original_group_id:
                target_group = group
                break

        if target_group is None:
            raise ValueError(f"Group ID '{original_group_id}' not found")

        # Prevent renaming to an existing different group
        if original_group_id != new_group_id:
            for group in groups_elem.findall("./group"):
                if group.get("id") == new_group_id:
                    raise ValueError(f"Group ID '{new_group_id}' already exists")

        valid_node_ids = {
            node.get("id")
            for node in root.findall(".//nodes/node")
            if node.get("id")
        }

        for member in members:
            if member not in valid_node_ids:
                raise ValueError(f"Unknown node ref '{member}'")

        target_group.set("id", new_group_id)
        target_group.set("aggregation", aggregation)

        for child in list(target_group):
            target_group.remove(child)

        for member in members:
            ET.SubElement(target_group, "member", {"ref": member})

        self._write_atomic(tree)

    def delete_group(self, group_id):
        group_id = str(group_id).strip()
        if not group_id:
            raise ValueError("Group ID cannot be empty")

        if not self.config_path.exists():
            raise FileNotFoundError(f"Config file not found: {self.config_path}")

        tree = ET.parse(self.config_path)
        root = tree.getroot()

        groups_elem = root.find(".//groups")
        if groups_elem is None:
            raise ValueError("Invalid XML: <groups> section not found")

        target_group = None
        for group in groups_elem.findall("./group"):
            if group.get("id") == group_id:
                target_group = group
                break

        if target_group is None:
            raise ValueError(f"Group ID '{group_id}' not found")

        groups_elem.remove(target_group)
        self._write_atomic(tree)

    def add_logic_record(
        self,
        record_id,
        output_group,
        priority,
        operator,
        desired_state,
        conditions=None,
        record_type="standard",
        hold_seconds=""
    ):
        record_id = str(record_id).strip()
        output_group = str(output_group).strip()
        priority = str(priority).strip()
        operator = str(operator).strip().upper()
        desired_state = str(desired_state).strip().lower()
        record_type = str(record_type).strip() or "standard"
        hold_seconds = str(hold_seconds).strip()
        conditions = conditions or []

        if not record_id:
            raise ValueError("Logic record ID cannot be empty")
        if not output_group:
            raise ValueError("Output group cannot be empty")
        if not priority:
            raise ValueError("Priority cannot be empty")
        if operator not in ("AND", "OR"):
            raise ValueError("Operator must be AND or OR")
        if desired_state not in ("on", "off"):
            raise ValueError("Desired state must be 'on' or 'off'")

        if record_type == "motion_hold":
            if not hold_seconds:
                raise ValueError("Motion hold records require holdSeconds")
            try:
                if int(hold_seconds) <= 0:
                    raise ValueError
            except ValueError:
                raise ValueError("holdSeconds must be a positive whole number")

        if not self.config_path.exists():
            raise FileNotFoundError(f"Config file not found: {self.config_path}")

        tree = ET.parse(self.config_path)
        root = tree.getroot()

        logic_records_elem = root.find(".//logicEngine/logicRecords")
        if logic_records_elem is None:
            logic_engine_elem = root.find(".//logicEngine")
            if logic_engine_elem is None:
                config_elem = root.find(".//config")
                if config_elem is None:
                    raise ValueError("Invalid XML: <config> section not found")
                logic_engine_elem = ET.SubElement(config_elem, "logicEngine")
            logic_records_elem = ET.SubElement(logic_engine_elem, "logicRecords")

        for record in logic_records_elem.findall("./logicRecord"):
            if record.get("id") == record_id:
                raise ValueError(f"Logic record ID '{record_id}' already exists")

        attrs = {
            "id": record_id,
            "outputGroup": output_group,
            "priority": priority,
            "operator": operator,
            "desiredState": desired_state,
        }

        if record_type != "standard":
            attrs["recordType"] = record_type

        if hold_seconds:
            attrs["holdSeconds"] = hold_seconds

        record_elem = ET.SubElement(logic_records_elem, "logicRecord", attrs)

        self._append_conditions(record_elem, conditions)
        self._write_atomic(tree)

    def update_logic_record(
        self,
        original_record_id,
        new_record_id,
        output_group,
        priority,
        operator,
        desired_state,
        conditions=None,
        record_type="standard",
        hold_seconds=""
    ):
        original_record_id = str(original_record_id).strip()
        new_record_id = str(new_record_id).strip()
        output_group = str(output_group).strip()
        priority = str(priority).strip()
        operator = str(operator).strip().upper()
        desired_state = str(desired_state).strip().lower()
        record_type = str(record_type).strip() or "standard"
        hold_seconds = str(hold_seconds).strip()
        conditions = conditions or []

        if not original_record_id:
            raise ValueError("Original logic record ID cannot be empty")
        if not new_record_id:
            raise ValueError("New logic record ID cannot be empty")
        if not output_group:
            raise ValueError("Output group cannot be empty")
        if not priority:
            raise ValueError("Priority cannot be empty")
        if operator not in ("AND", "OR"):
            raise ValueError("Operator must be AND or OR")
        if desired_state not in ("on", "off"):
            raise ValueError("Desired state must be 'on' or 'off'")

        if record_type == "motion_hold":
            if not hold_seconds:
                raise ValueError("Motion hold records require holdSeconds")
            try:
                if int(hold_seconds) <= 0:
                    raise ValueError
            except ValueError:
                raise ValueError("holdSeconds must be a positive whole number")

        if not self.config_path.exists():
            raise FileNotFoundError(f"Config file not found: {self.config_path}")

        tree = ET.parse(self.config_path)
        root = tree.getroot()

        logic_records_elem = root.find(".//logicEngine/logicRecords")
        if logic_records_elem is None:
            raise ValueError("Invalid XML: <logicRecords> section not found")

        target = None
        for record in logic_records_elem.findall("./logicRecord"):
            if record.get("id") == original_record_id:
                target = record
                break

        if target is None:
            raise ValueError(f"Logic record ID '{original_record_id}' not found")

        if original_record_id != new_record_id:
            for record in logic_records_elem.findall("./logicRecord"):
                if record.get("id") == new_record_id:
                    raise ValueError(f"Logic record ID '{new_record_id}' already exists")

        target.set("id", new_record_id)
        target.set("outputGroup", output_group)
        target.set("priority", priority)
        target.set("operator", operator)
        target.set("desiredState", desired_state)

        if record_type != "standard":
            target.set("recordType", record_type)
        else:
            target.attrib.pop("recordType", None)

        if hold_seconds:
            target.set("holdSeconds", hold_seconds)
        else:
            target.attrib.pop("holdSeconds", None)

        for child in list(target):
            target.remove(child)

        self._append_conditions(target, conditions)
        self._write_atomic(tree)

    def delete_logic_record(self, record_id):
        record_id = str(record_id).strip()
        if not record_id:
            raise ValueError("Logic record ID cannot be empty")

        if not self.config_path.exists():
            raise FileNotFoundError(f"Config file not found: {self.config_path}")

        tree = ET.parse(self.config_path)
        root = tree.getroot()

        logic_records_elem = root.find(".//logicEngine/logicRecords")
        if logic_records_elem is None:
            raise ValueError("Invalid XML: <logicRecords> section not found")

        target = None
        for record in logic_records_elem.findall("./logicRecord"):
            if record.get("id") == record_id:
                target = record
                break

        if target is None:
            raise ValueError(f"Logic record ID '{record_id}' not found")

        logic_records_elem.remove(target)
        self._write_atomic(tree)

    def _append_conditions(self, parent_elem, conditions):
        seen_ids = set()

        for cond in conditions:
            cond_id = str(cond.get("id", "")).strip()
            source = str(cond.get("source", "")).strip()
            operator = str(cond.get("operator", "")).strip()
            value = cond.get("value", "")

            if not cond_id:
                raise ValueError("Condition ID cannot be empty")
            if cond_id in seen_ids:
                raise ValueError(f"Duplicate condition ID '{cond_id}' in same logic record")
            if not source:
                raise ValueError(f"Condition '{cond_id}' must have a source")
            if not operator:
                raise ValueError(f"Condition '{cond_id}' must have an operator")

            seen_ids.add(cond_id)

            attrs = {
                "id": cond_id,
                "source": source,
                "operator": operator,
            }

            if str(value).strip():
                attrs["value"] = str(value).strip()

            ET.SubElement(parent_elem, "condition", attrs)

class RosInterface:
    def __init__(self, app_state, on_update=None):
        self.app_state = app_state
        self.on_update = on_update

    def connect(self):
        # Start ROS node/thread here
        self.app_state.ros_connected = True
        self.app_state.agent_connected = True
        self._notify()

    def publish_command(self, command: str):
        print(f"Publishing command: {command}")

    def update_module_state(self, module_id, relay_states=None, input_states=None):
        module_key = f"module_{module_id}"

        if module_key not in self.app_state.modules:
            self.app_state.modules[module_key] = {
                "module_id": str(module_id),
                "name": f"Module {module_id}",
                "role": "",
                "location": "",
                "host": "",
                "ip": "",
                "enabled": True,
                "relay_count": 0,
                "input_count": 0,
                "configured": False,
                "online": False,
                "last_seen": None,
                "relays": {},
                "inputs": {},
                "relay_states": {},
                "input_states": {},
            }

        module = self.app_state.modules[module_key]

        if relay_states:
            for idx, value in relay_states.items():
                module["relay_states"][int(idx)] = int(value)

        if input_states:
            for idx, value in input_states.items():
                module["input_states"][int(idx)] = int(value)

        module["online"] = True
        module["last_seen"] = time.time()
        self.app_state.last_update = time.time()
        self._notify()

    def _notify(self):
        if self.on_update:
            self.on_update()

def update_module_online_from_runtime(app_state, runtime_data, hold_seconds=30):
    if not runtime_data:
        return

    now = time.time()
    last_modulereturn = runtime_data.get("last_modulereturn")

    if last_modulereturn:
        try:
            module_id = str(last_modulereturn.split(";")[0]).strip()
            module_key = f"module_{module_id}"

            if module_key in app_state.modules:
                app_state.modules[module_key]["last_seen"] = time.time()
        except Exception:
            pass

    for module in app_state.modules.values():
        last_seen = module.get("last_seen")
        module["online"] = last_seen is not None and (now - last_seen) <= hold_seconds

def mark_stale_modules(app_state, timeout_seconds=15):
    now = time.time()
    for module in app_state.modules.values():
        last_seen = module.get("last_seen")
        if last_seen is None:
            module["online"] = False
        elif now - last_seen > timeout_seconds:
            module["online"] = False

def apply_runtime_node_states(app_state, runtime_data, node_to_module_map):
    runtime_nodes = runtime_data.get("nodes", {})

    for node_id, value in runtime_nodes.items():
        mapping = node_to_module_map.get(node_id)
        if not mapping:
            continue

        module_key = mapping["module_key"]
        kind = mapping["kind"]
        index = mapping["index"]

        module = app_state.modules.get(module_key)
        if not module:
            continue

        state = 1 if bool(value) else 0

        if kind == "relay":
            module["relay_states"][index] = state
        elif kind == "input":
            module["input_states"][index] = state

def apply_runtime_status(app_state, runtime_data, node_to_module_map, stale_seconds=15):
    if not runtime_data:
        return

    now = time.time()

    # Engine freshness
    last_update_ts = RuntimeStatusReader.parse_timestamp(runtime_data.get("last_update"))
    if last_update_ts is not None:
        app_state.last_update = last_update_ts
        app_state.ros_connected = (now - last_update_ts) <= stale_seconds
        app_state.agent_connected = app_state.ros_connected

    # Update point states by node name
    runtime_nodes = runtime_data.get("nodes", {})
    for node_id, value in runtime_nodes.items():
        mapping = node_to_module_map.get(node_id)
        if not mapping:
            continue

        module_key = mapping["module_key"]
        kind = mapping["kind"]
        index = mapping["index"]

        module = app_state.modules.get(module_key)
        if not module:
            continue

        state = 1 if bool(value) else 0

        if kind == "relay":
            module["relay_states"][index] = state
        elif kind == "input":
            module["input_states"][index] = state

    # Update most recently heard module from last_modulereturn
    last_modulereturn = runtime_data.get("last_modulereturn")
    if last_modulereturn:
        try:
            module_id = str(last_modulereturn.split(";")[0]).strip()
            module_key = f"module_{module_id}"

            if module_key in app_state.modules and last_update_ts is not None:
                app_state.modules[module_key]["online"] = (now - last_update_ts) <= stale_seconds
                app_state.modules[module_key]["last_seen"] = last_update_ts
        except Exception:
            pass

class HeaderBar(tk.Frame):
    def __init__(self, parent, app_state):
        super().__init__(parent, bd=2, relief="raised")
        self.app_state = app_state

        self.title_label = tk.Label(self, text="Logic Engine Control", font=("Arial", 18, "bold"))
        self.title_label.pack(side="left", padx=10, pady=8)

        self.status_label = tk.Label(self, text="ROS: Disconnected", font=("Arial", 14))
        self.status_label.pack(side="right", padx=10)

    def refresh(self):
        ros = "Connected" if self.app_state.ros_connected else "Disconnected"
        self.status_label.config(text=f"ROS: {ros}")

class NavigationFrame(tk.Frame):
    def __init__(self, parent, on_nav):
        super().__init__(parent, bd=2, relief="groove")
        buttons = [
            ("Dashboard", "dashboard"),
            ("Modules", "modules"),
            ("Schedules", "schedules"),
            ("Groups", "groups"),
            ("Logic", "logic"),
            ("Settings", "settings"),
            ("Debug" , "debug"),
        ]

        for text, page in buttons:
            btn = tk.Button(
                self,
                text=text,
                font=("Arial", 16),
                height=2,
                command=lambda p=page: on_nav(p)
            )
            btn.pack(fill="x", padx=8, pady=6)

class RuntimeStatusReader:
    def __init__(self, status_path):
        self.status_path = Path(status_path)

    def load(self):
        if not self.status_path.exists():
            return None

        try:
            with open(self.status_path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception as e:
            print(f"Failed to read runtime status: {e}")
            return None

    @staticmethod
    def parse_timestamp(ts):
        if not ts:
            return None
        try:
            return datetime.strptime(ts, "%Y-%m-%d %H:%M:%S").timestamp()
        except Exception:
            return None

class DashboardPage(tk.Frame):
    def __init__(self, parent, app_state):
        super().__init__(parent)
        self.app_state = app_state
        self.label = tk.Label(self, text="Dashboard", font=("Arial", 20, "bold"))
        self.label.pack(pady=10)

        self.summary = tk.Label(self, text="", font=("Arial", 14), justify="left")
        self.summary.pack(anchor="w", padx=20, pady=10)

    def refresh(self):
        module_count = len(self.app_state.modules)
        online_count = sum(1 for m in self.app_state.modules.values() if m.get("online"))
        logic = "Enabled" if self.app_state.logic_enabled else "Disabled"
        ros = "Connected" if self.app_state.ros_connected else "Disconnected"

        text = (
            f"ROS: {ros}\n"
            f"Logic Engine: {logic}\n"
            f"Configured Modules: {module_count}\n"
            f"Online Modules: {online_count}\n"
        )
        self.summary.config(text=text)

class AddModuleDialog(tk.Toplevel):
    def __init__(self, parent, on_submit):
        super().__init__(parent)
        self.title("Add Module")
        self.on_submit = on_submit
        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()

        self.entries = {}

        fields = [
            ("Module ID", "module_id"),
            ("Friendly Name", "friendly_name"),
            ("Location", "location"),
            ("Host", "host"),
            ("IP", "ip"),
            ("Relay Count", "relay_count"),
            ("Input Count", "input_count"),
            ("Role", "role"),
        ]

        for row, (label_text, key) in enumerate(fields):
            tk.Label(self, text=label_text, anchor="w").grid(
                row=row, column=0, padx=8, pady=6, sticky="w"
            )
            entry = tk.Entry(self, width=32)
            entry.grid(row=row, column=1, padx=8, pady=6)
            self.entries[key] = entry

        self.entries["relay_count"].insert(0, "8")
        self.entries["input_count"].insert(0, "4")

        self.enabled_var = tk.BooleanVar(value=True)
        tk.Checkbutton(self, text="Enabled", variable=self.enabled_var).grid(
            row=len(fields), column=1, padx=8, pady=6, sticky="w"
        )

        button_row = tk.Frame(self)
        button_row.grid(row=len(fields) + 1, column=0, columnspan=2, pady=12)

        tk.Button(button_row, text="Cancel", command=self.destroy).pack(side="left", padx=6)
        tk.Button(button_row, text="Add Module", command=self.submit).pack(side="left", padx=6)

        self.entries["module_id"].focus_set()

    def submit(self):
        try:
            data = {
                "module_id": self.entries["module_id"].get().strip(),
                "friendly_name": self.entries["friendly_name"].get().strip(),
                "location": self.entries["location"].get().strip(),
                "host": self.entries["host"].get().strip(),
                "ip": self.entries["ip"].get().strip(),
                "relay_count": int(self.entries["relay_count"].get().strip()),
                "input_count": int(self.entries["input_count"].get().strip()),
                "role": self.entries["role"].get().strip(),
                "enabled": self.enabled_var.get(),
            }
            self.on_submit(data)
            self.destroy()
        except ValueError:
            messagebox.showerror(
                "Invalid Input",
                "Relay count and input count must be whole numbers."
            )

class AddNodeDialog(tk.Toplevel):
    def __init__(self, parent, module_options, on_submit):
        super().__init__(parent)
        self.title("Add Node")
        self.on_submit = on_submit
        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()

        self.module_options = module_options

        tk.Label(self, text="Node ID", anchor="w").grid(row=0, column=0, padx=8, pady=6, sticky="w")
        self.node_id_entry = tk.Entry(self, width=32)
        self.node_id_entry.grid(row=0, column=1, padx=8, pady=6)

        tk.Label(self, text="Module", anchor="w").grid(row=1, column=0, padx=8, pady=6, sticky="w")
        self.module_var = tk.StringVar()
        self.module_menu = tk.OptionMenu(self, self.module_var, *module_options)
        self.module_menu.config(width=28)
        self.module_menu.grid(row=1, column=1, padx=8, pady=6, sticky="w")

        if module_options:
            self.module_var.set(module_options[0])

        tk.Label(self, text="Kind", anchor="w").grid(row=2, column=0, padx=8, pady=6, sticky="w")
        self.kind_var = tk.StringVar(value="relay")
        self.kind_menu = tk.OptionMenu(self, self.kind_var, "relay", "input")
        self.kind_menu.config(width=28)
        self.kind_menu.grid(row=2, column=1, padx=8, pady=6, sticky="w")

        tk.Label(self, text="Index", anchor="w").grid(row=3, column=0, padx=8, pady=6, sticky="w")
        self.index_entry = tk.Entry(self, width=32)
        self.index_entry.grid(row=3, column=1, padx=8, pady=6)

        tk.Label(self, text="Friendly Name", anchor="w").grid(row=4, column=0, padx=8, pady=6, sticky="w")
        self.friendly_name_entry = tk.Entry(self, width=32)
        self.friendly_name_entry.grid(row=4, column=1, padx=8, pady=6)

        button_row = tk.Frame(self)
        button_row.grid(row=5, column=0, columnspan=2, pady=12)

        tk.Button(button_row, text="Cancel", command=self.destroy).pack(side="left", padx=6)
        tk.Button(button_row, text="Add Node", command=self.submit).pack(side="left", padx=6)

        self.node_id_entry.focus_set()

    def submit(self):
        try:
            module_choice = self.module_var.get().strip()
            if not module_choice:
                raise ValueError("You must select a module")

            module_id = module_choice.split(" - ", 1)[0]

            data = {
                "node_id": self.node_id_entry.get().strip(),
                "module_id": module_id,
                "kind": self.kind_var.get().strip(),
                "index": int(self.index_entry.get().strip()),
                "friendly_name": self.friendly_name_entry.get().strip(),
            }
            self.on_submit(data)
            self.destroy()
        except ValueError as e:
            messagebox.showerror("Invalid Input", str(e))

class GroupDialog(tk.Toplevel):
    def __init__(self, parent, node_options, on_submit, title="Group", initial=None):
        super().__init__(parent)
        self.title(title)
        self.on_submit = on_submit
        self.node_options = node_options
        self.initial = initial or {}

        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()

        tk.Label(self, text="Group ID", anchor="w").grid(row=0, column=0, padx=8, pady=6, sticky="w")
        self.group_id_entry = tk.Entry(self, width=32)
        self.group_id_entry.grid(row=0, column=1, padx=8, pady=6)

        tk.Label(self, text="Aggregation", anchor="w").grid(row=1, column=0, padx=8, pady=6, sticky="w")
        self.aggregation_var = tk.StringVar(value=self.initial.get("aggregation", "any"))
        self.aggregation_menu = tk.OptionMenu(self, self.aggregation_var, "any", "all")
        self.aggregation_menu.config(width=28)
        self.aggregation_menu.grid(row=1, column=1, padx=8, pady=6, sticky="w")

        tk.Label(self, text="Members", anchor="w").grid(row=2, column=0, padx=8, pady=6, sticky="nw")
        self.member_listbox = tk.Listbox(self, selectmode=tk.MULTIPLE, width=32, height=12, exportselection=False)
        self.member_listbox.grid(row=2, column=1, padx=8, pady=6)

        for idx, node_id in enumerate(node_options):
            self.member_listbox.insert(tk.END, node_id)

        initial_members = set(self.initial.get("members", []))
        for idx, node_id in enumerate(node_options):
            if node_id in initial_members:
                self.member_listbox.selection_set(idx)

        button_row = tk.Frame(self)
        button_row.grid(row=3, column=0, columnspan=2, pady=12)

        tk.Button(button_row, text="Cancel", command=self.destroy).pack(side="left", padx=6)
        tk.Button(button_row, text="Save Group", command=self.submit).pack(side="left", padx=6)

        if self.initial.get("id"):
            self.group_id_entry.insert(0, self.initial["id"])
        self.group_id_entry.focus_set()

    def submit(self):
        group_id = self.group_id_entry.get().strip()
        aggregation = self.aggregation_var.get().strip()
        members = [self.node_options[i] for i in self.member_listbox.curselection()]

        if not group_id:
            messagebox.showerror("Invalid Input", "Group ID cannot be empty.")
            return

        self.on_submit({
            "group_id": group_id,
            "aggregation": aggregation,
            "members": members,
        })
        self.destroy()

class ModulesPage(tk.Frame):
    def __init__(self, parent, app_state, ros_if):
        super().__init__(parent)
        self.app_state = app_state
        self.ros_if = ros_if

        self.title = tk.Label(self, text="Modules", font=("Arial", 20, "bold"))
        self.title.pack(pady=10)

        # self.add_button = tk.Button(
        #     self,
        #     text="Add Module",
        #     font=("Arial", 12),
        #     command=self.open_add_module_dialog
        # )
        # self.add_button.pack(pady=(0, 8))

        button_row = tk.Frame(self)
        button_row.pack(pady=(0, 8))

        self.add_button = tk.Button(
            button_row,
            text="Add Module",
            font=("Arial", 12),
            command=self.open_add_module_dialog
        )
        self.add_button.pack(side="left", padx=4)

        self.add_node_button = tk.Button(
            button_row,
            text="Add Node",
            font=("Arial", 12),
            command=self.open_add_node_dialog
        )
        self.add_node_button.pack(side="left", padx=4)

        # Scrollable area
        self.scroll_canvas = tk.Canvas(self, highlightthickness=0)
        self.scrollbar = tk.Scrollbar(self, orient="vertical", command=self.scroll_canvas.yview)
        self.scrollable_frame = tk.Frame(self.scroll_canvas)
        self.scroll_canvas.bind_all("<Button-4>", lambda e: self.scroll_canvas.yview_scroll(-1, "units"))
        self.scroll_canvas.bind_all("<Button-5>", lambda e: self.scroll_canvas.yview_scroll(1, "units"))
        self.scrollable_frame.bind(
            "<Configure>",
            lambda e: self.scroll_canvas.configure(scrollregion=self.scroll_canvas.bbox("all"))
        )

        self.canvas_window = self.scroll_canvas.create_window(
            (0, 0),
            window=self.scrollable_frame,
            anchor="nw"
        )

        self.scroll_canvas.configure(yscrollcommand=self.scrollbar.set)

        self.scroll_canvas.pack(side="left", fill="both", expand=True)
        self.scrollbar.pack(side="right", fill="y")

        self.scroll_canvas.bind(
            "<Configure>",
            self._on_canvas_configure
        )

        # widget cache to reduce flicker
        self.module_widgets = {}
        self.last_module_signature = None

        # mouse wheel support
        self.scroll_canvas.bind_all("<MouseWheel>", self._on_mousewheel)

    def open_add_module_dialog(self):
        AddModuleDialog(self, self.handle_add_module)

    def handle_add_module(self, data):
        try:
            app = self.winfo_toplevel()
            app.add_module_to_config(
                module_id=data["module_id"],
                friendly_name=data["friendly_name"],
                location=data["location"],
                host=data["host"],
                ip=data["ip"],
                relay_count=data["relay_count"],
                input_count=data["input_count"],
                enabled=data["enabled"],
                role=data["role"],
            )
        except Exception as e:
            messagebox.showerror("Add Module Failed", str(e))

    def _update_states_only(self):
        for module_key, widgets in self.module_widgets.items():
            if module_key == "empty_label":
                continue

            module = self.app_state.modules.get(module_key)
            if not module:
                continue

            online = module.get("online")

            if online:
                fg_color = "black"
                bg_color = "#f0f0f0"
                indicator_color = "green"
            else:
                fg_color = "#888888"
                bg_color = "#e8e8e8"
                indicator_color = "#888888"

            status = "Online" if module.get("online") else "Offline"
            enabled = "Enabled" if module.get("enabled") else "Disabled"
            configured = "Configured" if module.get("configured") else "Discovered"

            info = (
                f"ID: {module['module_id']}    "
                f"Status: {status}    "
                f"{enabled}    "
                f"{configured}"
            )
            widgets["status_indicator"].config(fg=indicator_color)
            widgets["info_label"].config(text=info, fg=fg_color)

            details = (
                f"Location: {module.get('location', '')}    "
                f"Host: {module.get('host', '')}    "
                f"IP: {module.get('ip', '')}"
            )
            widgets["details_label"].config(text=details, fg=fg_color)

            counts = (
                f"Configured capacity: "
                f"{module.get('relay_count', 0)} relays, "
                f"{module.get('input_count', 0)} inputs"
            )
            widgets["counts_label"].config(text=counts, fg=fg_color)
            widgets["frame"].config(bg=bg_color)

            relays = module.get("relays", {})
            relay_states = module.get("relay_states", {})
            for idx, label in widgets["relay_labels"].items():
                relay_info = relays.get(idx, {})
                state = "ON" if relay_states.get(idx, 0) else "OFF"
                label.config(
                    text=f"R{idx}: {relay_info.get('friendly_name', f'Relay {idx}')} [{state}]",
                    fg=fg_color
                )

            inputs = module.get("inputs", {})
            input_states = module.get("input_states", {})
            for idx, label in widgets["input_labels"].items():
                input_info = inputs.get(idx, {})
                state = "ON" if input_states.get(idx, 0) else "OFF"
                label.config(
                    text=f"I{idx}: {input_info.get('friendly_name', f'Input {idx}')} [{state}]",
                    fg=fg_color
                )
    def _on_canvas_configure(self, event):
        self.scroll_canvas.itemconfig(self.canvas_window, width=event.width)

    def _on_mousewheel(self, event):
        self.scroll_canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")

    def _module_signature(self):
        """
        Returns a stable signature for whether the module layout changed.
        If this changes, rebuild all widgets. If not, only update labels.
        """
        sig = []
        for module_key in sorted(self.app_state.modules.keys()):
            module = self.app_state.modules[module_key]
            sig.append((
                module_key,
                module.get("name", ""),
                module.get("module_id", ""),
                tuple(sorted(module.get("relays", {}).keys())),
                tuple(sorted(module.get("inputs", {}).keys())),
            ))
        return tuple(sig)

    def refresh(self):
        if not self.app_state.modules:
            if "empty_label" not in self.module_widgets:
                self._clear_all()
                empty_label = tk.Label(
                    self.scrollable_frame,
                    text="No modules loaded from logic_config.xml",
                    font=("Arial", 14)
                )
                empty_label.pack(pady=20)
                self.module_widgets["empty_label"] = empty_label
            return

        current_signature = self._module_signature()

        if current_signature != self.last_module_signature:
            self._rebuild_all()
            self.last_module_signature = current_signature

        self._update_states_only()

    def _clear_all(self):
        for widget in self.scrollable_frame.winfo_children():
            widget.destroy()
        self.module_widgets = {}

    def _rebuild_all(self):
        self._clear_all()

        sorted_keys = sorted(
            self.app_state.modules.keys(),
            key=lambda k: int(self.app_state.modules[k]["module_id"])
            if str(self.app_state.modules[k]["module_id"]).isdigit()
            else str(self.app_state.modules[k]["module_id"])
        )

        for module_key in sorted_keys:
            module = self.app_state.modules[module_key]

            frame = tk.LabelFrame(
                self.scrollable_frame,
                text=module["name"],
                padx=10,
                pady=10
            )
            frame.pack(fill="x", padx=10, pady=8)

            header_row = tk.Frame(frame)
            header_row.pack(fill="x", anchor="w")

            status_indicator = tk.Label(
                header_row,
                text="●",
                font=("Arial", 14),
                width=2
            )
            status_indicator.pack(side="left")

            info_label = tk.Label(
                header_row,
                text="",
                font=("Arial", 11, "bold"),
                anchor="w",
                justify="left"
            )
            info_label.pack(side="left", fill="x")

            # info_label = tk.Label(frame, text="", font=("Arial", 11, "bold"), anchor="w", justify="left")
            # info_label.pack(anchor="w")

            

            details_label = tk.Label(frame, text="", font=("Arial", 10), anchor="w", justify="left")
            details_label.pack(anchor="w", pady=(0, 6))

            counts_label = tk.Label(frame, text="", font=("Arial", 10), anchor="w", justify="left")
            counts_label.pack(anchor="w", pady=(0, 6))

            relay_labels = {}
            input_labels = {}

            relays = module.get("relays", {})
            if relays:
                tk.Label(frame, text="Relays", font=("Arial", 11, "bold")).pack(anchor="w")
                for idx in sorted(relays.keys()):
                    relay_info = relays[idx]

                    row = tk.Frame(frame)
                    row.pack(fill="x", anchor="w", pady=2)

                    relay_label = tk.Label(
                        row,
                        text="",
                        font=("Arial", 10),
                        width=42,
                        anchor="w"
                    )
                    relay_label.pack(side="left")

                    tk.Button(
                        row,
                        text=f"Toggle R{idx}",
                        font=("Arial", 10),
                        command=lambda m=module["module_id"], i=idx: self.toggle_relay(m, i)
                    ).pack(side="left", padx=4)

                    relay_labels[idx] = relay_label

            inputs = module.get("inputs", {})
            if inputs:
                tk.Label(frame, text="Inputs", font=("Arial", 11, "bold")).pack(anchor="w", pady=(8, 0))
                for idx in sorted(inputs.keys()):
                    input_label = tk.Label(
                        frame,
                        text="",
                        font=("Arial", 10),
                        anchor="w"
                    )
                    input_label.pack(anchor="w", pady=1)
                    input_labels[idx] = input_label

            self.module_widgets[module_key] = {
                "frame": frame,
                "info_label": info_label,
                "details_label": details_label,
                "status_indicator": status_indicator,
                "counts_label": counts_label,
                "relay_labels": relay_labels,
                "input_labels": input_labels,
            }

    def open_add_node_dialog(self):
        module_options = []
        for module_key in sorted(
            self.app_state.modules.keys(),
            key=lambda k: int(self.app_state.modules[k]["module_id"])
            if str(self.app_state.modules[k]["module_id"]).isdigit()
            else str(self.app_state.modules[k]["module_id"])
        ):
            module = self.app_state.modules[module_key]
            module_options.append(f"{module['module_id']} - {module['name']}")

        if not module_options:
            messagebox.showerror("No Modules", "Add a module before adding nodes.")
            return

        AddNodeDialog(self, module_options, self.handle_add_node)

    def handle_add_node(self, data):
        try:
            app = self.winfo_toplevel()
            app.add_node_to_config(
                node_id=data["node_id"],
                module_id=data["module_id"],
                kind=data["kind"],
                index=data["index"],
                friendly_name=data["friendly_name"],
            )
        except Exception as e:
            messagebox.showerror("Add Node Failed", str(e))

    def toggle_relay(self, module_id, relay_index):
        self.ros_if.publish_command(f"{module_id};{relay_index};TOGGLE")

class LogicRecordDialog(tk.Toplevel):
    def __init__(self, parent, group_options, on_submit, title="Logic Record", initial=None):
        super().__init__(parent)
        self.title(title)
        self.on_submit = on_submit
        self.initial = initial or {}

        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()

        tk.Label(self, text="Record ID", anchor="w").grid(row=0, column=0, padx=8, pady=6, sticky="w")
        self.record_id_entry = tk.Entry(self, width=36)
        self.record_id_entry.grid(row=0, column=1, padx=8, pady=6)

        tk.Label(self, text="Output Group", anchor="w").grid(row=1, column=0, padx=8, pady=6, sticky="w")
        self.output_group_var = tk.StringVar()
        self.output_group_menu = tk.OptionMenu(self, self.output_group_var, *group_options)
        self.output_group_menu.config(width=32)
        self.output_group_menu.grid(row=1, column=1, padx=8, pady=6, sticky="w")
        if group_options:
            self.output_group_var.set(group_options[0])

        tk.Label(self, text="Priority", anchor="w").grid(row=2, column=0, padx=8, pady=6, sticky="w")
        self.priority_entry = tk.Entry(self, width=36)
        self.priority_entry.grid(row=2, column=1, padx=8, pady=6)

        tk.Label(self, text="Operator", anchor="w").grid(row=3, column=0, padx=8, pady=6, sticky="w")
        self.operator_var = tk.StringVar(value="AND")
        self.operator_menu = tk.OptionMenu(self, self.operator_var, "AND", "OR")
        self.operator_menu.config(width=32)
        self.operator_menu.grid(row=3, column=1, padx=8, pady=6, sticky="w")

        tk.Label(self, text="Desired State", anchor="w").grid(row=4, column=0, padx=8, pady=6, sticky="w")
        self.desired_state_var = tk.StringVar(value="on")
        self.desired_state_menu = tk.OptionMenu(self, self.desired_state_var, "on", "off")
        self.desired_state_menu.config(width=32)
        self.desired_state_menu.grid(row=4, column=1, padx=8, pady=6, sticky="w")

        tk.Label(self, text="Record Type", anchor="w").grid(row=5, column=0, padx=8, pady=6, sticky="w")
        self.record_type_var = tk.StringVar(value="standard")
        self.record_type_menu = tk.OptionMenu(self, self.record_type_var, "standard", "motion_hold")
        self.record_type_menu.config(width=32)
        self.record_type_menu.grid(row=5, column=1, padx=8, pady=6, sticky="w")

        tk.Label(self, text="Hold Seconds", anchor="w").grid(row=6, column=0, padx=8, pady=6, sticky="w")
        self.hold_seconds_entry = tk.Entry(self, width=36)
        self.hold_seconds_entry.grid(row=6, column=1, padx=8, pady=6)


        tk.Label(self, text="Conditions", anchor="w").grid(row=7, column=0, padx=8, pady=6, sticky="nw")
        self.conditions_text = tk.Text(self, width=50, height=12)
        self.conditions_text.grid(row=7, column=1, padx=8, pady=6)

        help_label = tk.Label(
            self,
            text="One condition per line: condition_id|source|operator|value",
            anchor="w",
            justify="left",
            font=("Arial", 9)
        )
        help_label.grid(row=8, column=1, padx=8, pady=(0, 6), sticky="w")

        button_row = tk.Frame(self)
        button_row.grid(row=9, column=0, columnspan=2, pady=12)

        tk.Button(button_row, text="Cancel", command=self.destroy).pack(side="left", padx=6)
        tk.Button(button_row, text="Save Logic Record", command=self.submit).pack(side="left", padx=6)

        self._load_initial()
        self.record_id_entry.focus_set()

    def _load_initial(self):
        if self.initial.get("id"):
            self.record_id_entry.insert(0, self.initial["id"])

        if self.initial.get("outputGroup"):
            self.output_group_var.set(self.initial["outputGroup"])

        if self.initial.get("priority", "") != "":
            self.priority_entry.insert(0, str(self.initial["priority"]))

        if self.initial.get("operator"):
            self.operator_var.set(self.initial["operator"])

        if self.initial.get("desiredState"):
            self.desired_state_var.set(self.initial["desiredState"])

        if self.initial.get("recordType"):
            self.record_type_var.set(self.initial["recordType"])

        if self.initial.get("holdSeconds", "") != "":
            self.hold_seconds_entry.insert(0, str(self.initial["holdSeconds"]))

        lines = []
        for cond in self.initial.get("conditions", []):
            lines.append(
                f"{cond.get('id','')}|{cond.get('source','')}|{cond.get('operator','')}|{cond.get('value','')}"
            )
        self.conditions_text.insert("1.0", "\n".join(lines))

    def submit(self):
        try:
            record_id = self.record_id_entry.get().strip()
            output_group = self.output_group_var.get().strip()
            priority = int(self.priority_entry.get().strip())
            operator = self.operator_var.get().strip()
            desired_state = self.desired_state_var.get().strip()

            record_type = self.record_type_var.get().strip()
            hold_seconds = self.hold_seconds_entry.get().strip()

            if record_type == "motion_hold":
                if not hold_seconds:
                    raise ValueError("Hold Seconds is required for motion_hold records")
                try:
                    if int(hold_seconds) <= 0:
                        raise ValueError
                except ValueError:
                    raise ValueError("Hold Seconds must be a positive whole number")
            else:
                hold_seconds = ""

            if not record_id:
                raise ValueError("Record ID cannot be empty")
            if not output_group:
                raise ValueError("Output group cannot be empty")

            raw_lines = self.conditions_text.get("1.0", "end").strip().splitlines()
            conditions = []

            for line in raw_lines:
                if not line.strip():
                    continue

                parts = [p.strip() for p in line.split("|")]
                while len(parts) < 4:
                    parts.append("")

                cond_id, source, cond_operator, value = parts[:4]

                if not cond_id:
                    raise ValueError("Each condition must have an ID")
                if not source:
                    raise ValueError(f"Condition '{cond_id}' must have a source")
                if not cond_operator:
                    raise ValueError(f"Condition '{cond_id}' must have an operator")

                conditions.append({
                    "id": cond_id,
                    "source": source,
                    "operator": cond_operator,
                    "value": value,
                })

            self.on_submit({
                "record_id": record_id,
                "output_group": output_group,
                "priority": priority,
                "operator": operator,
                "desired_state": desired_state,
                "record_type": record_type,
                "hold_seconds": hold_seconds,
                "conditions": conditions,
            })
            self.destroy()

        except ValueError as e:
            messagebox.showerror("Invalid Logic Record", str(e))

class GroupsPage(tk.Frame):
    def __init__(self, parent, app_state):
        super().__init__(parent)
        self.app_state = app_state

        title = tk.Label(self, text="Groups", font=("Arial", 20, "bold"))
        title.pack(pady=10)

        button_row = tk.Frame(self)
        button_row.pack(pady=(0, 8))

        tk.Button(button_row, text="Add Group", font=("Arial", 12), command=self.open_add_group_dialog).pack(side="left", padx=4)
        tk.Button(button_row, text="Edit Selected", font=("Arial", 12), command=self.open_edit_group_dialog).pack(side="left", padx=4)
        tk.Button(button_row, text="Delete Selected", font=("Arial", 12), command=self.delete_selected_group).pack(side="left", padx=4)

        self.listbox = tk.Listbox(self, width=80, height=18, exportselection=False, font=("Arial", 11))
        self.listbox.pack(fill="both", expand=True, padx=10, pady=10)

        self.group_ids = []

    def refresh(self):
        selected_group_id = self._selected_group_id()

        self.listbox.delete(0, tk.END)
        self.group_ids = []

        for group_id in sorted(self.app_state.groups.keys()):
            group = self.app_state.groups[group_id]
            members_text = ", ".join(group.get("members", []))
            line = f"{group_id}   aggregation={group.get('aggregation', 'any')}   members=[{members_text}]"
            self.listbox.insert(tk.END, line)
            self.group_ids.append(group_id)

        if selected_group_id and selected_group_id in self.group_ids:
            idx = self.group_ids.index(selected_group_id)
            self.listbox.selection_set(idx)
            self.listbox.activate(idx)
            self.listbox.see(idx)

    def _node_options(self):
        node_ids = []
        for module in self.app_state.modules.values():
            for relay in module.get("relays", {}).values():
                node_ids.append(relay["node_id"])
            for inp in module.get("inputs", {}).values():
                node_ids.append(inp["node_id"])
        return sorted(set(node_ids))

    def _selected_group_id(self):
        selection = self.listbox.curselection()
        if not selection:
            return None
        idx = selection[0]
        if idx >= len(self.group_ids):
            return None
        return self.group_ids[idx]

    def open_add_group_dialog(self):
        GroupDialog(
            self,
            node_options=self._node_options(),
            on_submit=self.handle_add_group,
            title="Add Group"
        )

    def handle_add_group(self, data):
        try:
            app = self.winfo_toplevel()
            app.add_group_to_config(
                group_id=data["group_id"],
                aggregation=data["aggregation"],
                members=data["members"],
            )
        except Exception as e:
            messagebox.showerror("Add Group Failed", str(e))

    def open_edit_group_dialog(self):
        group_id = self._selected_group_id()
        if not group_id:
            messagebox.showerror("No Selection", "Select a group to edit.")
            return

        group = self.app_state.groups[group_id]
        GroupDialog(
            self,
            node_options=self._node_options(),
            on_submit=lambda data: self.handle_edit_group(group_id, data),
            title="Edit Group",
            initial=group
        )

    def handle_edit_group(self, original_group_id, data):
        try:
            app = self.winfo_toplevel()
            app.update_group_in_config(
                original_group_id=original_group_id,
                new_group_id=data["group_id"],
                aggregation=data["aggregation"],
                members=data["members"],
            )
        except Exception as e:
            messagebox.showerror("Edit Group Failed", str(e))

    def delete_selected_group(self):
        group_id = self._selected_group_id()
        if not group_id:
            messagebox.showerror("No Selection", "Select a group to delete.")
            return

        if not messagebox.askyesno("Delete Group", f"Delete group '{group_id}'?"):
            return

        try:
            app = self.winfo_toplevel()
            app.delete_group_from_config(group_id)
        except Exception as e:
            messagebox.showerror("Delete Group Failed", str(e))

class LogicRecordsPage(tk.Frame):
    def __init__(self, parent, app_state):
        super().__init__(parent)
        self.app_state = app_state

        title = tk.Label(self, text="Logic Records", font=("Arial", 20, "bold"))
        title.pack(pady=10)

        button_row = tk.Frame(self)
        button_row.pack(pady=(0, 8))

        tk.Button(button_row, text="Add Record", font=("Arial", 12), command=self.open_add_dialog).pack(side="left", padx=4)
        tk.Button(button_row, text="Edit Selected", font=("Arial", 12), command=self.open_edit_dialog).pack(side="left", padx=4)
        tk.Button(button_row, text="Delete Selected", font=("Arial", 12), command=self.delete_selected).pack(side="left", padx=4)

        self.listbox = tk.Listbox(self, width=120, height=20, exportselection=False, font=("Arial", 10))
        self.listbox.pack(fill="both", expand=True, padx=10, pady=10)

        self.record_ids = []

    def refresh(self):
        selected_record_id = self._selected_record_id()

        self.listbox.delete(0, tk.END)
        self.record_ids = []

        for record_id in sorted(self.app_state.logic_records.keys()):
            record = self.app_state.logic_records[record_id]
            cond_count = len(record.get("conditions", []))
            line = (
                f"{record_id}   "
                f"type={record.get('recordType','standard')}   "
                f"outputGroup={record.get('outputGroup','')}   "
                f"priority={record.get('priority','')}   "
                f"operator={record.get('operator','')}   "
                f"desiredState={record.get('desiredState','')}   "
                f"holdSeconds={record.get('holdSeconds','')}   "
                f"conditions={cond_count}"
            )
            self.listbox.insert(tk.END, line)
            self.record_ids.append(record_id)

        if selected_record_id and selected_record_id in self.record_ids:
            idx = self.record_ids.index(selected_record_id)
            self.listbox.selection_set(idx)
            self.listbox.activate(idx)
            self.listbox.see(idx)

    def _selected_record_id(self):
        selection = self.listbox.curselection()
        if not selection:
            return None
        idx = selection[0]
        if idx >= len(self.record_ids):
            return None
        return self.record_ids[idx]

    def _group_options(self):
        return sorted(self.app_state.groups.keys())

    def open_add_dialog(self):
        group_options = self._group_options()
        if not group_options:
            messagebox.showerror("No Groups", "Create at least one group before adding logic records.")
            return

        LogicRecordDialog(
            self,
            group_options=group_options,
            on_submit=self.handle_add_record,
            title="Add Logic Record"
        )

    def handle_add_record(self, data):
        try:
            app = self.winfo_toplevel()
            app.add_logic_record_to_config(
                record_id=data["record_id"],
                output_group=data["output_group"],
                priority=data["priority"],
                operator=data["operator"],
                desired_state=data["desired_state"],
                conditions=data["conditions"],
                record_type=data["record_type"],
                hold_seconds=data["hold_seconds"],
            )
        except Exception as e:
            messagebox.showerror("Add Logic Record Failed", str(e))

    def open_edit_dialog(self):
        record_id = self._selected_record_id()
        if not record_id:
            messagebox.showerror("No Selection", "Select a logic record to edit.")
            return

        group_options = self._group_options()
        record = self.app_state.logic_records[record_id]

        LogicRecordDialog(
            self,
            group_options=group_options,
            on_submit=lambda data: self.handle_edit_record(record_id, data),
            title="Edit Logic Record",
            initial=record
        )

    def handle_edit_record(self, original_record_id, data):
        try:
            app = self.winfo_toplevel()
            app.update_logic_record_in_config(
                original_record_id=original_record_id,
                new_record_id=data["record_id"],
                output_group=data["output_group"],
                priority=data["priority"],
                operator=data["operator"],
                desired_state=data["desired_state"],
                conditions=data["conditions"],
                record_type=data["record_type"],
                hold_seconds=data["hold_seconds"],
            )
        except Exception as e:
            messagebox.showerror("Edit Logic Record Failed", str(e))

    def delete_selected(self):
        record_id = self._selected_record_id()
        if not record_id:
            messagebox.showerror("No Selection", "Select a logic record to delete.")
            return

        if not messagebox.askyesno("Delete Logic Record", f"Delete logic record '{record_id}'?"):
            return

        try:
            app = self.winfo_toplevel()
            app.delete_logic_record_from_config(record_id)
        except Exception as e:
            messagebox.showerror("Delete Logic Record Failed", str(e))

class LogicDebugPage(tk.Frame):
    def __init__(self, parent, app_state):
        super().__init__(parent)
        self.app_state = app_state
        self.debugger = LogicDebugger(app_state)

        self.previous_results = {}
        self.latched_results = {}
        self.show_only_changes = tk.BooleanVar(value=True)

        title = tk.Label(self, text="Logic Debug View", font=("Arial", 20, "bold"))
        title.pack(pady=10)

        controls = tk.Frame(self)
        controls.pack(fill="x", padx=10, pady=(0, 8))

        tk.Checkbutton(
            controls,
            text="Show latched changes only",
            variable=self.show_only_changes
        ).pack(side="left")

        tk.Button(
            controls,
            text="Clear Latched Changes",
            command=self.clear_history
        ).pack(side="left", padx=8)

        self.summary_label = tk.Label(
            controls,
            text="",
            font=("Arial", 11),
            justify="left",
            anchor="w"
        )
        self.summary_label.pack(side="right")

        self.text = tk.Text(self, font=("Courier", 10), wrap="none")
        self.text.pack(fill="both", expand=True, padx=10, pady=10)

        self.text.tag_config("true", foreground="green")
        self.text.tag_config("false", foreground="red")
        self.text.tag_config("changed", foreground="blue")
        self.text.tag_config("header", font=("Courier", 10, "bold"))

    def clear_history(self):
        self.latched_results = {}
        self.previous_results = {}
        self.refresh()

    def refresh(self):
        results = self.debugger.evaluate_all()
        now_text = self._now_text()

        for result in results:
            previous = self.previous_results.get(result["record_id"])
            result["changed"] = self._did_change(previous, result)
            result["change_text"] = self._build_change_text(previous, result)

            if result["changed"]:
                existing = self.latched_results.get(result["record_id"])
                change_count = 1
                first_changed = now_text

                if existing:
                    change_count = existing.get("change_count", 0) + 1
                    first_changed = existing.get("first_changed", now_text)

                latched = self._snapshot_full_result(result)
                latched["first_changed"] = first_changed
                latched["last_changed"] = now_text
                latched["change_count"] = change_count
                self.latched_results[result["record_id"]] = latched

            elif result["record_id"] in self.latched_results:
                # Keep latched entry visible, but refresh live fields
                existing = self.latched_results[result["record_id"]]
                latched = self._snapshot_full_result(result)
                latched["first_changed"] = existing.get("first_changed", now_text)
                latched["last_changed"] = existing.get("last_changed", now_text)
                latched["change_count"] = existing.get("change_count", 1)
                self.latched_results[result["record_id"]] = latched

        changed_count = len(self.latched_results)
        active_count = sum(1 for r in results if r["result"])
        total_count = len(results)

        self.summary_label.config(
            text=(
                f"Records: {total_count}    "
                f"Active: {active_count}    "
                f"Latched: {changed_count}    "
                f"Last update: {self._last_update_text()}"
            )
        )

        if self.show_only_changes.get():
            display_results = list(self.latched_results.values())
        else:
            display_results = []
            latched_map = self.latched_results

            for result in results:
                if result["record_id"] in latched_map:
                    merged = self._snapshot_full_result(result)
                    merged["first_changed"] = latched_map[result["record_id"]].get("first_changed", "")
                    merged["last_changed"] = latched_map[result["record_id"]].get("last_changed", "")
                    merged["change_count"] = latched_map[result["record_id"]].get("change_count", 0)
                    merged["latched"] = True
                    display_results.append(merged)
                else:
                    merged = self._snapshot_full_result(result)
                    merged["first_changed"] = ""
                    merged["last_changed"] = ""
                    merged["change_count"] = 0
                    merged["latched"] = False
                    display_results.append(merged)

        display_results.sort(
            key=lambda r: (
                not bool(r.get("change_count", 0)),
                not r["result"],
                self._safe_priority(r.get("priority")),
                r["record_id"]
            )
        )

        self.text.config(state="normal")
        self.text.delete("1.0", "end")

        if not display_results:
            if self.show_only_changes.get():
                self.text.insert("end", "No latched logic changes.\n")
            else:
                self.text.insert("end", "No logic records configured.\n")
        else:
            for result in display_results:
                self._render_record(result)

        self.text.config(state="disabled")
        self.text.yview_moveto(0.0)

        self.previous_results = {
            r["record_id"]: self._snapshot_result(r)
            for r in results
        }

    def _render_record(self, result):
        status = "TRUE" if result["result"] else "FALSE"
        status_tag = "true" if result["result"] else "false"

        latched = bool(result.get("change_count", 0))

        header = f"Record: {result['record_id']} [{status}]"
        if latched:
            header += f"  <-- LATCHED x{result.get('change_count', 0)}"

        self.text.insert("end", header + "\n", "header")
        self.text.insert("end", f"  Output Group : {result['output_group']}\n")
        self.text.insert("end", f"  Desired State: {result['desired_state']}\n")
        self.text.insert("end", f"  Priority     : {result['priority']}\n")
        self.text.insert("end", f"  Operator     : {result['operator']}\n")
        self.text.insert("end", f"  Result       : {status}\n", status_tag)

        if latched:
            self.text.insert("end", f"  First Changed: {result.get('first_changed', '')}\n", "changed")
            self.text.insert("end", f"  Last Changed : {result.get('last_changed', '')}\n", "changed")
            self.text.insert("end", f"  Change Count : {result.get('change_count', 0)}\n", "changed")

        if result.get("change_text"):
            self.text.insert("end", f"  Change       : {result['change_text']}\n", "changed")

        self.text.insert("end", f"  Explanation  : {result['explanation']}\n")
        self.text.insert("end", "  Conditions:\n")

        for cond in result["conditions"]:
            cond_status = "PASS" if cond["passed"] else "FAIL"
            cond_changed = cond.get("changed", False)

            line = (
                f"    - {cond['id']}: {cond_status}"
                f"{'  <-- changed' if cond_changed else ''}   "
                f"source={cond['source']}   "
                f"actual={cond['actual']}   "
                f"operator={cond['operator']}   "
                f"expected={cond['expected']}\n"
            )

            if cond_changed:
                self.text.insert("end", line, "changed")
            else:
                self.text.insert("end", line)

        self.text.insert("end", "\n" + "-" * 90 + "\n\n")

    def _did_change(self, previous, current):
        if previous is None:
            self._mark_condition_changes(None, current)
            return True

        record_changed = previous["result"] != current["result"]
        condition_changed = self._mark_condition_changes(previous, current)
        return record_changed or condition_changed

    def _mark_condition_changes(self, previous, current):
        previous_conditions = {}
        if previous:
            previous_conditions = {c["id"]: c for c in previous.get("conditions", [])}

        any_changed = False

        for cond in current["conditions"]:
            old = previous_conditions.get(cond["id"])
            changed = (
                old is None or
                old.get("passed") != cond.get("passed") or
                old.get("actual") != cond.get("actual")
            )
            cond["changed"] = changed
            if changed:
                any_changed = True

        return any_changed

    def _build_change_text(self, previous, current):
        if previous is None:
            return "First evaluation"

        changes = []

        if previous["result"] != current["result"]:
            old_status = "TRUE" if previous["result"] else "FALSE"
            new_status = "TRUE" if current["result"] else "FALSE"
            changes.append(f"record result {old_status} -> {new_status}")

        old_conditions = {c["id"]: c for c in previous.get("conditions", [])}
        for cond in current["conditions"]:
            old = old_conditions.get(cond["id"])
            if old is None:
                changes.append(f"condition {cond['id']} added")
                continue

            if old.get("actual") != cond.get("actual"):
                changes.append(f"{cond['id']} actual {old.get('actual')} -> {cond.get('actual')}")
            elif old.get("passed") != cond.get("passed"):
                old_pass = "PASS" if old.get("passed") else "FAIL"
                new_pass = "PASS" if cond.get("passed") else "FAIL"
                changes.append(f"{cond['id']} {old_pass} -> {new_pass}")

        return "; ".join(changes)

    def _snapshot_result(self, result):
        return {
            "record_id": result["record_id"],
            "result": result["result"],
            "priority": result["priority"],
            "conditions": [
                {
                    "id": c["id"],
                    "actual": c["actual"],
                    "passed": c["passed"],
                }
                for c in result["conditions"]
            ]
        }

    def _snapshot_full_result(self, result):
        return {
            "record_id": result["record_id"],
            "output_group": result["output_group"],
            "priority": result["priority"],
            "operator": result["operator"],
            "desired_state": result["desired_state"],
            "conditions": [
                {
                    "id": c["id"],
                    "source": c["source"],
                    "operator": c["operator"],
                    "expected": c["expected"],
                    "actual": c["actual"],
                    "passed": c["passed"],
                    "changed": c.get("changed", False),
                }
                for c in result["conditions"]
            ],
            "result": result["result"],
            "explanation": result["explanation"],
            "change_text": result.get("change_text", ""),
        }

    def _safe_priority(self, value):
        try:
            return -int(value)
        except (TypeError, ValueError):
            return 0

    def _last_update_text(self):
        if not self.app_state.last_update:
            return "never"
        return datetime.fromtimestamp(self.app_state.last_update).strftime("%Y-%m-%d %H:%M:%S")

    def _now_text(self):
        return datetime.now().strftime("%Y-%m-%d %H:%M:%S")

class LogicDebugger:
    def __init__(self, app_state):
        self.app_state = app_state

    def evaluate_all(self):
        results = []
        for record_id in sorted(self.app_state.logic_records.keys()):
            record = self.app_state.logic_records[record_id]
            results.append(self.evaluate_record(record))
        return results

    def evaluate_record(self, record):
        condition_results = []

        for cond in record.get("conditions", []):
            result = self.evaluate_condition(cond)
            condition_results.append(result)

        operator = str(record.get("operator", "AND")).upper()

        passed_values = [c["passed"] for c in condition_results]

        if not passed_values:
            overall = False
        elif operator == "OR":
            overall = any(passed_values)
        else:
            overall = all(passed_values)

        return {
            "record_id": record.get("id", ""),
            "output_group": record.get("outputGroup", ""),
            "priority": record.get("priority", ""),
            "operator": operator,
            "desired_state": record.get("desiredState", ""),
            "conditions": condition_results,
            "result": overall,
            "explanation": self._build_explanation(record, condition_results, overall),
        }

    def evaluate_condition(self, cond):
        source = cond.get("source", "")
        operator = str(cond.get("operator", "")).strip().lower()
        expected_raw = cond.get("value", "")

        actual_value = self.resolve_source_value(source)
        expected_value = self._normalize_value(expected_raw)

        passed = self._compare(actual_value, operator, expected_value)

        return {
            "id": cond.get("id", ""),
            "source": source,
            "operator": operator,
            "expected": expected_value,
            "actual": actual_value,
            "passed": passed,
        }

    def resolve_source_value(self, source):
        # node direct lookup
        for module in self.app_state.modules.values():
            for idx, relay in module.get("relays", {}).items():
                if relay.get("node_id") == source:
                    return int(module.get("relay_states", {}).get(idx, 0))
            for idx, inp in module.get("inputs", {}).items():
                if inp.get("node_id") == source:
                    return int(module.get("input_states", {}).get(idx, 0))

        # group lookup
        if source in self.app_state.groups:
            return self.evaluate_group(source)

        return None

    def evaluate_group(self, group_id):
        group = self.app_state.groups.get(group_id)
        if not group:
            return None

        member_values = []
        for ref in group.get("members", []):
            value = self.resolve_source_value(ref)
            member_values.append(bool(value))

        if not member_values:
            return 0

        aggregation = str(group.get("aggregation", "any")).lower()
        if aggregation == "all":
            return 1 if all(member_values) else 0
        return 1 if any(member_values) else 0

    def _normalize_value(self, value):
        if value is None:
            return None

        text = str(value).strip().lower()
        if text in ("1", "on", "true", "high"):
            return 1
        if text in ("0", "off", "false", "low"):
            return 0

        try:
            return int(text)
        except ValueError:
            return text

    def _compare(self, actual, operator, expected):
        if operator in ("is", "==", "equals", "eq"):
            return actual == expected
        if operator in ("!=", "not", "ne"):
            return actual != expected
        if operator in (">", "gt"):
            return actual is not None and expected is not None and actual > expected
        if operator in ("<", "lt"):
            return actual is not None and expected is not None and actual < expected
        if operator in (">=", "gte"):
            return actual is not None and expected is not None and actual >= expected
        if operator in ("<=", "lte"):
            return actual is not None and expected is not None and actual <= expected

        return False

    def _build_explanation(self, record, condition_results, overall):
        passed = sum(1 for c in condition_results if c["passed"])
        total = len(condition_results)
        return f"{record.get('operator', 'AND')} => {passed}/{total} conditions passed -> result={overall}"

class PlaceholderPage(tk.Frame):
    def __init__(self, parent, title):
        super().__init__(parent)
        tk.Label(self, text=title, font=("Arial", 20, "bold")).pack(pady=20)
        tk.Label(self, text="Page under development", font=("Arial", 14)).pack()

class TouchscreenApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Logic Engine")
        self.geometry("1024x600")
        self.attributes("-fullscreen", True)

        self.app_state = AppState()


        script_dir = Path(__file__).resolve().parent
        project_root = script_dir.parent
        config_path = project_root / "config" / "logic_config.xml"
        runtime_status_path = project_root / "runtime_status.json"
        print(f"Loading config from: {config_path}")

        # self.config_loader = LogicConfigLoader(config_path)
        # self.app_state.modules = self.config_loader.load_modules()

        self.config_path = config_path
        self.config_loader = LogicConfigLoader(self.config_path)
        self.config_writer = LogicConfigWriter(self.config_path)
        self.app_state.modules = self.config_loader.load_modules()
        self.app_state.groups = self.config_loader.groups
        self.app_state.logic_records = self.config_loader.logic_records

        self.runtime_reader = RuntimeStatusReader(runtime_status_path)

        self.ros_if = RosInterface(self.app_state, on_update=self.refresh_ui)

        self.header = HeaderBar(self, self.app_state)
        self.header.pack(fill="x")

        body = tk.Frame(self)
        body.pack(fill="both", expand=True)

        self.nav = NavigationFrame(body, self.show_page)
        self.nav.pack(side="left", fill="y")

        self.main = tk.Frame(body)
        self.main.pack(side="right", fill="both", expand=True)

        self.pages = {
            "dashboard": DashboardPage(self.main, self.app_state),
            "modules": ModulesPage(self.main, self.app_state, self.ros_if),
            "schedules": PlaceholderPage(self.main, "Schedules"),
            "groups": GroupsPage(self.main, self.app_state),
            "logic": LogicRecordsPage(self.main, self.app_state),
            "settings": PlaceholderPage(self.main, "Settings"),
            "debug": LogicDebugPage(self.main, self.app_state),
        }

        for page in self.pages.values():
            page.place(relx=0, rely=0, relwidth=1, relheight=1)

        self.show_page("dashboard")
        self.bind("<Escape>", lambda e: self.attributes("-fullscreen", False))

        self.ros_if.connect()

        # Optional debug print
        for key, module in self.app_state.modules.items():
            print(
                f"{key}: name={module['name']}, "
                f"relays={sorted(module['relays'].keys())}, "
                f"inputs={sorted(module['inputs'].keys())}"
            )

        # Optional mock live updates for testing
        # self.ros_if.update_module_state("1", relay_states={1: 1}, input_states={0: 1, 3: 0})
        # self.ros_if.update_module_state("6", relay_states={1: 1, 2: 0}, input_states={1: 1})
        # self.ros_if.update_module_state("7", relay_states={1: 1, 10: 1}, input_states={})

        self.periodic_refresh()

    def reload_config(self):
        self.config_loader = LogicConfigLoader(self.config_path)
        self.app_state.modules = self.config_loader.load_modules()
        self.app_state.groups = self.config_loader.groups
        self.app_state.logic_records = self.config_loader.logic_records
        self.refresh_ui()

    def add_module_to_config(
        self,
        module_id,
        friendly_name,
        location="",
        host="",
        ip="",
        relay_count=8,
        input_count=4,
        enabled=True,
        role=""
    ):
        self.config_writer.add_module(
            module_id=module_id,
            friendly_name=friendly_name,
            location=location,
            host=host,
            ip=ip,
            relay_count=relay_count,
            input_count=input_count,
            enabled=enabled,
            role=role,
        )
        self.reload_config()

    def periodic_refresh(self):
        runtime_data = self.runtime_reader.load()
        if runtime_data:
                apply_runtime_status(
                    self.app_state,
                    runtime_data,
                    self.config_loader.node_map,
                    stale_seconds=15
                )


                apply_runtime_node_states(
                    self.app_state,
                    runtime_data,
                    self.config_loader.node_map
                )
                update_module_online_from_runtime(
                    self.app_state,
                    runtime_data,
                    hold_seconds=30
                )
        mark_stale_modules(self.app_state, timeout_seconds=15)
        self.refresh_ui()
        self.after(1000, self.periodic_refresh)

    def show_page(self, page_name):
        self.pages[page_name].tkraise()
        self.current_page = page_name
        self.refresh_ui()

    def refresh_ui(self):
        self.header.refresh()
        if hasattr(self, "current_page"):
            self.pages[self.current_page].refresh()

    def add_node_to_config(
        self,
        node_id,
        module_id,
        kind,
        index,
        friendly_name=""
    ):
        self.config_writer.add_node(
            node_id=node_id,
            module_id=module_id,
            kind=kind,
            index=index,
            friendly_name=friendly_name,
        )
        self.reload_config()

    def add_group_to_config(self, group_id, aggregation="any", members=None):
        self.config_writer.add_group(
            group_id=group_id,
            aggregation=aggregation,
            members=members or [],
        )
        self.reload_config()

    def update_group_in_config(self, original_group_id, new_group_id, aggregation="any", members=None):
        self.config_writer.update_group(
            original_group_id=original_group_id,
            new_group_id=new_group_id,
            aggregation=aggregation,
            members=members or [],
        )
        self.reload_config()

    def delete_group_from_config(self, group_id):
        
        self.config_writer.delete_group(group_id)
        self.reload_config()

    def add_logic_record_to_config(
        self,
        record_id,
        output_group,
        priority,
        operator,
        desired_state,
        conditions=None,
        record_type="standard",
        hold_seconds=""
    ):
        self.config_writer.add_logic_record(
            record_id=record_id,
            output_group=output_group,
            priority=priority,
            operator=operator,
            desired_state=desired_state,
            conditions=conditions or [],
            record_type=record_type,
            hold_seconds=hold_seconds,
        )
        self.reload_config()

    def update_logic_record_in_config(
        self,
        original_record_id,
        new_record_id,
        output_group,
        priority,
        operator,
        desired_state,
        conditions=None,
        record_type="standard",
        hold_seconds=""
    ):
        self.config_writer.update_logic_record(
            original_record_id=original_record_id,
            new_record_id=new_record_id,
            output_group=output_group,
            priority=priority,
            operator=operator,
            desired_state=desired_state,
            conditions=conditions or [],
            record_type=record_type,
            hold_seconds=hold_seconds,
        )
        self.reload_config()

    def delete_logic_record_from_config(self, record_id):
        self.config_writer.delete_logic_record(record_id)
        self.reload_config()

if __name__ == "__main__":
    app = TouchscreenApp()
    app.mainloop()