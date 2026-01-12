// MLX90381 UI - Main Application Logic

// Tauri v2 API access - wait for it to be available
let invoke;

async function initTauri() {
  // For Tauri v2, the API is exposed on window.__TAURI__
  if (window.__TAURI__ && window.__TAURI__.core) {
    invoke = window.__TAURI__.core.invoke;
    console.log("Tauri API initialized via window.__TAURI__.core");
  } else if (window.__TAURI_INTERNALS__) {
    invoke = window.__TAURI_INTERNALS__.invoke;
    console.log("Tauri API initialized via __TAURI_INTERNALS__");
  } else {
    console.error("Tauri API not found!");
    log("ERROR: Tauri API not available", "error");
    return false;
  }
  return true;
}

// ---------- Field Specifications ----------
const FIELD_SPECS = [
  // Fields valid for both REG and MTP (reg_addr = mtp_addr + 0x20)
  { name: "RG_X", regAddr: 0x20, mtpAddr: 0x00, byteSel: "LSB", lsb: 0, width: 3 },
  { name: "FG_X", regAddr: 0x20, mtpAddr: 0x00, byteSel: "LSB", lsb: 3, width: 5 },
  { name: "RG_Y", regAddr: 0x22, mtpAddr: 0x02, byteSel: "LSB", lsb: 0, width: 3 },
  { name: "FG_Y", regAddr: 0x22, mtpAddr: 0x02, byteSel: "LSB", lsb: 3, width: 5 },
  { name: "RG_Z", regAddr: 0x24, mtpAddr: 0x04, byteSel: "LSB", lsb: 0, width: 3 },
  { name: "FG_Z", regAddr: 0x24, mtpAddr: 0x04, byteSel: "LSB", lsb: 3, width: 5 },
  { name: "VOQ_OUT1", regAddr: 0x20, mtpAddr: 0x00, byteSel: "MSB", lsb: 0, width: 4 },
  { name: "VOQ_OUT2", regAddr: 0x22, mtpAddr: 0x02, byteSel: "MSB", lsb: 0, width: 4 },
  { name: "AXIS_CH1", regAddr: 0x26, mtpAddr: 0x06, byteSel: "LSB", lsb: 0, width: 2 },
  { name: "AXIS_CH2", regAddr: 0x26, mtpAddr: 0x06, byteSel: "LSB", lsb: 2, width: 2 },
  { name: "PLATEZ", regAddr: 0x26, mtpAddr: 0x06, byteSel: "LSB", lsb: 4, width: 2 },
  { name: "TC", regAddr: 0x28, mtpAddr: 0x08, byteSel: "LSB", lsb: 0, width: 5 },
  { name: "FILT", regAddr: 0x2A, mtpAddr: 0x0A, byteSel: "LSB", lsb: 0, width: 5 },
  // Fields valid for MTP only (regAddr = null)
  { name: "DIS_DIAG", regAddr: null, mtpAddr: 0x0C, byteSel: "LSB", lsb: 1, width: 1 },
  { name: "MEMLOCK", regAddr: null, mtpAddr: 0x0C, byteSel: "LSB", lsb: 0, width: 1 },
  { name: "TC350_DATA", regAddr: null, mtpAddr: 0x0E, byteSel: "LSB", lsb: 0, width: 4 },
  { name: "TC2000_DATA", regAddr: null, mtpAddr: 0x14, byteSel: "MSB", lsb: 4, width: 4 },
  { name: "CHIP_ID1", regAddr: null, mtpAddr: 0x1A, byteSel: "LSB", lsb: 0, width: 8 },
  { name: "CHIP_ID2", regAddr: null, mtpAddr: 0x1C, byteSel: "LSB", lsb: 0, width: 8 },
  { name: "CHIP_ID3", regAddr: null, mtpAddr: 0x1E, byteSel: "LSB", lsb: 0, width: 8 },
];

// Sensing mode presets: {mode: [AXIS_CH1, AXIS_CH2, PLATEZ]}
const SENSING_MODES = {
  xy: [0, 1, 0], // X/Y mode
  yx: [1, 0, 0], // Y/X mode
  xz: [0, 2, 2], // X/Z mode
  zx: [2, 0, 2], // Z/X mode
  yz: [1, 2, 1], // Y/Z mode
  zy: [2, 1, 1], // Z/Y mode
};

