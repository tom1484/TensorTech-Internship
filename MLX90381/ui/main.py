# mlx90381_ui.py
import sys
import re
import time
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

import serial
import serial.tools.list_ports

from PySide6.QtCore import QThread, Signal, Slot, Qt
from PySide6.QtWidgets import (
    QApplication,
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QPushButton,
    QLabel,
    QComboBox,
    QTextEdit,
    QTableWidget,
    QTableWidgetItem,
    QMessageBox,
    QGroupBox,
    QAbstractScrollArea,
    QHeaderView,
    QRadioButton,
    QButtonGroup,
)


# ---------- Field specifications ----------


@dataclass(frozen=True)
class FieldSpec:
    name: str
    reg_addr: Optional[int]  # 0x20..0x2E, or None if MTP-only
    mtp_addr: int  # 0x00..0x1E
    byte_sel: str  # "LSB" or "MSB"
    lsb: int  # bit index within that byte (0..7)
    width: int  # number of bits


FIELD_SPECS = [
    # Fields valid for both REG and MTP (reg_addr = mtp_addr + 0x20)
    FieldSpec("RG_X", 0x20, 0x00, "LSB", 0, 3),
    FieldSpec("FG_X", 0x20, 0x00, "LSB", 3, 5),
    FieldSpec("RG_Y", 0x22, 0x02, "LSB", 0, 3),
    FieldSpec("FG_Y", 0x22, 0x02, "LSB", 3, 5),
    FieldSpec("RG_Z", 0x24, 0x04, "LSB", 0, 3),
    FieldSpec("FG_Z", 0x24, 0x04, "LSB", 3, 5),
    FieldSpec("VOQ_OUT1", 0x20, 0x00, "MSB", 0, 4),
    FieldSpec("VOQ_OUT2", 0x22, 0x02, "MSB", 0, 4),
    FieldSpec("AXIS_CH1", 0x26, 0x06, "LSB", 0, 2),
    FieldSpec("AXIS_CH2", 0x26, 0x06, "LSB", 2, 2),
    FieldSpec("PLATEZ", 0x26, 0x06, "LSB", 4, 2),
    FieldSpec("TC", 0x28, 0x08, "LSB", 0, 5),
    FieldSpec("FILT", 0x2A, 0x0A, "LSB", 0, 5),
    # Fields valid for MTP only (reg_addr = None)
    FieldSpec("DIS_DIAG", None, 0x0C, "LSB", 1, 1),
    FieldSpec("MEMLOCK", None, 0x0C, "LSB", 0, 1),
    FieldSpec("TC350_DATA", None, 0x0E, "LSB", 0, 4),
    FieldSpec("TC2000_DATA", None, 0x14, "MSB", 4, 4),
    FieldSpec("CHIP_ID1", None, 0x1A, "LSB", 0, 8),
    FieldSpec("CHIP_ID2", None, 0x1C, "LSB", 0, 8),
    FieldSpec("CHIP_ID3", None, 0x1E, "LSB", 0, 8),
]


def _get_byte(word: int, byte_sel: str) -> int:
    word &= 0xFFFF
    return (word & 0xFF) if byte_sel == "LSB" else ((word >> 8) & 0xFF)


def _set_byte(word: int, byte_sel: str, byte_val: int) -> int:
    word &= 0xFFFF
    byte_val &= 0xFF
    if byte_sel == "LSB":
        return (word & 0xFF00) | byte_val
    else:
        return (word & 0x00FF) | (byte_val << 8)


def get_field_at(words: dict[int, int], addr: Optional[int], spec: FieldSpec) -> Optional[int]:
    """Get field value from words dict at specified address. Returns None if addr is None."""
    if addr is None:
        return None
    w = words.get(addr, 0) & 0xFFFF
    b = _get_byte(w, spec.byte_sel)
    mask = (1 << spec.width) - 1
    return (b >> spec.lsb) & mask


