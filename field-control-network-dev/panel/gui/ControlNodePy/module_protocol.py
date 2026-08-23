# module_protocol.py

class ModuleProtocol:

    @staticmethod
    def parse(msg):
        parts = msg.split(";")
        fields = dict(p.split(":") for p in parts[1:])
        return {
            "module": parts[0],
            "inputs": list(map(int, fields["I"].split(","))),
            "relays": list(map(int, fields["R"].split(","))),
        }

    @staticmethod
    def cmd(module, relay_idx, state):
        return f"{module};{relay_idx+1};{'ON' if state else 'OFF'}"