// ---------- State ----------
let regWords = {}; // addr -> word (0x20..0x2E)
let mtpWords = {}; // addr -> word (0x00..0x1E)
let connected = false;
let regRead = false;
let mtpRead = false;
let pollInterval = null;

// ---------- DOM Elements ----------
const portSelect = document.getElementById("port-select");
const statusEl = document.getElementById("status");
const logEl = document.getElementById("log");

// Buttons
const btnRefresh = document.getElementById("btn-refresh");
const btnConnect = document.getElementById("btn-connect");
const btnIdentify = document.getElementById("btn-identify");
const btnReadReg = document.getElementById("btn-read-reg");
const btnReadMtp = document.getElementById("btn-read-mtp");
const btnMeasure = document.getElementById("btn-measure");
const btnProgReg = document.getElementById("btn-prog-reg");
const btnProgMtp = document.getElementById("btn-prog-mtp");
const btnCopyRegMtp = document.getElementById("btn-copy-reg-mtp");

const actionButtons = [btnIdentify, btnReadReg, btnReadMtp, btnMeasure, btnProgReg, btnProgMtp, btnCopyRegMtp];

// ---------- Utility Functions ----------
function getByte(word, byteSel) {
  word &= 0xFFFF;
  return byteSel === "LSB" ? (word & 0xFF) : ((word >> 8) & 0xFF);
}

function setByte(word, byteSel, byteVal) {
  word &= 0xFFFF;
  byteVal &= 0xFF;
  return byteSel === "LSB" 
    ? (word & 0xFF00) | byteVal 
    : (word & 0x00FF) | (byteVal << 8);
}

function getFieldAt(words, addr, spec) {
  if (addr === null) return null;
  const w = (words[addr] || 0) & 0xFFFF;
  const b = getByte(w, spec.byteSel);
  const mask = (1 << spec.width) - 1;
  return (b >> spec.lsb) & mask;
}

function setFieldAt(words, addr, spec, value) {
  if (addr === null) return false;
  const mask = (1 << spec.width) - 1;
  const v = value & mask;
  const w = (words[addr] || 0) & 0xFFFF;
  let b = getByte(w, spec.byteSel);
  b = (b & ~(mask << spec.lsb)) | (v << spec.lsb);
  words[addr] = setByte(w, spec.byteSel, b) & 0xFFFF;
  return true;
}

function getRegField(spec) {
  return getFieldAt(regWords, spec.regAddr, spec);
}

function getMtpField(spec) {
  return getFieldAt(mtpWords, spec.mtpAddr, spec);
}

function setRegField(spec, value) {
  return setFieldAt(regWords, spec.regAddr, spec, value);
}

function setMtpField(spec, value) {
  return setFieldAt(mtpWords, spec.mtpAddr, spec, value);
}

// ---------- Logging ----------
function log(msg, type = "info") {
  const line = document.createElement("div");
  line.className = type;
  line.textContent = msg;
  logEl.appendChild(line);
  logEl.scrollTop = logEl.scrollHeight;
}

// ---------- UI State Update ----------
function updateConnectionUI() {
  btnConnect.textContent = connected ? "Disconnect" : "Connect";
  statusEl.textContent = connected ? "Connected" : "Disconnected";
  statusEl.className = `status ${connected ? "connected" : "disconnected"}`;
  
  actionButtons.forEach(btn => {
    if (btn === btnProgReg) {
      btn.disabled = !connected || !regRead;
    } else if (btn === btnProgMtp) {
      btn.disabled = !connected || !mtpRead;
    } else if (btn === btnCopyRegMtp) {
      btn.disabled = !regRead;
    } else {
      btn.disabled = !connected;
    }
  });
}

// ---------- Table Initialization ----------
function initRegTable() {
  const tbody = document.querySelector("#reg-table tbody");
  tbody.innerHTML = "";
  for (let addr = 0x20; addr <= 0x2E; addr += 2) {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td class="addr">0x${addr.toString(16).toUpperCase()}</td>
      <td id="reg-dec-${addr}">0</td>
      <td class="hex" id="reg-hex-${addr}">0x0000</td>
    `;
    tbody.appendChild(tr);
    regWords[addr] = 0;
  }
}

function initMtpTable() {
  const tbody = document.querySelector("#mtp-table tbody");
  tbody.innerHTML = "";
  for (let addr = 0x00; addr <= 0x1E; addr += 2) {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td class="addr">0x${addr.toString(16).toUpperCase().padStart(2, "0")}</td>
      <td id="mtp-dec-${addr}">0</td>
      <td class="hex" id="mtp-hex-${addr}">0x0000</td>
    `;
    tbody.appendChild(tr);
    mtpWords[addr] = 0;
  }
}

