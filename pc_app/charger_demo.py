"""
Maxwell Charger Demo - PC Control Application
Protocol: Binary frame over USB CDC (Virtual COM Port)
Compatible with STM32F407 firmware demo

Requires: pip install pyserial
"""

import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import struct
import threading
import time

# ============== Protocol Constants ==============
SOF1 = 0xAA
SOF2 = 0x55
CRC8_POLY = 0x07

# Commands (PC -> STM32)
CMD_SET_VOLTAGE = 0x01
CMD_SET_CURRENT = 0x02
CMD_START = 0x03
CMD_STOP = 0x04
CMD_SET_MODULE_ADDR = 0x05
CMD_PING = 0x06

# Responses (STM32 -> PC)
RSP_STATUS = 0x81
RSP_ACK = 0x82
RSP_NACK = 0x83
RSP_PONG = 0x84

# Alarm bit definitions
ALARM_BITS = {
    0: "Module Fault",
    1: "Module Protect",
    4: "Input Error",
    6: "SCI Failure",
    8: "DCDC Overvoltage",
    9: "PFC Abnormal",
    14: "AC Undervoltage",
    16: "CAN Failure",
    17: "Current Imbalance",
    22: "DCDC Off",
    23: "Power Limit",
    24: "Temp Derating",
    25: "AC Power Limit",
    27: "Fan Failure",
    28: "Short Circuit",
    30: "DCDC Overtemp",
    31: "Output Overvoltage",
}


# ============== Protocol Layer ==============

def crc8(data: bytes) -> int:
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ CRC8_POLY) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def build_frame(cmd: int, payload: bytes = b"") -> bytes:
    header = bytes([SOF1, SOF2, cmd, len(payload)])
    crc_data = bytes([cmd, len(payload)]) + payload
    return header + payload + bytes([crc8(crc_data)])


def parse_status(payload: bytes) -> dict:
    """Parse PC_StatusReport_t (29 bytes, packed)"""
    if len(payload) < 29:
        return None
    voltage, current, temp_dcdc, temp_ambient, alarm, input_power, \
    modules_online, modules_fault, charging, btn_start, btn_stop = \
        struct.unpack("<ffffIIBBBBB", payload[:29])
    
    return {
        "voltage": voltage,
        "current": current,
        "temp_dcdc": temp_dcdc,
        "temp_ambient": temp_ambient,
        "alarm": alarm,
        "input_power": input_power,
        "charging": bool(charging),
        "online": modules_online > 0,
        "fault": modules_fault > 0,
    }


# ============== GUI Application ==============

class ChargerDemoApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Maxwell Charger Demo v1.0")
        self.root.resizable(False, False)

        self.serial_port = None
        self.running = False
        self.rx_thread = None

        self._build_ui()
        self._refresh_ports()

    def _build_ui(self):
        # --- Connection frame ---
        frm_conn = ttk.LabelFrame(self.root, text="Connection", padding=10)
        frm_conn.grid(row=0, column=0, columnspan=2, padx=10, pady=5, sticky="ew")

        ttk.Label(frm_conn, text="Port:").grid(row=0, column=0)
        self.cmb_port = ttk.Combobox(frm_conn, width=15, state="readonly")
        self.cmb_port.grid(row=0, column=1, padx=5)

        ttk.Button(frm_conn, text="Refresh", command=self._refresh_ports).grid(row=0, column=2, padx=5)
        self.btn_connect = ttk.Button(frm_conn, text="Connect", command=self._toggle_connect)
        self.btn_connect.grid(row=0, column=3, padx=5)

        self.lbl_status = ttk.Label(frm_conn, text="Disconnected", foreground="red")
        self.lbl_status.grid(row=0, column=4, padx=10)

        # --- Control frame ---
        frm_ctrl = ttk.LabelFrame(self.root, text="Control", padding=10)
        frm_ctrl.grid(row=1, column=0, padx=10, pady=5, sticky="nsew")

        ttk.Label(frm_ctrl, text="Voltage (V):").grid(row=0, column=0, sticky="w")
        self.ent_voltage = ttk.Entry(frm_ctrl, width=10)
        self.ent_voltage.grid(row=0, column=1, padx=5, pady=3)
        self.ent_voltage.insert(0, "54.6")

        ttk.Label(frm_ctrl, text="Current Limit (%):").grid(row=1, column=0, sticky="w")
        self.ent_current = ttk.Entry(frm_ctrl, width=10)
        self.ent_current.grid(row=1, column=1, padx=5, pady=3)
        self.ent_current.insert(0, "100")

        ttk.Label(frm_ctrl, text="Module Addr:").grid(row=2, column=0, sticky="w")
        self.ent_addr = ttk.Entry(frm_ctrl, width=10)
        self.ent_addr.grid(row=2, column=1, padx=5, pady=3)
        self.ent_addr.insert(0, "0")

        ttk.Button(frm_ctrl, text="Set V", command=self._cmd_set_voltage).grid(row=0, column=2, padx=5)
        ttk.Button(frm_ctrl, text="Set I", command=self._cmd_set_current).grid(row=1, column=2, padx=5)
        ttk.Button(frm_ctrl, text="Set Addr", command=self._cmd_set_module_addr).grid(row=2, column=2, padx=5)

        ttk.Separator(frm_ctrl, orient="horizontal").grid(row=3, column=0, columnspan=3, sticky="ew", pady=8)

        self.btn_start = ttk.Button(frm_ctrl, text="START", command=self._cmd_start)
        self.btn_start.grid(row=4, column=0, columnspan=2, sticky="ew", pady=3)

        self.btn_stop = ttk.Button(frm_ctrl, text="STOP", command=self._cmd_stop)
        self.btn_stop.grid(row=5, column=0, columnspan=2, sticky="ew", pady=3)

        # --- Monitor frame ---
        frm_mon = ttk.LabelFrame(self.root, text="Module Status", padding=10)
        frm_mon.grid(row=1, column=1, padx=10, pady=5, sticky="nsew")

        labels = ["Voltage:", "Current:", "Temp DCDC:", "Temp Ambient:", "Input Power:", "State:", "Module:"]
        self.mon_vars = {}
        for i, lbl in enumerate(labels):
            ttk.Label(frm_mon, text=lbl).grid(row=i, column=0, sticky="w", pady=2)
            var = tk.StringVar(value="---")
            ttk.Label(frm_mon, textvariable=var, width=15, anchor="e").grid(row=i, column=1, padx=10, pady=2)
            self.mon_vars[lbl] = var

        # --- Alarm frame ---
        frm_alarm = ttk.LabelFrame(self.root, text="Alarms", padding=10)
        frm_alarm.grid(row=2, column=0, columnspan=2, padx=10, pady=5, sticky="ew")

        self.lbl_alarm = ttk.Label(frm_alarm, text="No alarms", foreground="green")
        self.lbl_alarm.grid(row=0, column=0, sticky="w")

        # --- Log frame ---
        frm_log = ttk.LabelFrame(self.root, text="Log", padding=5)
        frm_log.grid(row=3, column=0, columnspan=2, padx=10, pady=5, sticky="ew")

        self.txt_log = tk.Text(frm_log, height=5, width=60, state="disabled")
        self.txt_log.grid(row=0, column=0, sticky="ew")
        scrollbar = ttk.Scrollbar(frm_log, orient="vertical", command=self.txt_log.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.txt_log.configure(yscrollcommand=scrollbar.set)

    def _log(self, msg):
        self.txt_log.configure(state="normal")
        self.txt_log.insert("end", f"[{time.strftime('%H:%M:%S')}] {msg}\n")
        self.txt_log.see("end")
        self.txt_log.configure(state="disabled")

    def _refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.cmb_port["values"] = ports
        if ports:
            self.cmb_port.current(0)

    def _toggle_connect(self):
        if self.serial_port and self.serial_port.is_open:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        port = self.cmb_port.get()
        if not port:
            messagebox.showwarning("Warning", "Please select a COM port")
            return
        try:
            self.serial_port = serial.Serial(port, 115200, timeout=0.1)
            self.running = True
            self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
            self.rx_thread.start()
            self.btn_connect.configure(text="Disconnect")
            self.lbl_status.configure(text=f"Connected ({port})", foreground="green")
            self._log(f"Connected to {port}")
            # Send ping
            self._send_frame(CMD_PING)
        except Exception as e:
            messagebox.showerror("Error", f"Cannot open {port}: {e}")

    def _disconnect(self):
        self.running = False
        if self.rx_thread:
            self.rx_thread.join(timeout=1)
        if self.serial_port:
            self.serial_port.close()
            self.serial_port = None
        self.btn_connect.configure(text="Connect")
        self.lbl_status.configure(text="Disconnected", foreground="red")
        self._log("Disconnected")

    def _send_frame(self, cmd, payload=b""):
        if not self.serial_port or not self.serial_port.is_open:
            self._log("Not connected!")
            return
        frame = build_frame(cmd, payload)
        self.serial_port.write(frame)

    def _cmd_set_voltage(self):
        try:
            v = float(self.ent_voltage.get())
            payload = struct.pack("<f", v)
            self._send_frame(CMD_SET_VOLTAGE, payload)
            self._log(f"Set Voltage = {v:.1f} V")
        except ValueError:
            messagebox.showwarning("Warning", "Invalid voltage value")

    def _cmd_set_current(self):
        try:
            pct = float(self.ent_current.get())
            ratio = pct / 100.0
            payload = struct.pack("<f", ratio)
            self._send_frame(CMD_SET_CURRENT, payload)
            self._log(f"Set Current Limit = {pct:.0f}% (ratio={ratio:.2f})")
        except ValueError:
            messagebox.showwarning("Warning", "Invalid current value")

    def _cmd_set_module_addr(self):
        try:
            addr = int(self.ent_addr.get(), 0)
            if not (0 <= addr <= 63):
                raise ValueError()
            payload = bytes([addr, 0]) # addr, group=0
            self._send_frame(CMD_SET_MODULE_ADDR, payload)
            self._log(f"Set Module Address = 0x{addr:02X}")
        except ValueError:
            messagebox.showwarning("Warning", "Invalid address (must be 0-63)")

    def _cmd_start(self):
        self._send_frame(CMD_START)
        self._log("START command sent")

    def _cmd_stop(self):
        self._send_frame(CMD_STOP)
        self._log("STOP command sent")

    # ============== RX Thread ==============

    def _rx_loop(self):
        """Background thread to receive and parse frames from STM32"""
        buf = bytearray()
        while self.running:
            try:
                data = self.serial_port.read(64)
                if data:
                    buf.extend(data)
                    self._process_rx_buffer(buf)
            except Exception:
                break

    def _process_rx_buffer(self, buf):
        """Parse frames from buffer, remove consumed bytes"""
        while len(buf) >= 5:  # Minimum frame: SOF1 + SOF2 + CMD + LEN(0) + CRC
            # Find SOF
            idx = -1
            for i in range(len(buf) - 1):
                if buf[i] == SOF1 and buf[i + 1] == SOF2:
                    idx = i
                    break
            if idx < 0:
                buf.clear()
                return
            if idx > 0:
                del buf[:idx]  # Discard bytes before SOF

            if len(buf) < 4:
                return  # Need more bytes for header

            cmd = buf[2]
            plen = buf[3]
            total = 4 + plen + 1  # header(4) + payload + crc(1)

            if len(buf) < total:
                return  # Need more bytes

            # Extract frame
            frame = bytes(buf[:total])
            del buf[:total]

            # Verify CRC
            crc_data = frame[2:4 + plen]  # cmd + len + payload
            if crc8(crc_data) != frame[-1]:
                continue  # Bad CRC, skip

            payload = frame[4:4 + plen]
            self._handle_response(cmd, payload)

    def _handle_response(self, cmd, payload):
        """Handle parsed response in main thread"""
        if cmd == RSP_STATUS:
            status = parse_status(payload)
            if status:
                self.root.after(0, self._update_monitor, status)
        elif cmd == RSP_ACK:
            pass  # Silent ACK
        elif cmd == RSP_NACK:
            err_cmd = payload[0] if len(payload) > 0 else 0
            err_code = payload[1] if len(payload) > 1 else 0
            self.root.after(0, self._log, f"NACK: cmd=0x{err_cmd:02X} err=0x{err_code:02X}")
        elif cmd == RSP_PONG:
            if len(payload) >= 4:
                ver = struct.unpack("<I", payload[:4])[0]
                major = (ver >> 16) & 0xFF
                minor = (ver >> 8) & 0xFF
                patch = ver & 0xFF
                self.root.after(0, self._log, f"PONG: FW v{major}.{minor}.{patch}")

    def _update_monitor(self, status):
        self.mon_vars["Voltage:"].set(f"{status['voltage']:.1f} V")
        self.mon_vars["Current:"].set(f"{status['current']:.1f} A")
        self.mon_vars["Temp DCDC:"].set(f"{status['temp_dcdc']:.1f} C")
        self.mon_vars["Temp Ambient:"].set(f"{status['temp_ambient']:.1f} C")
        self.mon_vars["Input Power:"].set(f"{status['input_power']} W")
        self.mon_vars["State:"].set("CHARGING" if status["charging"] else "IDLE")
        self.mon_vars["Module:"].set("ONLINE" if status["online"] else "OFFLINE")

        # Alarms
        alarm = status["alarm"]
        if alarm == 0:
            self.lbl_alarm.configure(text="No alarms", foreground="green")
        else:
            active = [name for bit, name in ALARM_BITS.items() if alarm & (1 << bit)]
            self.lbl_alarm.configure(text=" | ".join(active), foreground="red")


# ============== Entry Point ==============

if __name__ == "__main__":
    root = tk.Tk()
    app = ChargerDemoApp(root)
    root.mainloop()