def get_reg_field(words: dict[int, int], spec: FieldSpec) -> Optional[int]:
    """Get field value from register words dict."""
    return get_field_at(words, spec.reg_addr, spec)


def get_mtp_field(words: dict[int, int], spec: FieldSpec) -> Optional[int]:
    """Get field value from MTP words dict."""
    return get_field_at(words, spec.mtp_addr, spec)


def set_field_at(words: dict[int, int], addr: Optional[int], spec: FieldSpec, value: int) -> bool:
    """Set field value in words dict at specified address. Returns False if addr is None."""
    if addr is None:
        return False
    
    mask = (1 << spec.width) - 1
    v = int(value) & mask

    w = words.get(addr, 0) & 0xFFFF
    b = _get_byte(w, spec.byte_sel)

    b = (b & ~(mask << spec.lsb)) | (v << spec.lsb)
    w2 = _set_byte(w, spec.byte_sel, b)
    words[addr] = w2 & 0xFFFF
    return True


def set_reg_field(words: dict[int, int], spec: FieldSpec, value: int) -> bool:
    """Set field value in register words dict using reg_addr."""
    return set_field_at(words, spec.reg_addr, spec, value)


def set_mtp_field(words: dict[int, int], spec: FieldSpec, value: int) -> bool:
    """Set field value in MTP words dict using mtp_addr."""
    return set_field_at(words, spec.mtp_addr, spec, value)


# ---------- Serial worker ----------


class SerialWorker(QThread):
    line_rx = Signal(bytes, str)  # (raw_bytes, decoded_text)
    conn_state = Signal(bool, str)

    def __init__(self):
        super().__init__()
        self._ser: Optional[serial.Serial] = None
        self._running = False

    def open(self, port: str, baud: int = 115200):
        # Stop any existing thread first
        if self.isRunning():
            self._running = False
            self.wait(1000)  # Wait up to 1 second for thread to finish
        
        # Close any existing serial port
        if self._ser is not None:
            try:
                self._ser.close()
            except Exception:
                pass
            self._ser = None
        
        try:
            self._ser = serial.Serial(port=port, baudrate=baud, timeout=0.1)
            self._running = True
            self.start()  # Always start fresh
            self.conn_state.emit(True, f"Connected to {port} @ {baud}")
        except Exception as e:
            self._ser = None
            self._running = False
            self.conn_state.emit(False, f"Connect failed: {e}")

    def close(self):
        self._running = False
        if self.isRunning():
            self.wait(1000)  # Wait for thread to finish
        if self._ser is not None:
            try:
                self._ser.close()
            except Exception:
                pass
            self._ser = None
        self.conn_state.emit(False, "Disconnected")
    
    def is_connected(self) -> bool:
        return self._ser is not None and self._ser.is_open and self._running

    def send_bytes(self, b: bytes):
        if not self._ser:
            return
        try:
            self._ser.write(b)
        except Exception as e:
            self.conn_state.emit(False, f"Write failed: {e}")

    def send_text(self, s: str):
        self.send_bytes(s.encode("ascii", errors="ignore"))

    def run(self):
        buf = b""
        while self._running:
            if not self._ser:
                time.sleep(0.05)
                continue
            try:
                chunk = self._ser.read(256)
                if not chunk:
                    continue
                buf += chunk
                while b"\n" in buf:
                    line_bytes, buf = buf.split(b"\n", 1)
                    # Strip trailing \r if present
                    if line_bytes.endswith(b"\r"):
                        line_bytes = line_bytes[:-1]
                    try:
                        txt = line_bytes.decode("utf-8", errors="replace")
                    except Exception:
                        txt = repr(line_bytes)
                    self.line_rx.emit(line_bytes, txt)
            except Exception as e:
                self.conn_state.emit(False, f"Serial read failed: {e}")
                self._running = False
                break


# ---------- Parsing helpers (based on firmware prints) ----------

HEX_PAIR_RE = re.compile(r"([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)")


