from datetime import datetime
from engine.runtime.input_state import InputState
from engine.logic.logic_record_def import LogicRecordDef,LogicalOperator
from engine.runtime.enumerators import Tri


class LogicRecordRuntime:
    def __init__(self, definition: LogicRecordDef):
        self.defn = definition
        self.input_states = {}   # key -> InputState
        self.last_fired = None
        

    def eval(self, engine) -> Tri:
        """
        engine gives access to nodes, group_states, etc
        """
        results = []

        for cond in self.defn.conditions:
            cond_op = cond.operator.value if hasattr(cond.operator, "value") else cond.operator

            if cond_op in ("rising_edge", "falling_edge"):
                state = self.input_states.setdefault(cond.source_ref, InputState())

                raw = engine.get_source_value(cond.source_ref)
                state.update(raw)

                if state.previous is None:
                    results.append(Tri.PASS)
                    continue

                if cond_op == "rising_edge":
                    results.append(Tri.TRUE if state.rising_edge() else Tri.FALSE)
                    continue

                if cond_op == "falling_edge":
                    results.append(Tri.TRUE if state.falling_edge() else Tri.FALSE)
                    continue

                results.append(Tri.PASS)
                continue

            if cond_op == "time_equals":
                state = self.input_states.setdefault(cond.source_ref, InputState())

                raw = engine.get_source_value(cond.source_ref)   # "HH:MM"
                state.update(raw)

                if state.previous is None:
                    results.append(Tri.PASS)
                    continue

                # Fire once when the minute changes into the target minute
                if state.current == cond.value and state.previous != cond.value:
                    results.append(Tri.TRUE)
                else:
                    results.append(Tri.FALSE)
                continue
            
            # Non-edge condition
            results.append(engine.eval_condition(cond))
        
        
        # Existing tri-state logic
        op = self.defn.operator.value if hasattr(self.defn.operator, "value") else self.defn.operator
        print(f"LR {self.defn.record_id} op={op} results={results} contains_FALSE={Tri.FALSE in results}")
        if op == "AND":

            if Tri.FALSE in results:
                return Tri.FALSE
            if all(r == Tri.TRUE for r in results):
                return Tri.TRUE
            return Tri.PASS

        if op == "OR":

            if Tri.TRUE in results:
                return Tri.TRUE
            if all(r == Tri.FALSE for r in results):
                return Tri.FALSE
            return Tri.PASS

        return Tri.PASS
