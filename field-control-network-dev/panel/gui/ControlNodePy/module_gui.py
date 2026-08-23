import tkinter as tk

class ModuleGUI:
    def __init__(self, root, state, on_toggle, registry=None, module_id=None):
        self.root = root
        self.state = state
        self.on_toggle = on_toggle
        self.registry = registry
        self.module_id = module_id

        self.buttons = []
        self.inputs = []

        # Relay buttons
        for i in range(4):
            btn = tk.Button(
                root,
                width=8,
                command=lambda i=i: self.on_toggle(i)
            )
            btn.grid(row=0, column=i, padx=5, pady=5)
            self.buttons.append(btn)

            lbl = tk.Label(
                root,
                width=8,
                bg="gray"
            )
            lbl.grid(row=1, column=i, padx=5, pady=5)
            self.inputs.append(lbl)

        # Connection indicator (optional)
        if registry and module_id:
            self.status = tk.Label(root, text="OFFLINE", bg="red", width=10)
            self.status.grid(row=0, column=5, padx=10)

        self.root.after(100, self.update)

    def update(self):
        # Update relay buttons and input indicators
        for i in range(4):
            relay_on = self.state.relays[i]

            self.buttons[i].config(
                text="OFF" if relay_on else "ON",
                bg="red" if relay_on else "green"
            )

            self.inputs[i].config(
                bg="green" if self.state.inputs[i] else "gray"
            )

        # Update connection status
        if self.registry and self.module_id:
            online = self.registry.is_online(self.module_id)
            self.status.config(
                text="ONLINE" if online else "OFFLINE",
                bg="green" if online else "red"
            )

        self.root.after(100, self.update)