def parse_hex_pairs_line(line: str) -> List[Tuple[int, int]]:
    """
    Parses lines like: "20 1234 22 00FF 24 0 ..."
    Returns list of (addr, word) ints.
    """
    out = []
    for m in HEX_PAIR_RE.finditer(line):
        addr = int(m.group(1), 16)
        word = int(m.group(2), 16)
        out.append((addr, word))
    return out


@dataclass
class Measurement:
    out1: int
    out2: int


MEAS_RE = re.compile(r"OUT1\s+(\d+)\s+OUT2\s+(\d+)", re.IGNORECASE)


def parse_measurement(line: str) -> Optional[Measurement]:
    m = MEAS_RE.search(line)
    if not m:
        return None
    return Measurement(out1=int(m.group(1)), out2=int(m.group(2)))


# ---------- UI ----------


class MainWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("MLX90381 UART UI (MCU bridge)")

        self.worker = SerialWorker()
        self.worker.line_rx.connect(self.on_line_rx, Qt.QueuedConnection)
        self.worker.conn_state.connect(self.on_conn_state, Qt.QueuedConnection)

        # state
        self.reg_words: Dict[int, int] = {}  # addr->word
        self.mtp_words: Dict[int, int] = {}  # addr->word (0x00..0x1E step 2)
        self._connected = False  # True when connected to device
        self._reg_read = False  # True after Read Registers button clicked
        self._mtp_read = False  # True after Read MTP button clicked

        # widgets
        self.port_cb = QComboBox()
        self.refresh_btn = QPushButton("Refresh")
        self.connect_btn = QPushButton("Connect")
        self.ident_btn = QPushButton("Identify (I)")

        self.read_reg_btn = QPushButton("Read Registers (C)")
        self.read_mtp_btn = QPushButton("Read MTP (R)")
        self.measure_btn = QPushButton("Measure (M)")
        self.prog_reg_btn = QPushButton("Program Registers (W+S)")
        self.prog_mtp_btn = QPushButton("Program MTP (E+P)")
        self.lock_mtp_btn = QPushButton("Lock MTP (L)")

        # Group action buttons for easy enable/disable
        self._action_buttons = [
            self.ident_btn,
            self.read_reg_btn,
            self.read_mtp_btn,
            self.measure_btn,
            self.prog_reg_btn,
            self.prog_mtp_btn,
            self.lock_mtp_btn,
        ]
        # Start with action buttons disabled (not connected yet)
        self._set_actions_enabled(False)

        self.log = QTextEdit()
        self.log.setReadOnly(True)

        # Sensing Mode presets (AXIS_CH1, AXIS_CH2, PLATEZ values)
        # Format: {mode_name: (AXIS_CH1, AXIS_CH2, PLATEZ)}
        self._sensing_mode_presets = {
            "X/Y mode": (0, 1, 0),  # OUT1=X, OUT2=Y
            "Y/X mode": (1, 0, 0),  # OUT1=Y, OUT2=X
            "X/Z mode": (0, 2, 2),  # OUT1=X, OUT2=Z
            "Z/X mode": (2, 0, 2),  # OUT1=Z, OUT2=X
            "Y/Z mode": (1, 2, 1),  # OUT1=Y, OUT2=Z
            "Z/Y mode": (2, 1, 1),  # OUT1=Z, OUT2=Y
        }
        self._sensing_mode_group = QButtonGroup()
        self._sensing_mode_radios: Dict[str, QRadioButton] = {}
        for mode_name in self._sensing_mode_presets.keys():
            radio = QRadioButton(mode_name)
            self._sensing_mode_radios[mode_name] = radio
            self._sensing_mode_group.addButton(radio)
        self._sensing_mode_group.buttonClicked.connect(self._on_sensing_mode_selected)

        self.copy_reg_to_mtp_btn = QPushButton("Copy REG → MTP")

        self.reg_table = QTableWidget(8, 3)  # addr, dec, hex
        self.reg_table.setHorizontalHeaderLabels(["ADDR", "DEC", "HEX"])
        self.mtp_table = QTableWidget(16, 3)  # addr, dec, hex
        self.mtp_table.setHorizontalHeaderLabels(["ADDR", "DEC", "HEX"])

        self.dec_table = QTableWidget(len(FIELD_SPECS), 5)
        self.dec_table.setHorizontalHeaderLabels(
            ["Parameter", "REG_DEC", "MTP_DEC", "REG_HEX", "MTP_HEX"]
        )

        self.status_lbl = QLabel("Disconnected")
        self.status_lbl.setTextInteractionFlags(Qt.TextSelectableByMouse)

        self._build_layout()
        self._wire_events()
        self.refresh_ports()
        self._init_tables()
        self._init_decoded_table()

        self.dec_table.cellChanged.connect(self.on_decoded_cell_changed)

        self.reg_table.verticalHeader().setDefaultSectionSize(22)
        self.mtp_table.verticalHeader().setDefaultSectionSize(22)
        self.dec_table.verticalHeader().setDefaultSectionSize(22)

        self._fit_table_height(self.reg_table)
        self._fit_table_height(self.mtp_table)
        # Don't fit dec_table height - it has too many rows, allow scrolling

        self._fit_table_width(self.reg_table)
        self._fit_table_width(self.mtp_table)
        self._fit_table_width(self.dec_table)

    @Slot(int, int)
    def on_decoded_cell_changed(self, row: int, col: int):
        spec = FIELD_SPECS[row]
        
        # Column 1 = REG_DEC, Column 2 = MTP_DEC
        if col == 1:
            # REG_DEC column - only valid if reg_addr is not None
            if spec.reg_addr is None:
                self.dec_table.blockSignals(True)
                self.dec_table.item(row, col).setText("N/A")
                self.dec_table.blockSignals(False)
                return
            
            txt = self.dec_table.item(row, col).text().strip()
            try:
                v = int(txt, 10)
            except ValueError:
                v = 0
            v &= (1 << spec.width) - 1
            
            # Only update reg_words (decoupled from MTP)
            set_reg_field(self.reg_words, spec, v)
            self._refresh_tables_from_state()
            
        elif col == 2:
            # MTP_DEC column - only editable for programmable addresses (0x00-0x0E)
            if spec.mtp_addr > 0x0E:
                self.dec_table.blockSignals(True)
                self.dec_table.item(row, col).setText(str(get_mtp_field(self.mtp_words, spec) or 0))
                self.dec_table.blockSignals(False)
                return
            
            txt = self.dec_table.item(row, col).text().strip()
            try:
                v = int(txt, 10)
            except ValueError:
                v = 0
            v &= (1 << spec.width) - 1
            
            # Only update mtp_words (decoupled from REG)
            set_mtp_field(self.mtp_words, spec, v)
            self._refresh_tables_from_state()
    
    def _fit_table_height(self, table: QTableWidget, extra_px: int = 6):
        """
        Make table tall enough to show all rows without vertical scrolling.
        """
        table.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOff)

        # Use a stable row height
        vh = table.verticalHeader()
        table.verticalHeader().setSectionResizeMode(QHeaderView.Fixed)

        # If you want tighter/looser rows, set this once:
        # table.verticalHeader().setDefaultSectionSize(22)

        header_h = table.horizontalHeader().height()
        rows_h = sum(table.rowHeight(r) for r in range(table.rowCount()))
        frame = table.frameWidth() * 2

        # If horizontal scrollbar can appear, reserve space for it
        hbar_h = table.horizontalScrollBar().sizeHint().height() if table.horizontalScrollBarPolicy() != Qt.ScrollBarAlwaysOff else 0

        table.setMinimumHeight(header_h + rows_h + frame + hbar_h + extra_px)

        # Optional: also shrink the viewport policy so it doesn't try to expand oddly
        table.setSizeAdjustPolicy(QAbstractScrollArea.AdjustToContents)

    def _fit_table_width(self, table: QTableWidget):
        """
        Make table columns stretch to fill available space.
        """
        table.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        
        # Make all columns stretch to fill available width
        table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)

    def _build_layout(self):
        root = QVBoxLayout(self)

        # Connection row
        row = QHBoxLayout()
        row.addWidget(QLabel("COM Port:"))
        row.addWidget(self.port_cb, 2)
        row.addWidget(self.refresh_btn)
        row.addWidget(self.connect_btn)
        row.addWidget(self.ident_btn)
        root.addLayout(row)

        # Action row
        act = QHBoxLayout()
        for b in [
            self.read_reg_btn,
            self.read_mtp_btn,
            self.measure_btn,
            self.prog_reg_btn,
            self.prog_mtp_btn,
            self.lock_mtp_btn,
            self.copy_reg_to_mtp_btn,
        ]:
            act.addWidget(b)
        root.addLayout(act)

        # Tables
        tables = QHBoxLayout()
        gb1 = QGroupBox("Registers (0x20..0x2E)")
        l1 = QVBoxLayout(gb1)
        l1.addWidget(self.reg_table)
        tables.addWidget(gb1, 1)

        gb2 = QGroupBox("MTP (0x00..0x1E)")
        l2 = QVBoxLayout(gb2)
        l2.addWidget(self.mtp_table)
        tables.addWidget(gb2, 1)

        gb3 = QGroupBox("Decoded parameters (customer area)")
        l3 = QVBoxLayout(gb3)
        l3.addWidget(self.dec_table)
        tables.addWidget(gb3, 1)

        root.addLayout(tables, 1)  # Tables get priority space

        # Log + Presets row (smaller height)
        log_presets = QHBoxLayout()
        
        # Log area
        log_box = QVBoxLayout()
        log_box.addWidget(QLabel("Log:"))
        log_box.addWidget(self.log)
        log_presets.addLayout(log_box, 3)  # Log takes more horizontal space
        
        # Presets area (compact)
        presets_gb = QGroupBox("Sensing Mode OUT1/2")
        presets_layout = QVBoxLayout(presets_gb)
        presets_layout.setSpacing(2)  # Reduce spacing between radio buttons
        presets_layout.setContentsMargins(6, 6, 6, 6)  # Reduce margins
        for mode_name in self._sensing_mode_presets.keys():
            presets_layout.addWidget(self._sensing_mode_radios[mode_name])
        log_presets.addWidget(presets_gb, 1)
        
        root.addLayout(log_presets, 0)  # Reduced stretch - log takes less vertical space
        root.addWidget(self.status_lbl)

    def _wire_events(self):
        self.refresh_btn.clicked.connect(self.refresh_ports)
        self.connect_btn.clicked.connect(self.toggle_connect)
        self.ident_btn.clicked.connect(lambda: self.send_cmd("I"))

        self.read_reg_btn.clicked.connect(self._read_registers)
        self.read_mtp_btn.clicked.connect(self._read_mtp)
        self.measure_btn.clicked.connect(lambda: self.send_cmd("M"))

        self.prog_reg_btn.clicked.connect(self.program_registers)
        self.prog_mtp_btn.clicked.connect(self.program_mtp)
        self.lock_mtp_btn.clicked.connect(self.lock_mtp)
        
        self.copy_reg_to_mtp_btn.clicked.connect(self._copy_reg_to_mtp)

    def _read_registers(self):
        """Read registers and unlock program registers button."""
        self._reg_read = True
        self._update_program_buttons()
        self.send_cmd("C")

    def _read_mtp(self):
        """Read MTP and unlock program MTP button."""
        self._mtp_read = True
        self._update_program_buttons()
        self.send_cmd("R")

    def _on_sensing_mode_selected(self, button: QRadioButton):
        """Apply sensing mode preset to REG values."""
        mode_name = button.text()
        if mode_name not in self._sensing_mode_presets:
            return
        
        axis_ch1, axis_ch2, platez = self._sensing_mode_presets[mode_name]
        
        # Find the specs for AXIS_CH1, AXIS_CH2, PLATEZ and set their values
        for spec in FIELD_SPECS:
            if spec.name == "AXIS_CH1":
                set_reg_field(self.reg_words, spec, axis_ch1)
            elif spec.name == "AXIS_CH2":
                set_reg_field(self.reg_words, spec, axis_ch2)
            elif spec.name == "PLATEZ":
                set_reg_field(self.reg_words, spec, platez)
        
        self._refresh_tables_from_state()
        self._log(f"Applied sensing mode preset: {mode_name}")

    def _copy_reg_to_mtp(self):
        """Copy REG values to MTP for all fields in the programmable customer area."""
        copied_count = 0
        for spec in FIELD_SPECS:
            # Only copy fields that exist in both REG and MTP, and are in programmable MTP range
            if spec.reg_addr is not None and spec.mtp_addr <= 0x0E:
                reg_val = get_reg_field(self.reg_words, spec)
                if reg_val is not None:
                    set_mtp_field(self.mtp_words, spec, reg_val)
                    copied_count += 1
        
        self._refresh_tables_from_state()
        self._log(f"Copied REG to MTP ({copied_count} fields)")

    def _init_tables(self):
        # Make REG and MTP tables read-only (users edit via decoded parameters table)
        self.reg_table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.mtp_table.setEditTriggers(QTableWidget.NoEditTriggers)
        
        # Registers table rows: 0x20..0x2E step 2
        for i, addr in enumerate(range(0x20, 0x30, 2)):
            self.reg_table.setItem(i, 0, QTableWidgetItem(f"0x{addr:02X}"))
            self.reg_table.setItem(i, 1, QTableWidgetItem("0"))
            self.reg_table.setItem(i, 2, QTableWidgetItem("0x0000"))

        # MTP table rows: 0x00..0x1E step 2
        for i, addr in enumerate(range(0x00, 0x20, 2)):
            self.mtp_table.setItem(i, 0, QTableWidgetItem(f"0x{addr:02X}"))
            self.mtp_table.setItem(i, 1, QTableWidgetItem("0"))
            self.mtp_table.setItem(i, 2, QTableWidgetItem("0x0000"))

    def _init_decoded_table(self):
        for row, spec in enumerate(FIELD_SPECS):
            self.dec_table.setItem(row, 0, QTableWidgetItem(spec.name))

            # REG columns: editable for fields with reg_addr, N/A for MTP-only
            if spec.reg_addr is not None:
                reg_dec_item = QTableWidgetItem("0")
                reg_dec_item.setFlags(reg_dec_item.flags() | Qt.ItemIsEditable)
                reg_hex_item = QTableWidgetItem("0x0")
            else:
                reg_dec_item = QTableWidgetItem("N/A")
                reg_dec_item.setFlags(reg_dec_item.flags() & ~Qt.ItemIsEditable)
                reg_hex_item = QTableWidgetItem("N/A")
            
            self.dec_table.setItem(row, 1, reg_dec_item)
            self.dec_table.setItem(row, 3, reg_hex_item)

            # MTP columns: editable for programmable addresses (0x00-0x0E), read-only otherwise
            mtp_dec_item = QTableWidgetItem("0")
            if spec.mtp_addr <= 0x0E:
                mtp_dec_item.setFlags(mtp_dec_item.flags() | Qt.ItemIsEditable)
            else:
                mtp_dec_item.setFlags(mtp_dec_item.flags() & ~Qt.ItemIsEditable)
            self.dec_table.setItem(row, 2, mtp_dec_item)
            self.dec_table.setItem(row, 4, QTableWidgetItem("0x0"))

    def refresh_ports(self):
        self.port_cb.clear()
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            self.port_cb.addItem(p.device)
        if not ports:
            self.port_cb.addItem("")

    def toggle_connect(self):
        if self.connect_btn.text() == "Connect":
            port = self.port_cb.currentText().strip()
            if not port:
                QMessageBox.warning(self, "No port", "Select a COM port first.")
                return
            self.worker.open(port, 115200)
        else:
            self.worker.close()

    def send_cmd(self, c: str):
        if not c or len(c) != 1:
            return
        if not self.worker.is_connected():
            return  # Buttons should be disabled anyway
        self.worker.send_text(c)
        self._log(f">> {c}")

    def _log(self, s: str):
        self.log.append(s)

    def _set_actions_enabled(self, enabled: bool):
        """Enable or disable all action buttons based on connection state."""
        for btn in self._action_buttons:
            btn.setEnabled(enabled)
        # Program buttons have additional requirement: data must be read first
        self._update_program_buttons()

    def _update_program_buttons(self):
        """Update program button states based on connection and read status."""
        # Program REG requires: connected AND registers have been read
        self.prog_reg_btn.setEnabled(self._connected and self._reg_read)
        # Program MTP requires: connected AND MTP has been read
        self.prog_mtp_btn.setEnabled(self._connected and self._mtp_read)

    @Slot(bool, str)
    def on_conn_state(self, ok: bool, msg: str):
        self.status_lbl.setText(msg)
        self.connect_btn.setText("Disconnect" if ok else "Connect")
        self._connected = ok
        # Reset read flags on disconnect
        if not ok:
            self._reg_read = False
            self._mtp_read = False
        self._set_actions_enabled(ok)

    @Slot(bytes, str)
    def on_line_rx(self, raw: bytes, line: str):
        # Log received line
        self._log(f"<< {line}")

        # Identify line
        if line.strip() == "90381":
            self._log("Firmware ID OK (90381).")
            return

        # Measurement line
        meas = parse_measurement(line)
        if meas:
            self._log(f"Measurement parsed: OUT1={meas.out1}, OUT2={meas.out2}")
            return

        # Hex pairs lines (C / R)
        pairs = parse_hex_pairs_line(line)
        if pairs:
            # Heuristic: if address >= 0x20 treat as registers; else MTP
            for addr, word in pairs:
                if addr >= 0x20:
                    self.reg_words[addr] = word & 0xFFFF
                else:
                    self.mtp_words[addr] = word & 0xFFFF
            self._refresh_tables_from_state()
    
    def _refresh_decoded_table(self):
        # avoid triggering cellChanged while we update the table
        self.dec_table.blockSignals(True)
        try:
            for row, spec in enumerate(FIELD_SPECS):
                rv = get_reg_field(self.reg_words, spec)
                mv = get_mtp_field(self.mtp_words, spec)

                # REG columns: show "N/A" if reg_addr is None (MTP-only field)
                if rv is not None:
                    self.dec_table.item(row, 1).setText(str(rv))
                    self.dec_table.item(row, 3).setText(f"0x{rv:X}")
                else:
                    self.dec_table.item(row, 1).setText("N/A")
                    self.dec_table.item(row, 3).setText("N/A")

                # MTP columns: always valid since all fields have mtp_addr
                self.dec_table.item(row, 2).setText(str(mv))
                self.dec_table.item(row, 4).setText(f"0x{mv:X}")
        finally:
            self.dec_table.blockSignals(False)

    def _refresh_tables_from_state(self):
        for i, addr in enumerate(range(0x20, 0x30, 2)):
            w = self.reg_words.get(addr, 0)
            self.reg_table.item(i, 1).setText(str(w))
            self.reg_table.item(i, 2).setText(f"0x{w:04X}")

        for i, addr in enumerate(range(0x00, 0x20, 2)):
            w = self.mtp_words.get(addr, 0)
            self.mtp_table.item(i, 1).setText(str(w))
            self.mtp_table.item(i, 2).setText(f"0x{w:04X}")

        self._refresh_decoded_table()

    def _collect_buffer_words_from_reg_table(self) -> List[int]:
        """
        Collect 8 words from register table (0x20..0x2E) for register programming.
        """
        vals = []
        for i in range(8):
            txt = self.reg_table.item(i, 1).text().strip()
            try:
                v = int(txt, 10) & 0xFFFF
            except ValueError:
                v = 0
            vals.append(v)
        return vals

    def _collect_buffer_words_from_mtp_table(self, start_row: int = 0, count: int = 8) -> List[int]:
        """
        Collect words from MTP table for MTP programming.
        MTP table has 16 rows (0x00..0x1E), but firmware only supports 8 words per W command.
        """
        vals = []
        for i in range(start_row, min(start_row + count, 16)):
            txt = self.mtp_table.item(i, 1).text().strip()
            try:
                v = int(txt, 10) & 0xFFFF
            except ValueError:
                v = 0
            vals.append(v)
        return vals

    def _send_memory_sequence(self, words: List[int], command: str = "W"):
        """
        Emulate firmware's interactive 'W'/'E' loader.
        IMPORTANT: firmware reads exactly 5 chars per number, then waits for y/n/a.
        We'll always 'y' accept.
        """
        if len(words) != 8:
            raise ValueError("Need exactly 8 words for memory sequence.")
        self.send_cmd(command)
        # Give MCU time to print prompts; we do not rely on them, but pacing helps.
        time.sleep(0.05)

        for i, w in enumerate(words):
            field = f"{int(w):5d}"  # exactly 5 chars
            self.worker.send_text(field)
            time.sleep(0.01)
            self.worker.send_text("y")  # accept
            time.sleep(0.02)

        # after last entry the loop ends naturally; no need to send 'a'

    def program_registers(self):
        words8 = self._collect_buffer_words_from_reg_table()
        try:
            self._send_memory_sequence(words8)
            time.sleep(0.05)
            self.send_cmd("S")
        except Exception as e:
            QMessageBox.critical(self, "Error", str(e))

    def program_mtp(self):
        # Read first 8 words from MTP table (0x00-0x0E) - only this range is programmable
        words8 = self._collect_buffer_words_from_mtp_table(start_row=0, count=8)
        
        # Strong guardrails for MTP: word 6 (0x0C) contains MEMLOCK/DIS_DIAG
        if words8[6] != 0:
            r = QMessageBox.warning(
                self,
                "Unsafe MTP content",
                "MTP address 0x0C contains MEMLOCK and diagnostic bits.\n"
                "Non-zero values can permanently lock the chip or disable features.\n\n"
                "Set this back to 0 unless you are absolutely sure.\n\nContinue anyway?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            )
            if r != QMessageBox.StandardButton.Yes:
                return

        r = QMessageBox.question(
            self,
            "Confirm MTP write",
            "Programming MTP is non-volatile and has limited write cycles.\n"
            "This will program addresses 0x00-0x0E (8 words).\n\n"
            "Proceed with Program MTP (E+P)?",
        )
        if r != QMessageBox.StandardButton.Yes:
            return

        try:
            self._send_memory_sequence(words8, command="E")
            time.sleep(0.05)
            self.send_cmd("P")
        except Exception as e:
            QMessageBox.critical(self, "Error", str(e))

    def lock_mtp(self):
        # r = QMessageBox.warning(
        #     self, "Confirm MTP Lock",
        #     "Lock MTP (MEMLOCK) is permanent. After this, MTP cannot be reprogrammed.\n\nProceed?",
        #     QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        # )
        # if r == QMessageBox.StandardButton.Yes:
        #     self.send_cmd("L")
        pass

    def closeEvent(self, event):
        """Clean up the serial worker thread before closing."""
        self.worker.close()
        event.accept()


def main():
    app = QApplication(sys.argv)
    w = MainWindow()
    w.resize(1200, 850)  # pick a height that fits all three tables comfortably
    w.setMinimumHeight(850)
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
