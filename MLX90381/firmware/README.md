# MLX90381 I2C Configuration Firmware

STM32 HAL firmware for configuring the **MLX90381** magnetic position sensor via I2C. This firmware allows End-of-Line (EoL) calibration by reading/writing the sensor's registers and MTP (Multi-Time Programmable) memory.

## Overview

The MLX90381 is a Hall-effect resolver sensor for high-speed angle measurements. It outputs SIN/COS analog signals representing magnetic angle. The sensor can be calibrated via a special I2C-like protocol through its output pins (OUT1/OUT2).

This firmware is ported from an original Mbed OS implementation to vanilla STM32 HAL for easier debugging and integration.

## Hardware Requirements

- **MCU**: STM32G474RE (Nucleo-G474RE board)
- **Sensor**: MLX90381AA
- **Connections**:
  | Signal | STM32 Pin | Sensor Pin |
  |--------|-----------|------------|
  | I2C1_SCL | PB8 | OUT1 or OUT2 |
  | I2C1_SDA | PB9 | OUT2 or OUT1 |
  | ADC1_IN7 (A4) | PC1 | OUT1 (analog) |
  | ADC1_IN6 (A5) | PC0 | OUT2 (analog) |
  | UART TX | PA2 | - |
  | UART RX | PA3 | - |

> **Note**: The MLX90381 has internal pull-up resistors that activate during I2C mode. Do NOT use external I2C pull-ups on the OUT1/OUT2 lines.

## Features

- **PTC Entry**: Special activation sequence to enable I2C communication
- **Register Read/Write**: Access volatile configuration registers
- **MTP Read/Write**: Program permanent calibration data
- **MTP Lock**: Permanently lock MTP contents (irreversible!)
- **Analog Output Measurement**: Read SIN/COS outputs via ADC

## Serial Commands

Connect via UART at **115200 baud**. Available commands:

| Command | Description |
|---------|-------------|
| `S` | **Program registers** - Write `memoryWrite[]` to volatile registers (0x20-0x2E) |
| `C` | **Check registers** - Read and display register contents |
| `P` | **Program MTP** - Write `mtpMemoryWrite[]` to MTP (0x00-0x0E) |
| `R` | **Read MTP** - Read and display MTP contents |
| `L` | **Program MEMLOCK** - ⚠️ Permanently lock MTP (irreversible!) |
| `W` | **Write data** - Enter register values manually via UART |
| `T` | **Write MTP data** - Enter MTP values manually via UART |
| `M` | **Measure outputs** - Enter application mode and read analog outputs |
| `I` | **Identify** - Print firmware identifier ("90381") |
| `A` | **Acknowledge** - Print last I2C operation status |

## Memory Maps

### Customer Registers (Volatile, 0x20-0x2E)
| Address | Description |
|---------|-------------|
| 0x20 | Customer config word 0 |
| 0x22 | Customer config word 1 |
| 0x24 | Customer config word 2 |
| 0x26 | Customer config word 3 |
| 0x28 | Customer config word 4 |
| 0x2A | Customer config word 5 |
| 0x2C | Customer config word 6 |
| 0x2E | Customer config word 7 |

### Customer MTP (Permanent, 0x00-0x0E)
| Address | Description |
|---------|-------------|
| 0x00-0x0A | Calibration parameters |
| 0x0C | MEMLOCK (bit 0 = lock MTP permanently) |
| 0x0E | Additional config |

> **Warning**: Addresses 0x10-0x1E are factory-programmed and read-only!

## Building

### Prerequisites
- ARM GCC toolchain (`arm-none-eabi-gcc`)
- CMake 3.22+
- Ninja build system

### Build Commands
```bash
# Configure (first time)
cmake --preset Debug

# Build
cmake --build build/Debug

# Clean and rebuild
cmake --build build/Debug --target clean
cmake --build build/Debug
```

### Output
- `build/Debug/MLX90381.elf` - Firmware binary

## Configuration

### Debug Messages
Edit `Core/Src/main.c`:
```c
#define MLX_DEBUG  true   /* Set to true for verbose debug messages */
```

### I2C Timing
The I2C baud rate and timing are configured in `Core/Inc/mlx90381.h`:
```c
#define MLX90381_DEFAULT_BAUDRATE       25000   /* I2C baud rate in Hz */
#define MLX90381_DEFAULT_DELAY_INST     5       /* Instruction overhead in µs */
```

## I2C Protocol Notes

The MLX90381 uses a special I2C activation sequence:

1. **PTC Entry**: Pull SDA low to trigger overcurrent detection, then send 8 clock pulses
2. **Calibration Mode**: Write `0x544E` to register `0x0044`
3. **MTP Access**: Write `0x0077` (write) or `0x0007` (read) to register `0x0046`
4. **Normal Mode**: Write `0x944C` to register `0x0044`

### Important Timing
- **I2C Communication Timeout**: 20-30ms (sensor returns to application mode if no I2C activity)
- **MTP Write Delay**: 11ms minimum per word (EEPROM erase/write cycle)

## Troubleshooting

### PTC Entry Fails (0xFE)
- Check wiring connections
- Ensure no external pull-up resistors on OUT1/OUT2
- Verify sensor is powered

### I2C Timeout Errors (0x20)
- Sensor may have exited I2C mode due to communication timeout
- The firmware automatically re-activates I2C after MTP writes
- Try the operation again

### NACK Errors (0x04)
- Sensor not in correct mode (calibration mode required for register access)
- MTP might be locked (check MEMLOCK bit at address 0x0C)
- Invalid address (only 0x00-0x0E writable in MTP)

## License

Based on original Mbed OS code © 2019 ARM Limited (Apache-2.0).
STM32 HAL port © 2025.

## References

- [MLX90381 Datasheet](https://www.melexis.com/en/product/MLX90381)
- MLX90381 I2C Communication Protocol for EoL Calibration (Application Note)

