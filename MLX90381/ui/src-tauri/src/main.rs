// Prevents additional console window on Windows in release
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod serial_handler;

use serial_handler::SerialState;
use std::sync::Arc;
use tokio::sync::Mutex;

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .manage(Arc::new(Mutex::new(SerialState::new())))
        .invoke_handler(tauri::generate_handler![
            serial_handler::list_ports,
            serial_handler::connect_port,
            serial_handler::disconnect_port,
            serial_handler::send_command,
            serial_handler::send_memory_sequence,
            serial_handler::read_incoming,
            serial_handler::is_connected,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