function initDecodedTable() {
  const tbody = document.querySelector("#decoded-table tbody");
  tbody.innerHTML = "";
  
  FIELD_SPECS.forEach((spec, idx) => {
    const tr = document.createElement("tr");
    const regEditable = spec.regAddr !== null;
    const mtpEditable = spec.mtpAddr <= 0x0E;
    
    tr.innerHTML = `
      <td>${spec.name}</td>
      <td class="${regEditable ? "editable" : "na"}">
        ${regEditable 
          ? `<input type="number" id="dec-reg-${idx}" value="0" min="0" max="${(1 << spec.width) - 1}">`
          : "N/A"}
      </td>
      <td class="${mtpEditable ? "editable" : ""}">
        <input type="number" id="dec-mtp-${idx}" value="0" min="0" max="${(1 << spec.width) - 1}" ${mtpEditable ? "" : "disabled"}>
      </td>
      <td class="hex" id="dec-reg-hex-${idx}">${regEditable ? "0x0" : "N/A"}</td>
      <td class="hex" id="dec-mtp-hex-${idx}">0x0</td>
    `;
    tbody.appendChild(tr);
    
    // Event listeners for editable fields
    if (regEditable) {
      const regInput = document.getElementById(`dec-reg-${idx}`);
      regInput.addEventListener("change", () => {
        let v = parseInt(regInput.value) || 0;
        v &= (1 << spec.width) - 1;
        setRegField(spec, v);
        refreshTables();
      });
    }
    
    if (mtpEditable) {
      const mtpInput = document.getElementById(`dec-mtp-${idx}`);
      mtpInput.addEventListener("change", () => {
        let v = parseInt(mtpInput.value) || 0;
        v &= (1 << spec.width) - 1;
        setMtpField(spec, v);
        refreshTables();
      });
    }
  });
}

// ---------- Table Refresh ----------
function refreshTables() {
  // Register table
  for (let addr = 0x20; addr <= 0x2E; addr += 2) {
    const w = regWords[addr] || 0;
    document.getElementById(`reg-dec-${addr}`).textContent = w;
    document.getElementById(`reg-hex-${addr}`).textContent = `0x${w.toString(16).toUpperCase().padStart(4, "0")}`;
  }
  
  // MTP table
  for (let addr = 0x00; addr <= 0x1E; addr += 2) {
    const w = mtpWords[addr] || 0;
    document.getElementById(`mtp-dec-${addr}`).textContent = w;
    document.getElementById(`mtp-hex-${addr}`).textContent = `0x${w.toString(16).toUpperCase().padStart(4, "0")}`;
  }
  
  // Decoded table
  FIELD_SPECS.forEach((spec, idx) => {
    const rv = getRegField(spec);
    const mv = getMtpField(spec);
    
    const regInput = document.getElementById(`dec-reg-${idx}`);
    const mtpInput = document.getElementById(`dec-mtp-${idx}`);
    const regHex = document.getElementById(`dec-reg-hex-${idx}`);
    const mtpHex = document.getElementById(`dec-mtp-hex-${idx}`);
    
    if (regInput && rv !== null) {
      regInput.value = rv;
      regHex.textContent = `0x${rv.toString(16).toUpperCase()}`;
    }
    
    if (mtpInput) {
      mtpInput.value = mv;
      mtpHex.textContent = `0x${mv.toString(16).toUpperCase()}`;
    }
  });
}

// ---------- Serial Communication ----------
async function refreshPorts() {
  if (!invoke) {
    log("Cannot refresh ports: Tauri API not available", "error");
    return;
  }
  
  try {
    log("Scanning for serial ports...", "info");
    const ports = await invoke("list_ports");
    console.log("Ports found:", ports);
    portSelect.innerHTML = "";
    if (!ports || ports.length === 0) {
      portSelect.innerHTML = '<option value="">No ports found</option>';
      log("No serial ports detected", "info");
    } else {
      ports.forEach(p => {
        const opt = document.createElement("option");
        opt.value = p.name;
        opt.textContent = p.description ? `${p.name} - ${p.description}` : p.name;
        portSelect.appendChild(opt);
      });
      log(`Found ${ports.length} port(s)`, "info");
    }
  } catch (e) {
    console.error("Error listing ports:", e);
    log(`Error listing ports: ${e}`, "error");
  }
}

