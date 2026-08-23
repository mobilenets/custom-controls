# module_state.py

class ModuleState:
    def __init__(self):
        self.inputs = [0, 0, 0, 0]
        self.relays = [0, 0, 0, 0]
        self.override = [False] * 4
