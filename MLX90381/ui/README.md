# MLX90381 UART UI (Tauri)

A lightweight, cross-platform UI for MLX90381 magnetic sensor configuration via UART/MCU bridge.

**Binary size: ~5-8 MB** (compared to ~100+ MB with PySide6)

## Prerequisites

### Windows
1. Install [Node.js](https://nodejs.org/) (LTS version)
2. Install [Rust](https://rustup.rs/)
3. Install Visual Studio Build Tools with C++ workload

### macOS
1. Install Xcode Command Line Tools: `xcode-select --install`
2. Install Node.js: `brew install node`
3. Install Rust: `curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh`

### Linux
1. Install dependencies: `sudo apt install libwebkit2gtk-4.1-dev build-essential curl wget file libssl-dev libgtk-3-dev libayatana-appindicator3-dev librsvg2-dev libudev-dev`
2. Install Node.js and Rust

## Setup

```bash
cd tauri-ui
npm install
```

## Development

```bash
npm run dev
```

This opens a development window with hot-reload for the frontend.

## Build

```bash
npm run build
```

Output will be in `src-tauri/target/release/`:
- Windows: `MLX90381_UI.exe` (~5-8 MB)
- macOS: `MLX90381_UI.app`
- Linux: `mlx90381-ui`

## Generate Icons

Before first build, generate app icons using the Tauri CLI:

```bash
# Place a 1024x1024 PNG as src-tauri/icons/app-icon.png, then:
npx tauri icon src-tauri/icons/app-icon.png
```

Or use the default Tauri icons for testing.

## Cross-Platform Notes

- **Serial port names**: Windows uses `COM3`, Linux `/dev/ttyUSB0`, macOS `/dev/cu.usbserial-*`
- **Permissions**: On Linux, add user to `dialout` group: `sudo usermod -aG dialout $USER`
- Same source code works on all platforms - no rewrites needed!

## Project Structure

```
tauri-ui/
├── dist/                  # Frontend (HTML/CSS/JS)
│   ├── index.html
│   ├── style.css
│   └── main.js
├── src-tauri/             # Rust backend
│   ├── src/
│   │   ├── main.rs
│   │   └── serial_handler.rs
│   ├── Cargo.toml
│   └── tauri.conf.json
└── package.json
```