async function toggleConnect() {
  if (!connected) {
    const port = portSelect.value;
    if (!port) {
      log("Select a COM port first", "error");
      return;
    }
    try {
      const result = await invoke("connect_port", { portName: port, baudRate: 115200 });
      if (result.success) {
        connected = true;
        regRead = false;
        mtpRead = false;
        log(result.message, "info");
        startPolling();
      } else {
        log(result.message, "error");
      }
    } catch (e) {
      log(`Connection error: ${e}`, "error");
    }
  } else {
    try {
      await invoke("disconnect_port");
      connected = false;
      regRead = false;
      mtpRead = false;
      log("Disconnected", "info");
      stopPolling();
    } catch (e) {
      log(`Disconnect error: ${e}`, "error");
    }
  }
  updateConnectionUI();
}

async function sendCommand(cmd) {
  if (!connected) return;
  try {
    const result = await invoke("send_command", { command: cmd });
    log(`>> ${cmd}`, "tx");
    if (!result.success) {
      log(result.message, "error");
    }
  } catch (e) {
    log(`Send error: ${e}`, "error");
  }
}

// ---------- Polling for incoming data ----------
function startPolling() {
  if (pollInterval) return;
  pollInterval = setInterval(pollIncoming, 20);  // Poll every 20ms for faster response
}

function stopPolling() {
  if (pollInterval) {
    clearInterval(pollInterval);
    pollInterval = null;
  }
}

async function pollIncoming() {
  if (!connected) return;
  try {
    const result = await invoke("read_incoming");
    for (const line of result.lines) {
      log(`<< ${line}`, "rx");
      parseLine(line);
    }
  } catch (e) {
    // Ignore read timeouts
  }
}

