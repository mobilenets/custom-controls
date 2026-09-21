import xml.etree.ElementTree as ET
from typing import Dict, List
from engine.runtime.node_runtime import NodeRuntime
from engine.logic.group_def import GroupDef
from engine.logic.logic_record_def import LogicRecordDef
from engine.logic.condition_def import ConditionDef
from engine.runtime.enumerators import Tri
from engine.logic.node_map import NodeMapEntry



def load_logic_config(path: str):
    tree = ET.parse(path)
    root = tree.getroot()

    config = root.find("config")

    # ---------- Nodes ----------
    node_map: Dict[str, NodeMapEntry] = {}

    nodes: Dict[str, NodeRuntime] = {}
    for node_el in config.find("nodes").findall("node"):
        node_id = node_el.attrib["id"]
        nodes[node_id] = NodeRuntime(node_id=node_id)

        # Optional mapping fields (only required for ROS bridge)
        module_id = node_el.attrib.get("moduleId")
        kind = node_el.attrib.get("kind")
        index = node_el.attrib.get("index")
        friendly = node_el.attrib.get("friendlyName")

        if module_id is not None and kind is not None and index is not None:
            node_map[node_id] = NodeMapEntry(
                node_id=node_id,
                module_id=str(module_id).strip(),
                kind=kind.strip(),
                index=int(index),
                friendly_name=friendly,
            )

    # ---------- Groups ----------
    groups: Dict[str, GroupDef] = {}
    for g in config.find("groups").findall("group"):
        gid = g.attrib["id"]
        aggregation = g.attrib.get("aggregation", "any")
        members = [m.attrib["ref"] for m in g.findall("member")]

        groups[gid] = GroupDef(
            group_id=gid,
            members=members,
            aggregation=aggregation,
        )

    # ---------- Logic Records ----------
    logic_records: List[LogicRecordDef] = []

    lr_root = config.find("logicEngine").find("logicRecords")
    for lr in lr_root.findall("logicRecord"):
        record_id = lr.attrib["id"]
        output_group = lr.attrib["outputGroup"]
        priority = int(lr.attrib.get("priority", "0"))
        operator = lr.attrib.get("operator", "AND")

        desired_state_str = lr.attrib.get("desiredState", "on").lower()
        if desired_state_str not in ("on", "off"):
            raise ValueError(f"logicRecord {record_id}: desiredState must be 'on' or 'off'")
        desired_state = True if desired_state_str == "on" else False

        record_type = lr.attrib.get("recordType", "standard")
        hold_seconds = float(lr.attrib.get("holdSeconds", "60"))

        conditions: List[ConditionDef] = []
        for c in lr.findall("condition"):
            op = c.attrib["operator"]

            if op in ("rising_edge", "falling_edge"):
                value = None
            else:
                value = c.attrib.get("value")
                if value == "true":
                    value = True
                elif value == "false":
                    value = False

            conditions.append(
                ConditionDef(
                    condition_id=c.attrib["id"],
                    source_ref=c.attrib["source"],
                    operator=op,
                    value=value,
                    on_undefined=Tri.PASS,
                )
            )

        print(
            record_id,
            "type=", record_type,
            "hold=", hold_seconds,
            "conds=", len(conditions),
            [c.condition_id for c in conditions]
        )

        logic_records.append(
            LogicRecordDef(
                record_id=record_id,
                output_group=output_group,
                priority=priority,
                operator=operator,
                desired_state=desired_state,
                conditions=conditions,
                record_type=record_type,
                hold_seconds=hold_seconds,
            )
        )
    return nodes, groups, logic_records, node_map
