use serde::{Deserialize, Serialize};
use serialport::{SerialPort, SerialPortInfo};
use std::io::{Read, Write};
use std::sync::Arc;
use std::time::Duration;
use tauri::State;
use tokio::sync::Mutex;

pub struct SerialState {
    port: Option<Box<dyn SerialPort>>,
    buffer: Vec<u8>,
}

impl SerialState {
    pub fn new() -> Self {
        Self {
            port: None,
            buffer: Vec::new(),
        }
    }
}

#[derive(Serialize)]
pub struct PortInfo {
    name: String,
    description: String,
}

#[derive(Serialize, Deserialize)]
pub struct SerialResult {
    success: bool,
    message: String,
}

#[derive(Serialize)]
pub struct ReadResult {
    lines: Vec<String>,
}

/// List available serial ports
#[tauri::command]
pub async fn list_ports() -> Vec<PortInfo> {
    let ports: Vec<SerialPortInfo> = serialport::available_ports().unwrap_or_default();
    ports
        .into_iter()
        .map(|p| PortInfo {
            description: match &p.port_type {
                serialport::SerialPortType::UsbPort(info) => {
                    info.product.clone().unwrap_or_default()
                }
                _ => String::new(),
            },
            name: p.port_name,
        })
        .collect()
}

/// Connect to a serial port
#[tauri::command]
pub async fn connect_port(
    state: State<'_, Arc<Mutex<SerialState>>>,
    port_name: String,
    baud_rate: u32,
) -> Result<SerialResult, String> {
    let mut state = state.lock().await;

    // Close existing connection
    if state.port.is_some() {
        state.port = None;
    }

    match serialport::new(&port_name, baud_rate)
        .timeout(Duration::from_millis(10))
        .open()
    {
        Ok(port) => {
            state.port = Some(port);
            state.buffer.clear();
            Ok(SerialResult {
                success: true,
                message: format!("Connected to {} @ {}", port_name, baud_rate),
            })
        }
        Err(e) => Ok(SerialResult {
            success: false,
            message: format!("Failed to connect: {}", e),
        }),
    }
}

/// Disconnect from serial port
#[tauri::command]
pub async fn disconnect_port(
    state: State<'_, Arc<Mutex<SerialState>>>,
) -> Result<SerialResult, String> {
    let mut state = state.lock().await;
    state.port = None;
    state.buffer.clear();
    Ok(SerialResult {
        success: true,
        message: "Disconnected".to_string(),
    })
}

/// Check if connected
#[tauri::command]
pub async fn is_connected(state: State<'_, Arc<Mutex<SerialState>>>) -> Result<bool, String> {
    let state = state.lock().await;
    Ok(state.port.is_some())
}

/// Send a single character command
#[tauri::command]
pub async fn send_command(
    state: State<'_, Arc<Mutex<SerialState>>>,
    command: String,
) -> Result<SerialResult, String> {
    let mut state = state.lock().await;

    if let Some(ref mut port) = state.port {
        match port.write(command.as_bytes()) {
            Ok(_) => Ok(SerialResult {
                success: true,
                message: format!("Sent: {}", command),
            }),
            Err(e) => Ok(SerialResult {
                success: false,
                message: format!("Write failed: {}", e),
            }),
        }
    } else {
        Ok(SerialResult {
            success: false,
            message: "Not connected".to_string(),
        })
    }
}

/// Send memory programming sequence (W or E command with 8 words)
#[tauri::command]
pub async fn send_memory_sequence(
    state: State<'_, Arc<Mutex<SerialState>>>,
    command: String,
    words: Vec<u16>,
) -> Result<SerialResult, String> {
    let mut state = state.lock().await;

    if words.len() != 8 {
        return Ok(SerialResult {
            success: false,
            message: "Need exactly 8 words".to_string(),
        });
    }

    if let Some(ref mut port) = state.port {
        // Send initial command (W or E)
        if let Err(e) = port.write(command.as_bytes()) {
            return Ok(SerialResult {
                success: false,
                message: format!("Failed to send command: {}", e),
            });
        }
        std::thread::sleep(Duration::from_millis(50));

        // Send each word as 5-char decimal + 'y' to accept
        for word in words {
            let field = format!("{:5}", word);
            if let Err(e) = port.write(field.as_bytes()) {
                return Ok(SerialResult {
                    success: false,
                    message: format!("Failed to send word: {}", e),
                });
            }
            std::thread::sleep(Duration::from_millis(10));

            if let Err(e) = port.write(b"y") {
                return Ok(SerialResult {
                    success: false,
                    message: format!("Failed to send confirm: {}", e),
                });
            }
            std::thread::sleep(Duration::from_millis(20));
        }

        Ok(SerialResult {
            success: true,
            message: "Memory sequence sent".to_string(),
        })
    } else {
        Ok(SerialResult {
            success: false,
            message: "Not connected".to_string(),
        })
    }
}

/// Read incoming data and return complete lines
#[tauri::command]
pub async fn read_incoming(
    state: State<'_, Arc<Mutex<SerialState>>>,
) -> Result<ReadResult, String> {
    let mut state = state.lock().await;
    let mut lines = Vec::new();

    // First, read all available data from port into a temporary buffer
    let mut incoming = Vec::new();
    if let Some(ref mut port) = state.port {
        let mut buf = [0u8; 256];
        loop {
            match port.read(&mut buf) {
                Ok(n) if n > 0 => {
                    incoming.extend_from_slice(&buf[..n]);
                }
                _ => break,
            }
        }
    }

    // Now extend the state buffer (port borrow is released)
    state.buffer.extend_from_slice(&incoming);

    // Extract complete lines
    while let Some(pos) = state.buffer.iter().position(|&b| b == b'\n') {
        let line_bytes: Vec<u8> = state.buffer.drain(..=pos).collect();
        let mut line = String::from_utf8_lossy(&line_bytes).to_string();
        // Trim \r\n
        line = line.trim_end_matches(&['\r', '\n'][..]).to_string();
        if !line.is_empty() {
            lines.push(line);
        }
    }

    Ok(ReadResult { lines })
}

