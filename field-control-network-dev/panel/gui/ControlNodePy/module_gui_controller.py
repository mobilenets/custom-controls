import rclpy
from rclpy.node import Node
from std_msgs.msg import String
from rclpy.qos import QoSProfile, ReliabilityPolicy
import tkinter as tk
import threading
import time
import random

# ---------------- ROS QoS ----------------
qos = QoSProfile(depth=10, reliability=ReliabilityPolicy.BEST_EFFORT)

# ---------------- Module State ----------------
class ModuleState:
    """Stores the inputs, relays, and last heartbeat time for a module"""
    def __init__(self):
        self.inputs = [0, 0, 0, 0]
        self.relays = [0, 0, 0, 0]
        self.last_seen = time.time()

# ---------------- Module Registry ----------------
class ModuleRegistry:
    """Tracks all module states in insertion order"""
    def __init__(self):
        from collections import OrderedDict
        self.modules = OrderedDict()  # module_id -> ModuleState

    def get_or_create(self, module_id):
        if module_id not in self.modules:
            self.modules[module_id] = ModuleState()
        return self.modules[module_id]

    def all_module_ids(self):
        return list(self.modules.keys())

# ---------------- GUI + ROS Node ----------------
class ModuleGUI(Node):
    def __init__(self, root, registry, module_ids):
        super().__init__("module_gui_controller")
        self.registry = registry
        self.root = root
        self.module_ids = module_ids

        # Publisher & subscriber
        self.pub = self.create_publisher(String, "/actionrequest", qos)
        self.sub = self.create_subscription(String, "/modulereturn", self.status_cb, qos)

        # GUI storage
        self.gui_nodes = {}

        # Initialize GUI for all modules
        for module_id in module_ids:
            self.create_module_gui(module_id)

        # GUI update loop
        self.root.after(100, self.gui_update)

        # Start ROS spinning in a background thread
        threading.Thread(target=self.start_ros_spin, daemon=True).start()

    # ---------------- ROS spin ----------------
    def start_ros_spin(self):
        while rclpy.ok():
            rclpy.spin_once(self, timeout_sec=0.1)

    # ---------------- Handle incoming messages ----------------
    def status_cb(self, msg):
        try:
            parts = msg.data.split(";")
            module_id = parts[0]
            state = self.registry.get_or_create(module_id)

            fields = {}
            for p in parts[1:]:
                if ":" in p:
                    key, val = p.split(":")
                    fields[key.strip()] = val.strip()

            # Update state
            state.relays = [int(x) for x in fields.get("R", "0,0,0,0").split(",")]
            state.inputs = [int(x) for x in fields.get("I", "0,0,0,0").split(",")]
            state.last_seen = time.time()

            # Create GUI if new module
            if module_id not in self.gui_nodes:
                self.create_module_gui(module_id)

            # Debug log
            self.get_logger().info(f"Module {module_id}: inputs={state.inputs}, relays={state.relays}")

        except Exception as e:
            self.get_logger().warn(f"Parse error: {e}")

    # ---------------- Create GUI for a module ----------------
    def create_module_gui(self, module_id):
        row = len(self.gui_nodes) * 2
        buttons = []
        inputs = []

        for i in range(4):
            btn = tk.Button(
                self.root,
                text="OFF",
                width=12,
                command=lambda i=i, m_id=module_id: self.on_toggle(m_id, i)
            )
            btn.grid(row=row, column=i, padx=5, pady=5)
            buttons.append(btn)

            lbl = tk.Label(self.root, text=f"Input {i+1}", width=12, bg="gray")
            lbl.grid(row=row+1, column=i, padx=5, pady=5)
            inputs.append(lbl)

        # Heartbeat indicator
        indicator = tk.Label(self.root, text="●", width=2, bg="red")
        indicator.grid(row=row, column=4, padx=5)

        self.gui_nodes[module_id] = {"buttons": buttons, "inputs": inputs, "indicator": indicator}

    # ---------------- Handle relay button toggle ----------------
    def on_toggle(self, module_id, idx):
        state = self.registry.get_or_create(module_id)
        new_state = not state.relays[idx]
        #state.relays[idx] = new_state

        # Send command
        cmd = f"{module_id};{idx+1};{'ON' if new_state else 'OFF'}"
        self.pub.publish(String(data=cmd))
        self.get_logger().info(f"CMD → {cmd}")

    # ---------------- Update GUI ----------------
    def gui_update(self):
        now = time.time()
        for module_id, gui in self.gui_nodes.items():
            state = self.registry.get_or_create(module_id)

            # Update relay buttons
            for i in range(4):
                gui["buttons"][i].config(
                    text="ON" if state.relays[i] else "OFF",
                    bg="green" if state.relays[i] else "red"
                )

            # Update input indicators
            for i in range(4):
                gui["inputs"][i].config(
                    bg="green" if state.inputs[i] else "gray"
                )

            # Update heartbeat indicator
            gui["indicator"].config(
                bg="green" if now - state.last_seen < 2.0 else "red"
            )

        self.root.after(100, self.gui_update)

# ---------------- Simulation for testing ----------------
def simulate_modules(gui_node, module_ids, idx=0):
    """Simulate incoming messages cycling through module_ids"""
    module_id = module_ids[idx % len(module_ids)]
    inputs = ",".join(str(random.randint(0,1)) for _ in range(4))
    relays = ",".join(str(random.randint(0,1)) for _ in range(4))
    msg = String(data=f"{module_id};R:{relays};I:{inputs}")
    gui_node.status_cb(msg)
    gui_node.root.after(1000, lambda: simulate_modules(gui_node, module_ids, idx+1))

# ---------------- Main ----------------
def main():
    rclpy.init()
    root = tk.Tk()
    root.title("Module Panel")

    # Ordered list of module IDs (arbitrary, non-consecutive)
    module_ids = ["1","2"]

    # Registry tracks module states
    registry = ModuleRegistry()
    for mid in module_ids:
        registry.get_or_create(mid)

    # Create GUI + ROS node
    gui_node = ModuleGUI(root, registry, module_ids)

    # Start simulation for testing
    #simulate_modules(gui_node, module_ids)

    try:
        root.mainloop()
    finally:
        gui_node.destroy_node()
        rclpy.shutdown()

if __name__ == "__main__":
    main()
