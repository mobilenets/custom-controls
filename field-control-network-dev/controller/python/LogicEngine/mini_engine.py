from datetime import datetime
from collections import defaultdict
import time

# -----------------------------
# Intent
# -----------------------------
class Intent:
    def __init__(self, output, state, intent_type, source):
        self.output = output
        self.state = state
        self.type = intent_type
        self.source = source


# -----------------------------
# Logic Record (minimal)
# -----------------------------
class LogicRecord:
    def __init__(self, timestamp, output, state, intent_type, intent_source):
        self.timestamp = timestamp
        self.output = output
        self.state = state
        self.intent_type = intent_type
        self.intent_source = intent_source

    def __eq__(self, other):
        if other is None:
            return False
        return (
            self.output == other.output and
            self.state == other.state and
            self.intent_type == other.intent_type and
            self.intent_source == other.intent_source
        )


# -----------------------------
# Resolution logic
# -----------------------------
INTENT_PRIORITY = {
    "override": 100,
    "manual": 80,
    "schedule": 60,
    "occupancy": 40,
    "automation": 20
}


def constraints_allow(intent, current_record):
    # Occupancy cannot override manual intent
    if current_record:
        if current_record.intent_type == "manual" and intent.type == "occupancy":
            return False
    return True


def choose_intent(intents):
    return max(intents, key=lambda i: INTENT_PRIORITY.get(i.type, 0))


def resolve_output(output, current_record, intents, now):
    if not intents:
        return current_record

    allowed = [
        i for i in intents
        if constraints_allow(i, current_record)
    ]

    if not allowed:
        return current_record

    winner = choose_intent(allowed)

    if current_record and winner.state == current_record.state:
        return current_record

    return LogicRecord(
        timestamp=now,
        output=output,
        state=winner.state,
        intent_type=winner.type,
        intent_source=winner.source
    )


# -----------------------------
# Mini Engine
# -----------------------------
class MiniEngine:
    def __init__(self):
        self.outputs = set()
        self.logic_records = {}
        self.intent_queue = defaultdict(list)

    def register_output(self, output_name):
        self.outputs.add(output_name)

    def inject_intent(self, intent):
        self.outputs.add(intent.output)
        self.intent_queue[intent.output].append(intent)

    def tick(self, now=None):
        if now is None:
            now = datetime.utcnow()

        print(f"\n--- Engine Tick @ {now.isoformat()} ---")

        for output in self.outputs:
            current_record = self.logic_records.get(output)
            intents = self.intent_queue.get(output, [])

            new_record = resolve_output(
                output=output,
                current_record=current_record,
                intents=intents,
                now=now
            )

            if new_record != current_record:
                self.logic_records[output] = new_record
                self.apply_output(new_record)
            else:
                print(f"{output}: no change")

        # clear intents after processing
        self.intent_queue.clear()

    def apply_output(self, record):
        print(
            f"{record.output} -> {record.state} "
            f"(by {record.intent_type} from {record.intent_source})"
        )


# -----------------------------
# Demo runner
# -----------------------------
if __name__ == "__main__":
    engine = MiniEngine()
    engine.register_output("Shop Lights")

    step = 0
    tick_interval = 1.0

    try:
        while True:
            # PIR intent every 5 ticks
            if step % 5 == 0:
                engine.inject_intent(
                    Intent(
                        output="Shop Lights",
                        state="on",
                        intent_type="occupancy",
                        source="pir_1"
                    )
                )

            # Manual override at step 7
            if step == 7:
                engine.inject_intent(
                    Intent(
                        output="Shop Lights",
                        state="off",
                        intent_type="manual",
                        source="door_switch_1"
                    )
                )

            engine.tick()
            step += 1
            time.sleep(tick_interval)

    except KeyboardInterrupt:
        print("\nEngine stopped.")
