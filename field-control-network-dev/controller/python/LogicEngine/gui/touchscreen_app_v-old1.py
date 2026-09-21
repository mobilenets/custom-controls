#!/usr/bin/env python3
import json
from pathlib import Path
import tkinter as tk
from tkinter import ttk
from datetime import datetime

STATUS_PATH = Path("/home/dev/Projects/BasicLogicEngine/LogicEngine/runtime_status.json")
REFRESH_MS = 1000


class TouchscreenApp:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("Logic Controller")
        self.root.attributes("-fullscreen", True)
        self.root.configure(bg="#202020")

        # Escape fullscreen with Esc for debugging
        self.root.bind("<Escape>", lambda e: self.root.attributes("-fullscreen", False))

        self.status_data = {}

        self._build_styles()
        self._build_layout()
        self.refresh()

    def _build_styles(self):
        self.style = ttk.Style()
        self.style.theme_use("clam")

        self.style.configure("Header.TLabel", font=("Arial", 22, "bold"), foreground="white", background="#202020")
        self.style.configure("Section.TLabel", font=("Arial", 18, "bold"), foreground="white", background="#303030")
        self.style.configure("Value.TLabel", font=("Arial", 14), foreground="white", background="#404040")
        self.style.configure("TileOn.TLabel", font=("Arial", 14, "bold"), foreground="black", background="#66cc66")
        self.style.configure("TileOff.TLabel", font=("Arial", 14, "bold"), foreground="white", background="#666666")
        self.style.configure("TileUnknown.TLabel", font=("Arial", 14, "bold"), foreground="white", background="#aa8844")
        self.style.configure("Msg.TLabel", font=("Arial", 12), foreground="white", background="#404040", wraplength=500)

    def _build_layout(self):
        self.root.grid_rowconfigure(1, weight=1)
        self.root.grid_columnconfigure(0, weight=1)
        self.root.grid_columnconfigure(1, weight=1)
        self.root.grid_columnconfigure(2, weight=1)

        # Top bar
        top = tk.Frame(self.root, bg="#202020")
        top.grid(row=0, column=0, columnspan=3, sticky="nsew", padx=10, pady=10)

        self.title_label = ttk.Label(top, text="Logic Controller Dashboard", style="Header.TLabel")
        self.title_label.pack(side="left", padx=10)

        self.clock_label = ttk.Label(top, text="", style="Header.TLabel")
        self.clock_label.pack(side="right", padx=10)

        # Left column: groups
        self.groups_frame = self._make_section(self.root, "Groups")
        self.groups_frame.grid(row=1, column=0, sticky="nsew", padx=10, pady=10)

        # Center column: flags + nodes
        center = tk.Frame(self.root, bg="#202020")
        center.grid(row=1, column=1, sticky="nsew", padx=10, pady=10)
        center.grid_rowconfigure(0, weight=1)
        center.grid_rowconfigure(1, weight=2)
        center.grid_columnconfigure(0, weight=1)

        self.flags_frame = self._make_section(center, "Flags")
        self.flags_frame.grid(row=0, column=0, sticky="nsew", pady=(0, 10))

        self.nodes_frame = self._make_section(center, "Nodes")
        self.nodes_frame.grid(row=1, column=0, sticky="nsew")

        # Right column: messages
        right = tk.Frame(self.root, bg="#202020")
        right.grid(row=1, column=2, sticky="nsew", padx=10, pady=10)
        right.grid_rowconfigure(0, weight=1)
        right.grid_rowconfigure(1, weight=1)
        right.grid_rowconfigure(2, weight=1)
        right.grid_columnconfigure(0, weight=1)

        self.meta_frame = self._make_section(right, "Controller")
        self.meta_frame.grid(row=0, column=0, sticky="nsew", pady=(0, 10))

        self.in_frame = self._make_section(right, "Last /modulereturn")
        self.in_frame.grid(row=1, column=0, sticky="nsew", pady=(0, 10))

        self.out_frame = self._make_section(right, "Last /actionrequest")
        self.out_frame.grid(row=2, column=0, sticky="nsew")

    def _make_section(self, parent, title: str):
        frame = tk.Frame(parent, bg="#303030", bd=2, relief="ridge")
        header = ttk.Label(frame, text=title, style="Section.TLabel")
        header.pack(anchor="w", padx=10, pady=8)
        body = tk.Frame(frame, bg="#404040")
        body.pack(fill="both", expand=True, padx=8, pady=(0, 8))
        frame.body = body
        return frame

    def _clear_body(self, frame):
        for child in frame.body.winfo_children():
            child.destroy()

    def _tile_style_for_value(self, value):
        text = str(value).lower()
        if text in ("true", "tri.true", "on", "1"):
            return "TileOn.TLabel"
        if text in ("false", "tri.false", "off", "0"):
            return "TileOff.TLabel"
        return "TileUnknown.TLabel"

    def _populate_dict_tiles(self, frame, data: dict):
        self._clear_body(frame)
        row = 0
        for key, value in sorted(data.items()):
            name = ttk.Label(frame.body, text=key, style="Value.TLabel")
            name.grid(row=row, column=0, sticky="ew", padx=6, pady=4)

            val = ttk.Label(frame.body, text=str(value), style=self._tile_style_for_value(value))
            val.grid(row=row, column=1, sticky="ew", padx=6, pady=4)

            frame.body.grid_columnconfigure(0, weight=1)
            frame.body.grid_columnconfigure(1, weight=1)
            row += 1

    def _populate_meta(self, frame, data: dict):
        self._clear_body(frame)

        items = [
            ("Snapshot Time", data.get("time", "unknown")),
            ("Last Update", data.get("last_update", "unknown")),
        ]

        for row, (k, v) in enumerate(items):
            ttk.Label(frame.body, text=k, style="Value.TLabel").grid(row=row, column=0, sticky="w", padx=6, pady=4)
            ttk.Label(frame.body, text=str(v), style="Value.TLabel").grid(row=row, column=1, sticky="w", padx=6, pady=4)

    def _populate_message(self, frame, message):
        self._clear_body(frame)
        ttk.Label(frame.body, text=str(message) if message is not None else "None", style="Msg.TLabel").pack(
            anchor="w", fill="both", expand=True, padx=6, pady=6
        )

    def load_status(self):
        if not STATUS_PATH.exists():
            return {
                "time": "no status file",
                "last_update": "no status file",
                "groups": {},
                "flags": {},
                "nodes": {},
                "last_modulereturn": None,
                "last_actionrequest": None,
            }

        try:
            with open(STATUS_PATH, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception as e:
            return {
                "time": "read error",
                "last_update": str(e),
                "groups": {},
                "flags": {},
                "nodes": {},
                "last_modulereturn": None,
                "last_actionrequest": None,
            }

    def refresh(self):
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self.clock_label.config(text=now)

        self.status_data = self.load_status()

        self._populate_dict_tiles(self.groups_frame, self.status_data.get("groups", {}))
        self._populate_dict_tiles(self.flags_frame, self.status_data.get("flags", {}))
        self._populate_dict_tiles(self.nodes_frame, self.status_data.get("nodes", {}))
        self._populate_meta(self.meta_frame, self.status_data)
        self._populate_message(self.in_frame, self.status_data.get("last_modulereturn"))
        self._populate_message(self.out_frame, self.status_data.get("last_actionrequest"))

        self.root.after(REFRESH_MS, self.refresh)


def main():
    root = tk.Tk()
    app = TouchscreenApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()