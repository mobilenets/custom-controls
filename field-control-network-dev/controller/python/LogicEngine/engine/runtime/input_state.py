# engine/input_state.py

class InputState:
    def __init__(self):
        self.previous = None
        self.current = None

    def update(self, value):
        self.previous = self.current
        self.current = value

    def rising_edge(self) -> bool:
        return self.previous is False and self.current is True

    def falling_edge(self) -> bool:
        return self.previous is True and self.current is False
