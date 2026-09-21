import time
from datetime import datetime
from dataclasses import dataclass, field
from typing import List, Dict, Any, Optional
from enum import Enum
from engine.runtime.node_runtime import NodeRuntime
from engine.logic.group_def import GroupDef

from engine.runtime.logic_record_runtime import LogicRecordRuntime
from engine.runtime.enumerators import Tri
from engine.logic.logic_record_def import LogicRecordDef
from engine.logic.condition_def import ConditionDef
from config.xml_loader  import load_logic_config
# ---------- Tri-state ----------
""" class Tri(Enum):
    TRUE = True
    FALSE = False
    PASS = None
 """








# ---------- Engine ----------
class LogicEngine:
    def __init__(
        self,
        nodes: Dict[str, NodeRuntime],
        groups: Dict[str, GroupDef],
        logic_records: List[LogicRecordDef],
    ):
        self.nodes = nodes
        self.groups = groups
        ##self.logic_records = logic_records
        self.logic_records = [
        LogicRecordRuntime(lr) for lr in logic_records
        ]
        self.group_states: Dict[str, Tri] = {}
        self.prev_group_states: Dict[str, Tri] = {}   # <-- NEW
        self.flags: Dict[str, Any] = {"shop_lights_override": False}

        self.tick_seconds = 0.5
        self.motion_holds: Dict[str, Dict[str, Any]] = {}

    def _seconds_to_steps(self, seconds: Optional[float]) -> int:
        try:
            s = float(seconds or 0)
        except (TypeError, ValueError):
            s = 0
        return max(1, int(round(s / self.tick_seconds)))

    def _group_is_on(self, gid: str) -> bool:
        tri = self.group_states.get(gid, Tri.PASS)
        return tri == Tri.TRUE

    def _motion_hold_is_active(self, gid: str) -> bool:
        return bool(self.motion_holds.get(gid, {}).get("active", False))

    def _start_or_reset_motion_hold(self, gid: str, hold_seconds: float, record_id: str, step: int):
        hold_steps = self._seconds_to_steps(hold_seconds)

        state = self.motion_holds.setdefault(gid, {
            "active": False,
            "expires_step": 0,
            "hold_steps": hold_steps,
            "source_records": set(),
        })

        state["active"] = True
        state["hold_steps"] = hold_steps
        state["expires_step"] = step + hold_steps
        state["source_records"].add(record_id)

    def _clear_motion_hold(self, gid: str):
        if gid in self.motion_holds:
            self.motion_holds[gid]["active"] = False

    def get_source_value(self, ref: str):

        if ref == "clock":
            return datetime.now().strftime("%H:%M")
        
        # examine override flags
        if ref in self.flags:
            return self.flags[ref]
        # Groups: return a raw value (True/False/None) to support edges cleanly
        if ref in self.group_states:
            tri = self.group_states[ref]
            return tri.value  # True / False / None

        # Nodes: return whatever the node is currently holding (usually bool)
        if ref in self.nodes:
            return self.nodes[ref].state

        return None

    def eval_motion_record(self, lr: LogicRecordRuntime, step: int) -> Tri:
        defn = lr.defn

        # Suggested XML/defn fields:
        # defn.record_type == "motion_hold"
        # defn.output_group
        # defn.hold_seconds
        #
        # lr.eval(self) should still handle the rising-edge detection
        # based on your existing runtime logic record behavior.

        result = lr.eval(self)
        print(f"MotionRecord {defn.record_id} -> {result}")

        if result != Tri.TRUE:
            return Tri.PASS

        gid = defn.output_group
        hold_seconds = getattr(defn, "hold_seconds", 60)

        group_is_on = self._group_is_on(gid)
        motion_is_active = self._motion_hold_is_active(gid)

        # Case 1: group off -> turn on + start timer
        if not group_is_on:
            self._start_or_reset_motion_hold(gid, hold_seconds, defn.record_id, step)
            return Tri.TRUE

        # Case 2: group already on from motion -> reset timer
        if motion_is_active:
            self._start_or_reset_motion_hold(gid, hold_seconds, defn.record_id, step)
            return Tri.TRUE

        # Case 3: group already on from some other reason -> ignore
        return Tri.PASS

    def get_expired_motion_groups(self, step: int) -> List[str]:
        expired = []

        for gid, state in self.motion_holds.items():
            if not state.get("active"):
                continue
            if step >= state.get("expires_step", 0):
                expired.append(gid)

        return expired

    # ----- Group resolution -----
    def resolve_group_state(self, group: GroupDef) -> Tri:
        vals = []

        for nid in group.members:
            node = self.nodes.get(nid)
            if node is None:
                continue

            v = node.state

            # Normalize Tri -> raw value
            if isinstance(v, Tri):
                v = v.value  # True / False / None

            # Skip undefined
            if v is None:
                continue

            # Only aggregate booleans
            vals.append(bool(v))

        if not vals:
            return Tri.PASS

        if group.aggregation == "any":
            return Tri.TRUE if any(vals) else Tri.FALSE

        if group.aggregation == "all":
            return Tri.TRUE if all(vals) else Tri.FALSE

        return Tri.PASS

    # ----- Condition evaluation -----
    def eval_condition(self, cond: ConditionDef) -> Tri:


        op = cond.operator.value if hasattr(cond.operator, "value") else cond.operator

        source_val = self.get_source_value(cond.source_ref)

        if op == "equals":
            out = Tri.TRUE if source_val == cond.value else Tri.FALSE
            print(f"COND {cond.condition_id}: {cond.source_ref}={source_val} == {cond.value} -> {out}")
            return out
      

        if source_val is None:
            return cond.on_undefined
        
        if op == "time_after":
            out = Tri.TRUE if source_val >= cond.value else Tri.FALSE
            print(f"COND {cond.condition_id}: {cond.source_ref}={source_val} >= {cond.value} -> {out}")
            return out

        if op == "time_before":
            out = Tri.TRUE if source_val < cond.value else Tri.FALSE
            print(f"COND {cond.condition_id}: {cond.source_ref}={source_val} < {cond.value} -> {out}")
            return out
        
        if op == "equals":
            return Tri.TRUE if source_val == cond.value else Tri.FALSE

        if op == "day_in":
            today = datetime.now().strftime("%a").lower()[:3]   # mon, tue, wed...
            allowed_days = [d.strip().lower()[:3] for d in str(cond.value).split(",")]
            out = Tri.TRUE if today in allowed_days else Tri.FALSE
            print(f"COND {cond.condition_id}: day={today} in {allowed_days} -> {out}")
            return out


        # Edge ops handled in LogicRecordRuntime
        return Tri.PASS

    # ----- LogicRecord evaluation -----
    def eval_logic_record(self, lr: LogicRecordDef) -> Tri:
        results = []
        for cond in lr.conditions:
            r = self.eval_condition(cond)
            results.append(r)

        if lr.operator == "AND":
            if Tri.FALSE in results:
                return Tri.FALSE
            if all(r == Tri.TRUE for r in results):
                return Tri.TRUE
            return Tri.PASS

        if lr.operator == "OR":
            if Tri.TRUE in results:
                return Tri.TRUE
            if all(r == Tri.FALSE for r in results):
                return Tri.FALSE
            return Tri.PASS

        return Tri.PASS

    # ----- Tick -----
    def tick(self, step: int):
        print(f"\n--- ENGINE TICK {step} ---")
        self.prev_group_states = dict(self.group_states)

        # Resolve groups from actual node state first
        for gid, group in self.groups.items():
            self.group_states[gid] = self.resolve_group_state(group)
            print(f"Group {gid} -> {self.group_states[gid]}")

        winners: Dict[str, List[LogicRecordRuntime]] = {}

        # 1) Evaluate logic records
        for lr in self.logic_records:
            record_type = getattr(lr.defn, "record_type", "standard")

            if record_type == "motion_hold":
                result = self.eval_motion_record(lr, step)
            else:
                result = lr.eval(self)
                print(f"LogicRecord {lr.defn.record_id} -> {result}")

            if result == Tri.TRUE:
                winners.setdefault(lr.defn.output_group, []).append(lr)

        # 2) Add synthetic OFF winners for expired motion holds
        expired_motion_groups = self.get_expired_motion_groups(step)
        for gid in expired_motion_groups:
            print(f"Motion hold expired for {gid}")

            # only create an OFF action if the group is currently on
            if self._group_is_on(gid):
                winners.setdefault(gid, [])

                class _SyntheticOff:
                    def __init__(self, gid: str):
                        self.defn = type("Defn", (), {})()
                        self.defn.record_id = f"motion_timeout_{gid}"
                        self.defn.output_group = gid
                        self.defn.priority = 1000000
                        self.defn.desired_state = False
                        self.defn.last_reason = ""
                        self.defn.history = []

                winners[gid].append(_SyntheticOff(gid))
            else:
                self._clear_motion_hold(gid)
                
        # 3) Resolve priority per group
        for gid, records in winners.items():
            records.sort(key=lambda r: r.defn.priority, reverse=True)
            winner = records[0].defn

            if winner.record_id == "lr_manual_on":
                self.flags["shop_lights_override"] = True
            elif winner.record_id == "lr_manual_off":
                self.flags["shop_lights_override"] = False

            print(f"Override shop_lights_override => {self.flags['shop_lights_override']}")

            reason = f"Won by priority {winner.priority}"
            winner.last_reason = reason
            if hasattr(winner, "history"):
                winner.history.append(reason)

            print(f">>> APPLY {gid} via {winner.record_id}")
            print(f"    Suppressed: {[r.defn.record_id for r in records[1:]]}")

            desired = winner.desired_state
            current = self.group_states.get(gid, Tri.PASS).value

            if current != desired:
                for nid in self.groups[gid].members:
                    self.nodes[nid].state = desired

            print(f"APPLIED {gid} => {'ON' if desired else 'OFF'}")

            # if a motion timeout won, clear the motion hold after applying OFF
            if winner.record_id == f"motion_timeout_{gid}":
                self._clear_motion_hold(gid)

# ---------- DEMO SETUP ----------
def main():
    nodes, groups, logic_records,node_map = load_logic_config(
        "config/logic_config.xml"
    )
    engine = LogicEngine(nodes, groups, logic_records)

    step = 0
    while True:
        # Simulated inputs
        nodes["pir1"].state = step % 5 == 0
        if step % 9 == 0:
            nodes["switch1"].state = not bool(nodes["switch1"].state)

        engine.tick(step)
        step += 1
        time.sleep(1)

if __name__ == "__main__":
    main()