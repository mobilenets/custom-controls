# node.py (excerpt)

from module_registry import ModuleRegistry
from module_protocol import ModuleProtocol

class ModuleNode(Node):
    def __init__(self):
        super().__init__("module_node")

        self.registry = ModuleRegistry(timeout=3.0)
        self.states = {}  # module_id -> ModuleState

        self.sub = self.create_subscription(
            String,
            "/modulereturn",
            self.status_cb,
            10
        )

        self.create_timer(0.5, self.registry.update)

    def status_cb(self, msg):
        data = ModuleProtocol.parse(msg.data)
        module_id = data["module"]

        # Register / heartbeat
        self.registry.register_seen(module_id)

        # Create state on first sight
        if module_id not in self.states:
            self.states[module_id] = ModuleState()

        self.states[module_id].inputs = data["inputs"]
        self.states[module_id].relays = data["relays"]
