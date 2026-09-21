import queue
import threading
import time
from datetime import datetime
import tkinter as tk
from tkinter import ttk, messagebox

import serial
from serial.tools import list_ports


class SerialCommissioningManager:
    def __init__(self, log_callback=None, status_callback=None):
        self.log_callback = log_callback
        self.status_callback = status_callback

        self.ser = None
        self.reader_thread = None
        self.reader_running = False

        self.config_mode_detected = False
        self.arm_config_trigger = False
        self.arm_timeout_deadline = 0.0
        self.config_trigger_sent = False

    def log(self, text: str) -> None:
        if self.log_callback:
            self.log_callback(text)

    def set_status(self, text: str) -> None:
        if self.status_callback:
            self.status_callback(text)

    def list_serial_ports(self):
        ports = []
        for port in list_ports.comports():
            ports.append({
                "device": port.device,
                "description": port.description or "",
                "hwid": port.hwid or "",
            })
        return ports

    def connect(self, port: str, baudrate: int = 115200, timeout: float = 0.25) -> bool:
        self.disconnect()

        try:
            self.ser = serial.Serial(port=port, baudrate=baudrate, timeout=timeout)

            self.config_mode_detected = False
            self.arm_config_trigger = False
            self.arm_timeout_deadline = 0.0
            self.config_trigger_sent = False

            self.reader_running = True
            self.reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
            self.reader_thread.start()

            self.set_status(f"Connected: {port} @ {baudrate}")
            self.log(f"[INFO] Connected to {port} @ {baudrate}")
            return True
        except Exception as e:
            self.ser = None
            self.set_status("Connect failed")
            self.log(f"[ERROR] Failed to connect: {e}")
            return False

    def disconnect(self) -> None:
        self.reader_running = False
        self.arm_config_trigger = False

        if self.ser:
            try:
                port_name = self.ser.port
                self.ser.close()
                self.log(f"[INFO] Disconnected from {port_name}")
            except Exception as e:
                self.log(f"[WARN] Error while disconnecting: {e}")

        self.ser = None
        self.set_status("Disconnected")

    def is_connected(self) -> bool:
        return self.ser is not None and self.ser.is_open

    def arm_for_config_prompt(self, timeout_seconds: float = 15.0):
        if not self.is_connected():
            self.log("[ERROR] Connect to a serial port first")
            return False

        self.config_mode_detected = False
        self.config_trigger_sent = False
        self.arm_config_trigger = True
        self.arm_timeout_deadline = time.time() + timeout_seconds

        self.log("[INFO] Config trigger armed; waiting for boot prompt")
        self.set_status("Waiting for boot prompt...")
        return True

    def disarm_config_prompt(self):
        self.arm_config_trigger = False
        self.arm_timeout_deadline = 0.0
        self.log("[INFO] Config trigger disarmed")

    def send_config_trigger_once(self):
        if not self.is_connected():
            self.log("[ERROR] Cannot send config trigger; serial not connected")
            return False

        try:
            self.ser.write(b"c")
            self.ser.flush()
            self.config_trigger_sent = True
            self.log("[TX] c")
            self.set_status("Config trigger sent")
            return True
        except Exception as e:
            self.log(f"[ERROR] Failed to send config trigger: {e}")
            self.set_status("Config trigger failed")
            return False

    def write_line(self, line: str) -> bool:
        if not self.is_connected():
            self.log("[ERROR] Serial port is not connected")
            return False

        text = line.rstrip("\r\n")

        try:
            self.ser.write((text + "\n").encode("utf-8"))
            self.ser.flush()
            self.log(f"[TX] {text}")
            return True
        except Exception as e:
            self.log(f"[ERROR] Serial write failed: {e}")
            self.set_status("Write failed")
            return False

    def _reader_loop(self) -> None:
        while self.reader_running and self.ser and self.ser.is_open:
            try:
                waiting = self.ser.in_waiting
                if waiting:
                    data = self.ser.read(waiting)
                    if data:
                        text = data.decode("utf-8", errors="replace")

                        for line in text.splitlines():
                            clean = line.strip()
                            if not clean:
                                continue

                            self.log(f"[RX] {clean}")

                            if "=== Configuration Mode ===" in clean or clean.startswith("Wi-Fi SSID:"):
                                self.config_mode_detected = True
                                self.arm_config_trigger = False
                                self.set_status("Config mode detected")

                            if self.arm_config_trigger:
                                if time.time() > self.arm_timeout_deadline:
                                    self.arm_config_trigger = False
                                    self.log("[WARN] Config trigger arm timed out")
                                    self.set_status("Config arm timed out")

                                elif (
                                    "Press 'c' in 5 seconds to enter config mode" in clean
                                    and not self.config_trigger_sent
                                ):
                                    self.log("[INFO] Boot prompt detected; sending config trigger")
                                    self.send_config_trigger_once()
                                    self.arm_config_trigger = False
                else:
                    time.sleep(0.05)

            except Exception as e:
                self.log(f"[ERROR] Serial read failed: {e}")
                self.set_status("Read failed")
                break

        self.reader_running = False


