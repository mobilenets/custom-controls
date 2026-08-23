# module_registry.py

import time

class ModuleRegistry:
    def __init__(self, timeout=5.0):
        self.timeout = timeout
        self.modules = {}  # module_id -> info

    def register_seen(self, module_id):
        now = time.time()

        if module_id not in self.modules:
            self.modules[module_id] = {
                "last_seen": now,
                "online": True
            }
        else:
            self.modules[module_id]["last_seen"] = now
            self.modules[module_id]["online"] = True

    def update(self):
        now = time.time()
        for m in self.modules.values():
            if now - m["last_seen"] > self.timeout:
                m["online"] = False

    def is_online(self, module_id):
        return self.modules.get(module_id, {}).get("online", False)

    def all_modules(self):
        return self.modules
