# MLX90381 UI Implementation

A Python/PySide6 desktop application for communicating with an MCU that bridges I²C communication with the MLX90381 magnetic sensor.

## Architecture

### Stack
- **PySide6 (Qt6)** - Desktop UI framework
- **pyserial** - Serial port communication
- **QThread** - Background serial worker for non-blocking I/O

### Key Components

1. **SerialWorker (QThread)** - Background thread that:
   - Continuously reads serial data
   - Emits `line_rx(bytes, str)` signal for received lines
   - Emits `conn_state(bool, str)` signal for connection status changes
   - Handles connect/disconnect/send operations

2. **MainWindow (QWidget)** - Main UI with:
   - Connection controls (COM port selection, connect/disconnect)
   - Action buttons for sensor operations
   - Three data tables (REG, MTP, Decoded parameters)
   - Sensing mode presets
   - Log pane

## Memory Model

### Registers (Volatile) - 0x20..0x2E
- 8 words (16-bit each)
- Read via `C` command, programmed via `W` + `S` commands
- Stored in `reg_words: Dict[int, int]`

### MTP (Non-Volatile) - 0x00..0x1E  
- 16 words total, but only 0x00-0x0E (8 words) are programmable
- Read via `R` command, programmed via `E` + `P` commands
- Stored in `mtp_words: Dict[int, int]`

### Field Specifications
Each decoded parameter is defined by `FieldSpec`:
```python
@dataclass(frozen=True)
class FieldSpec:
    name: str
    reg_addr: Optional[int]  # 0x20..0x2E, or None if MTP-only
    mtp_addr: int            # 0x00..0x1E
    byte_sel: str            # "LSB" or "MSB"
    lsb: int                 # bit index within byte (0..7)
    width: int               # number of bits
```

Fields with `reg_addr=None` are MTP-only (e.g., CHIP_ID, MEMLOCK).

## UART Protocol

| Command | Description | Response Format |
|---------|-------------|-----------------|
| `I` | Identify firmware | `90381\n` |
| `C` | Read registers (0x20-0x2E) | `20 {hex} 22 {hex} ... 2E {hex}\n` |
| `R` | Read MTP (0x00-0x1E) | `0 {hex} 2 {hex} ... 1E {hex}\n` |
| `M` | Measure outputs | ` OUT1 {dec}  OUT2 {dec}\n` |
| `W` | Load register buffer (interactive) | Prompts for 8 values |
| `E` | Load MTP buffer (interactive) | Prompts for 8 values |
| `S` | Program registers from buffer | Measurement output on success |
| `P` | Program MTP from buffer | Acknowledge status |
| `L` | Lock MTP (permanent) | Acknowledge status |

### Interactive W/E Sequence
For each of 8 entries:
1. MCU prints: `ADD {decimal_addr}\n`
2. UI sends: exactly 5 characters (e.g., `"  123"`)
3. MCU echoes: `{parsed_value}\n`
4. UI sends: `y` (accept), `n` (redo), or `a` (abort)

## UI Layout

```
┌─────────────────────────────────────────────────────────────────────┐
│ COM Port: [dropdown] [Refresh] [Connect] [Identify]                 │
├─────────────────────────────────────────────────────────────────────┤
│ [Read REG] [Read MTP] [Measure] [Prog REG] [Prog MTP] [Lock] [Copy] │
├─────────────────────┬─────────────────────┬─────────────────────────┤
│ Registers           │ MTP (0x00..0x1E)    │ Decoded Parameters      │
│ (0x20..0x2E)        │                     │ (customer area)         │
│ ┌──────┬────┬─────┐ │ ┌──────┬────┬─────┐ │ ┌────────┬───────┬────┐ │
│ │ ADDR │DEC │ HEX │ │ │ ADDR │DEC │ HEX │ │ │ Param  │REG_DEC│... │ │
│ │ 0x20 │ 0  │0x00 │ │ │ 0x00 │ 0  │0x00 │ │ │ RG_X   │  0    │    │ │
│ │ ...  │    │     │ │ │ ...  │    │     │ │ │ ...    │       │    │ │
│ └──────┴────┴─────┘ │ └──────┴────┴─────┘ │ └────────┴───────┴────┘ │
├─────────────────────────────────────┬───────────────────────────────┤
│ Log:                                │ Sensing Mode OUT1/2           │
│ >> C                                │ ○ X/Y mode                    │
│ << 20 0 22 0 24 0 ...               │ ○ Y/X mode                    │
│                                     │ ○ X/Z mode  ...               │
└─────────────────────────────────────┴───────────────────────────────┘
```

## Features

### Connection Management
- Auto-detect available COM ports
- Connect/Disconnect with proper thread lifecycle
- Action buttons disabled when disconnected
- Clean shutdown on window close

### Data Tables
- **REG table**: Read-only, shows raw register values (0x20-0x2E)
- **MTP table**: Read-only, shows raw MTP values (0x00-0x1E)
- **Decoded table**: 
  - REG_DEC column: Editable for fields with `reg_addr` (updates REG only)
  - MTP_DEC column: Editable for programmable MTP fields ≤0x0E (updates MTP only)
  - Shows "N/A" for fields not applicable to that memory type

### Programming Safety
- Program buttons disabled until corresponding Read is performed
- MTP programming warns about MEMLOCK/DIS_DIAG at address 0x0C
- Confirmation dialogs before MTP writes (limited write cycles)

### Sensing Mode Presets
Quick configuration of AXIS_CH1, AXIS_CH2, PLATEZ for common output modes:

| Mode | AXIS_CH1 | AXIS_CH2 | PLATEZ | OUT1 | OUT2 |
|------|----------|----------|--------|------|------|
| X/Y  | 0 | 1 | 0 | X-axis | Y-axis |
| Y/X  | 1 | 0 | 0 | Y-axis | X-axis |
| X/Z  | 0 | 2 | 2 | X-axis | Z-axis |
| Z/X  | 2 | 0 | 2 | Z-axis | X-axis |
| Y/Z  | 1 | 2 | 1 | Y-axis | Z-axis |
| Z/Y  | 2 | 1 | 1 | Z-axis | Y-axis |

### Utilities
- **Copy REG → MTP**: Copies all editable register values to MTP buffer for programming

## File Structure

```
main.py          - Complete UI application
main.cpp         - MCU firmware (for reference)
IMPLEMENTATION.md - This documentation
```

## Dependencies

```
PySide6
pyserial
```

## Running

```bash
python main.py
```