// ---------- Line Parsing ----------
function parseLine(line) {
  // Identify response
  if (line.trim() === "90381") {
    log("Firmware ID OK (90381)", "info");
    return;
  }
  
  // Measurement response: OUT1 1234 OUT2 5678
  const measMatch = line.match(/OUT1\s+(\d+)\s+OUT2\s+(\d+)/i);
  if (measMatch) {
    log(`Measurement: OUT1=${measMatch[1]}, OUT2=${measMatch[2]}`, "info");
    return;
  }
  
  // Hex pairs: 20 1234 22 5678 ...
  const hexPairs = [...line.matchAll(/([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)/g)];
  if (hexPairs.length > 0) {
    for (const m of hexPairs) {
      const addr = parseInt(m[1], 16);
      const word = parseInt(m[2], 16) & 0xFFFF;
      if (addr >= 0x20) {
        regWords[addr] = word;
      } else {
        mtpWords[addr] = word;
      }
    }
    refreshTables();
  }
}

// ---------- Programming ----------
async function programRegisters() {
  // Collect 8 words from reg table (0x20..0x2E)
  const words = [];
  for (let addr = 0x20; addr <= 0x2E; addr += 2) {
    words.push(regWords[addr] || 0);
  }
  
  try {
    const result = await invoke("send_memory_sequence", { command: "W", words });
    if (result.success) {
      log("Memory sequence sent (W)", "info");
      // Wait and send S command
      setTimeout(() => sendCommand("S"), 100);
    } else {
      log(result.message, "error");
    }
  } catch (e) {
    log(`Program error: ${e}`, "error");
  }
}

async function programMtp() {
  // Check word 6 (0x0C) for MEMLOCK
  if ((mtpWords[0x0C] || 0) !== 0) {
    const proceed = await showConfirmDialog(
      "Unsafe MTP Content",
      "MTP address 0x0C contains MEMLOCK and diagnostic bits. " +
      "Non-zero values can permanently lock the chip or disable features.\n\n" +
      "Set this back to 0 unless you are absolutely sure. Continue anyway?"
    );
    if (!proceed) return;
  }
  
  const proceed = await showConfirmDialog(
    "Confirm MTP Write",
    "Programming MTP is non-volatile and has limited write cycles. " +
    "This will program addresses 0x00-0x0E (8 words).\n\n" +
    "Proceed with Program MTP (E+P)?"
  );
  if (!proceed) return;
  
  // Collect first 8 words from MTP table (0x00-0x0E)
  const words = [];
  for (let addr = 0x00; addr <= 0x0E; addr += 2) {
    words.push(mtpWords[addr] || 0);
  }
  
  try {
    const result = await invoke("send_memory_sequence", { command: "E", words });
    if (result.success) {
      log("Memory sequence sent (E)", "info");
      // Wait and send P command
      setTimeout(() => sendCommand("P"), 100);
    } else {
      log(result.message, "error");
    }
  } catch (e) {
    log(`Program error: ${e}`, "error");
  }
}

function copyRegToMtp() {
  let copied = 0;
  FIELD_SPECS.forEach(spec => {
    if (spec.regAddr !== null && spec.mtpAddr <= 0x0E) {
      const regVal = getRegField(spec);
      if (regVal !== null) {
        setMtpField(spec, regVal);
        copied++;
      }
    }
  });
  refreshTables();
  log(`Copied REG to MTP (${copied} fields)`, "info");
}

// ---------- Confirm Dialog ----------
function showConfirmDialog(title, message) {
  return new Promise(resolve => {
    const overlay = document.createElement("div");
    overlay.className = "dialog-overlay";
    overlay.innerHTML = `
      <div class="dialog">
        <h4>${title}</h4>
        <p>${message.replace(/\n/g, "<br>")}</p>
        <div class="dialog-buttons">
          <button class="btn btn-secondary" id="dialog-cancel">Cancel</button>
          <button class="btn btn-warning" id="dialog-confirm">Proceed</button>
        </div>
      </div>
    `;
    document.body.appendChild(overlay);
    
    document.getElementById("dialog-cancel").onclick = () => {
      overlay.remove();
      resolve(false);
    };
    document.getElementById("dialog-confirm").onclick = () => {
      overlay.remove();
      resolve(true);
    };
  });
}

// ---------- Sensing Mode Presets ----------
function applySensingMode(mode) {
  const preset = SENSING_MODES[mode];
  if (!preset) return;
  
  const [axisCh1, axisCh2, platez] = preset;
  FIELD_SPECS.forEach(spec => {
    if (spec.name === "AXIS_CH1") setRegField(spec, axisCh1);
    else if (spec.name === "AXIS_CH2") setRegField(spec, axisCh2);
    else if (spec.name === "PLATEZ") setRegField(spec, platez);
  });
  
  refreshTables();
  log(`Applied sensing mode: ${mode.toUpperCase()}`, "info");
}

// ---------- Event Bindings ----------
btnRefresh.addEventListener("click", refreshPorts);
btnConnect.addEventListener("click", toggleConnect);
btnIdentify.addEventListener("click", () => sendCommand("I"));
btnReadReg.addEventListener("click", () => {
  regRead = true;
  updateConnectionUI();
  sendCommand("C");
});
btnReadMtp.addEventListener("click", () => {
  mtpRead = true;
  updateConnectionUI();
  sendCommand("R");
});
btnMeasure.addEventListener("click", () => sendCommand("M"));
btnProgReg.addEventListener("click", programRegisters);
btnProgMtp.addEventListener("click", programMtp);
btnCopyRegMtp.addEventListener("click", copyRegToMtp);

// Sensing mode radio buttons
document.querySelectorAll('input[name="sensing-mode"]').forEach(radio => {
  radio.addEventListener("change", e => {
    if (e.target.checked) {
      applySensingMode(e.target.value);
    }
  });
});

// ---------- Initialize ----------
async function init() {
  // Initialize Tauri API first
  const tauriReady = await initTauri();
  if (!tauriReady) {
    log("Failed to initialize Tauri API - running in browser mode?", "error");
  }
  
  initRegTable();
  initMtpTable();
  initDecodedTable();
  updateConnectionUI();
  
  if (tauriReady) {
    await refreshPorts();
  }
  
  log("MLX90381 UI initialized", "info");
}

// Wait for DOM and Tauri to be ready
document.addEventListener("DOMContentLoaded", () => {
  // Small delay to ensure Tauri injects its API
  setTimeout(init, 100);
});

