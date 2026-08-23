# relay_controller.py

class RelayController:
    def __init__(self, state):
        self.state = state

    def toggle(self, idx):
        self.state.relays[idx] = not self.state.relays[idx]
        return self.state.relays[idx]
