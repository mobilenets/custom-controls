import time
from typing import List
from logic_record import LogicRecord


class LogicEngine:
    def __init__(self, tick_interval: float = 1.0):
        self.logic_records: List[LogicRecord] = []
        self.runtime_state = {}
        self.tick_interval = tick_interval

    def tick(self):
        print("\n--- ENGINE TICK ---")
        for record in self.logic_records:
            result = record.resolve(self.runtime_state)
            print(result)

    def run_forever(self):
        step = 0
        while True:
            print(f"\nTick {step}")
            self.tick()
            step += 1
            time.sleep(self.tick_interval)