class CommissioningFrame(tk.Frame):
    DEFAULT_BAUD = "115200"

    def __init__(self, parent):
        super().__init__(parent)

        self.log_queue = queue.Queue()
        self.manager = SerialCommissioningManager(
            log_callback=self._queue_log,
            status_callback=self._queue_status,
        )

        self.port_records = []

        self._build_ui()
        self.refresh_ports()
        self.after(100, self._drain_gui_queue)

    def _build_ui(self):
        self.columnconfigure(0, weight=1)
        self.rowconfigure(2, weight=1)

        title = tk.Label(self, text="Module Commissioning Tool", font=("Arial", 18, "bold"))
        title.grid(row=0, column=0, sticky="ew", padx=10, pady=(10, 6))

        top = tk.Frame(self)
        top.grid(row=1, column=0, sticky="nsew", padx=10, pady=(0, 8))
        top.columnconfigure(0, weight=1)
        top.columnconfigure(1, weight=1)

        serial_frame = tk.LabelFrame(top, text="Serial Connection", padx=10, pady=10)
        serial_frame.grid(row=0, column=0, sticky="nsew", padx=(0, 5))
        serial_frame.columnconfigure(1, weight=1)

        form_frame = tk.LabelFrame(top, text="Module Setup", padx=10, pady=10)
        form_frame.grid(row=0, column=1, sticky="nsew", padx=(5, 0))
        form_frame.columnconfigure(1, weight=1)

        self._build_serial_panel(serial_frame)
        self._build_form_panel(form_frame)

        log_frame = tk.LabelFrame(self, text="Serial Output", padx=8, pady=8)
        log_frame.grid(row=2, column=0, sticky="nsew", padx=10, pady=(0, 10))
        log_frame.rowconfigure(0, weight=1)
        log_frame.columnconfigure(0, weight=1)

        self.serial_output = tk.Text(
            log_frame,
            height=18,
            wrap="word",
            state="disabled",
            font=("Courier", 10),
        )
        self.serial_output.grid(row=0, column=0, sticky="nsew")

        scroll = tk.Scrollbar(log_frame, command=self.serial_output.yview)
        scroll.grid(row=0, column=1, sticky="ns")
        self.serial_output.configure(yscrollcommand=scroll.set)

        bottom = tk.Frame(self)
        bottom.grid(row=3, column=0, sticky="ew", padx=10, pady=(0, 10))
        bottom.columnconfigure(0, weight=1)

        self.status_label = tk.Label(
            bottom,
            text="Disconnected",
            anchor="w",
            font=("Arial", 11, "bold"),
        )
        self.status_label.grid(row=0, column=0, sticky="ew")

        clear_btn = tk.Button(bottom, text="Clear Log", command=self.clear_log, width=12)
        clear_btn.grid(row=0, column=1, padx=(8, 0))

    def _build_serial_panel(self, parent):
        tk.Label(parent, text="Serial Port").grid(row=0, column=0, sticky="w", pady=4)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(parent, textvariable=self.port_var, state="readonly")
        self.port_combo.grid(row=0, column=1, sticky="ew", pady=4)

        refresh_btn = tk.Button(parent, text="Refresh Ports", command=self.refresh_ports, width=14)
        refresh_btn.grid(row=0, column=2, padx=(8, 0), pady=4)

        tk.Label(parent, text="Baud Rate").grid(row=1, column=0, sticky="w", pady=4)
        self.baud_var = tk.StringVar(value=self.DEFAULT_BAUD)
        self.baud_entry = tk.Entry(parent, textvariable=self.baud_var)
        self.baud_entry.grid(row=1, column=1, sticky="ew", pady=4)

        connect_btn = tk.Button(parent, text="Connect", command=self.connect_serial, width=14)
        connect_btn.grid(row=2, column=0, pady=(8, 4), sticky="w")

        disconnect_btn = tk.Button(parent, text="Disconnect", command=self.disconnect_serial, width=14)
        disconnect_btn.grid(row=2, column=1, pady=(8, 4), sticky="w")

        trigger_btn = tk.Button(parent, text="Arm Config Trigger", command=self.start_config_trigger, width=18)
        trigger_btn.grid(row=3, column=0, pady=4, sticky="w")

        send_btn = tk.Button(parent, text="Send Config", command=self.send_config, width=14)
        send_btn.grid(row=3, column=1, pady=4, sticky="w")

        help_text = (
            "Suggested flow:\n"
            "1. Select port and connect\n"
            "2. Click Arm Config Trigger\n"
            "3. Power-cycle the module\n"
            "4. Tool sends 'c' when boot prompt appears\n"
            "5. Wait for config prompts\n"
            "6. Click Send Config"
        )
        tk.Label(parent, text=help_text, justify="left", anchor="w").grid(
            row=4, column=0, columnspan=3, sticky="ew", pady=(12, 0)
        )

    def _build_form_panel(self, parent):
        self.form_vars = {
            "wifi_ssid": tk.StringVar(),
            "wifi_password": tk.StringVar(),
            "agent_ip": tk.StringVar(),
            "agent_port": tk.StringVar(value="8888"),
            "module_id": tk.StringVar(),
        }

        row = 0
        self._add_form_row(parent, row, "Wi-Fi SSID", self.form_vars["wifi_ssid"])
        row += 1

        tk.Label(parent, text="Wi-Fi Password").grid(row=row, column=0, sticky="w", pady=4)
        self.wifi_password_entry = tk.Entry(parent, textvariable=self.form_vars["wifi_password"], show="*")
        self.wifi_password_entry.grid(row=row, column=1, sticky="ew", pady=4)
        row += 1

        self._add_form_row(parent, row, "Agent IP", self.form_vars["agent_ip"])
        row += 1

        self._add_form_row(parent, row, "Agent Port", self.form_vars["agent_port"])
        row += 1

        self._add_form_row(parent, row, "Module ID", self.form_vars["module_id"])

    def _add_form_row(self, parent, row, label_text, var):
        tk.Label(parent, text=label_text).grid(row=row, column=0, sticky="w", pady=4)
        entry = tk.Entry(parent, textvariable=var)
        entry.grid(row=row, column=1, sticky="ew", pady=4)

    def _queue_log(self, text: str):
        self.log_queue.put(("log", text))

    def _queue_status(self, text: str):
        self.log_queue.put(("status", text))

    def _drain_gui_queue(self):
        try:
            while True:
                item_type, value = self.log_queue.get_nowait()
                if item_type == "log":
                    self._append_log_direct(value)
                elif item_type == "status":
                    self.status_label.config(text=value)
        except queue.Empty:
            pass

        self.after(100, self._drain_gui_queue)

    def _append_log_direct(self, text: str):
        timestamp = datetime.now().strftime("%H:%M:%S")
        self.serial_output.config(state="normal")
        self.serial_output.insert("end", f"[{timestamp}] {text}\n")
        self.serial_output.see("end")
        self.serial_output.config(state="disabled")

    def clear_log(self):
        self.serial_output.config(state="normal")
        self.serial_output.delete("1.0", "end")
        self.serial_output.config(state="disabled")

    def refresh_ports(self):
        self.port_records = self.manager.list_serial_ports()

        display_values = []
        for rec in self.port_records:
            display_values.append(f"{rec['device']} - {rec['description']}")

        self.port_combo["values"] = display_values

        if display_values:
            if self.port_var.get() not in display_values:
                self.port_var.set(display_values[0])
            self._queue_log(f"[INFO] Found {len(display_values)} serial port(s)")
        else:
            self.port_var.set("")
            self._queue_log("[WARN] No serial ports found")

    def _selected_port_device(self):
        selection = self.port_var.get().strip()
        if not selection:
            return None
        return selection.split(" - ", 1)[0]

    def connect_serial(self):
        port = self._selected_port_device()
        if not port:
            messagebox.showerror("No Port Selected", "Select a serial port first.")
            return

        try:
            baud = int(self.baud_var.get().strip())
        except ValueError:
            messagebox.showerror("Invalid Baud Rate", "Baud rate must be a whole number.")
            return

        self.manager.connect(port, baudrate=baud)

    def disconnect_serial(self):
        self.manager.disconnect()

    def start_config_trigger(self):
        if not self.manager.is_connected():
            messagebox.showerror("Not Connected", "Connect to a serial port first.")
            return

        ok = self.manager.arm_for_config_prompt(timeout_seconds=15.0)
        if ok:
            self._queue_log("[INFO] Trap armed. Power-cycle the module now.")

    def validate_form(self):
        wifi_ssid = self.form_vars["wifi_ssid"].get().strip()
        agent_ip = self.form_vars["agent_ip"].get().strip()
        agent_port = self.form_vars["agent_port"].get().strip()
        module_id = self.form_vars["module_id"].get().strip()

        if not wifi_ssid:
            raise ValueError("Wi-Fi SSID is required.")
        if not agent_ip:
            raise ValueError("Agent IP is required.")
        if not agent_port:
            raise ValueError("Agent Port is required.")
        if not module_id:
            raise ValueError("Module ID is required.")

        try:
            port_num = int(agent_port)
        except ValueError:
            raise ValueError("Agent Port must be a whole number.")

        if not (1 <= port_num <= 65535):
            raise ValueError("Agent Port must be between 1 and 65535.")

        try:
            module_num = int(module_id)
        except ValueError:
            raise ValueError("Module ID must be a whole number.")

        if not (0 <= module_num <= 255):
            raise ValueError("Module ID must be between 0 and 255.")

    def build_config_lines(self):
        return [
            self.form_vars["wifi_ssid"].get().strip(),
            self.form_vars["wifi_password"].get().strip(),
            self.form_vars["agent_ip"].get().strip(),
            self.form_vars["agent_port"].get().strip(),
            self.form_vars["module_id"].get().strip(),
        ]

    def send_config(self):
        if not self.manager.is_connected():
            messagebox.showerror("Not Connected", "Connect to a serial port first.")
            return

        try:
            self.validate_form()
        except ValueError as e:
            messagebox.showerror("Invalid Form", str(e))
            return

        confirm = messagebox.askyesno(
            "Write Configuration",
            "This will send the form values to the connected module.\n\nContinue?"
        )
        if not confirm:
            return

        lines = self.build_config_lines()
        worker = threading.Thread(target=self._send_config_worker, args=(lines,), daemon=True)
        worker.start()

    def _send_config_worker(self, lines):
        self.manager.set_status("Sending config...")
        self.manager.log("[INFO] Sending configuration values")

        for line in lines:
            if not self.manager.is_connected():
                self.manager.log("[ERROR] Serial connection lost")
                self.manager.set_status("Connection lost")
                return

            ok = self.manager.write_line(line)
            if not ok:
                return

            time.sleep(0.25)

        self.manager.log("[INFO] Config send complete")
        self.manager.set_status("Config sent")

    def shutdown(self):
        self.manager.disconnect()


class CommissioningApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Module Commissioning Tool")
        self.geometry("980x720")

        self.frame = CommissioningFrame(self)
        self.frame.pack(fill="both", expand=True)

        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def on_close(self):
        self.frame.shutdown()
        self.destroy()


if __name__ == "__main__":
    app = CommissioningApp()
    app.mainloop()