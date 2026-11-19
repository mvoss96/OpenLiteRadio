// The tab "ELRS-Modul": everything the ELRS module offers, read from the
// module itself. The radio only passes frames on: "crsf HEX" sends one CRSF
// frame to the module, and with "frames 1" every frame from the module comes
// back as an "rx" line. The list of parameters is the module's own, so
// another ELRS version with other parameters needs no change here; only the
// labels and the grouping of known names are added.
//
// CRSF parameter protocol, extended frames between radio (0xEA) and module (0xEE):
//   0x28 ping                     -> 0x29 device info: name, serial, versions, parameter count
//   0x2C read index, chunk        -> 0x2B entry: index, chunks left, then a part of the entry
//   0x2D write index, value          no answer; the entry is read again afterwards
// An entry is: parent folder, type (bit 7: hidden), name, then data by type.

const ELRS_MODULE = 0xEE, ELRS_RADIO = 0xEA;
const ELRS_TIMEOUT_MS = 400, ELRS_TRIES = 4;
const ELRS_START_MS = 5000;   // "antwortet nicht" only once the module has been on this long
const ELRS_TYPE = { UINT8: 0, SELECT: 9, STRING: 10, FOLDER: 11, INFO: 12, COMMAND: 13 };
const COMMAND = { READY: 0, START: 1, PROGRESS: 2, CONFIRM_NEEDED: 3, CONFIRM: 4 };

// German labels and groups of the names ELRS 3.6 uses; other names are shown as the module sends them.
const ELRS_LABELS = {
  "Packet Rate": ["Paketrate", "Funk"], "Telem Ratio": ["Telemetrie", "Funk"], "Switch Mode": ["Schaltermodus", "Funk"],
  "Link Mode": ["Verbindung", "Funk"], "Model Match": ["Modell-Abgleich", "Funk"],
  "Max Power": ["Höchstens", "Sendeleistung"], "Dynamic": ["Dynamisch", "Sendeleistung"],
  "Band": ["Band", "Videosender"], "Channel": ["Kanal", "Videosender"], "Pwr Lvl": ["Leistung", "Videosender"],
  "Send VTx": ["An den Videosender", "Videosender"],
  "Bad/Good": ["Verloren / gut", "Empfänger"], "Bind": ["Neuen Empfänger binden", "Empfänger"],
  "Enable WiFi": ["WiFi für Modul-Update", "Modul"], "Enable Rx WiFi": ["WiFi für Empfänger-Update", "Modul"],
  "BLE Joystick": ["Bluetooth-Joystick", "Modul"],
};
const ELRS_BUTTONS = { "Bind": "Binden", "Send VTx": "Senden" };
// Groups in two columns, with the icon of each; a group of an unknown folder goes to the left.
const ELRS_COLUMNS = [["Funk", "Sendeleistung", "Videosender"], ["Empfänger", "Modul"]];
const ELRS_ICONS = {
  "Funk": "M12 12v9M8.5 8.5a5 5 0 0 1 7 0M5.6 5.6a9 9 0 0 1 12.8 0", "Sendeleistung": "M13 3 5 14h6l-1 7 8-11h-6z",
  "Videosender": "M3 7h12v10H3zM15 10l6-3v10l-6-3", "Empfänger": "M9 15l6-6M10 6l1-1a4 4 0 0 1 6 6l-1 1M14 18l-1 1a4 4 0 0 1-6-6l1-1",
  "Modul": "M7 7h10v10H7zM10 3v4M14 3v4M10 17v4M14 17v4M3 10h4M3 14h4M17 10h4M17 14h4",
};

const elrs = {
  open: false,       // the tab is shown
  moduleOn: false,   // from the last state line
  device: null,      // { name, count } from the device info
  entries: [],       // index -> parsed entry
  failed: false,     // the module did not answer
  waiting: null,     // the request that waits for its answer: { accept, done }
  chain: Promise.resolve(),   // requests run one after the other
  queued: 0,         // readings queued or running
  onSince: 0,        // Date.now() when the module was last seen switching on or off
};

function crc8(bytes) {
  let crc = 0;
  for (const byte of bytes) {
    crc ^= byte;
    for (let i = 0; i < 8; i++) crc = crc & 0x80 ? ((crc << 1) ^ 0xD5) & 0xFF : (crc << 1) & 0xFF;
  }
  return crc;
}

// One extended frame from the radio to the module, as hex for "crsf".
function elrsFrame(type, payload) {
  const body = [type, ELRS_MODULE, ELRS_RADIO, ...payload];
  return [ELRS_MODULE, body.length + 1, ...body, crc8(body)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

// Send a frame and wait for the first frame from the module that accept() takes; null after the last try.
function elrsRequest(type, payload, accept) {
  return new Promise((resolve) => {
    const attempt = (left) => {
      const timer = setTimeout(() => {
        elrs.waiting = null;
        if (left > 1) attempt(left - 1); else resolve(null);
      }, ELRS_TIMEOUT_MS);
      elrs.waiting = { accept, done: (frame) => { clearTimeout(timer); elrs.waiting = null; resolve(frame); } };
      send("crsf " + elrsFrame(type, payload));
    };
    attempt(ELRS_TRIES);
  });
}

// Run fn after every request already queued; fn is an async function.
function elrsQueue(fn) {
  elrs.queued++;
  elrs.chain = elrs.chain.then(fn).catch((error) => console.error(error)).finally(() => elrs.queued--);
  return elrs.chain;
}

// A line "rx aa bb ..." from the radio: one frame from the module, sync byte to CRC.
function elrsReceived(words) {
  const frame = Uint8Array.from(words, (word) => parseInt(word, 16));
  if (elrs.waiting && elrs.waiting.accept(frame)) elrs.waiting.done(frame);
}

// Zero-terminated UTF-8 string at offset; returns [text, offset after the zero].
function elrsString(data, offset) {
  let end = data.indexOf(0, offset);
  if (end < 0) end = data.length;
  return [new TextDecoder().decode(data.subarray(offset, end)), end + 1];
}

function parseEntry(index, data) {
  const type = data[1] & 0x7F;
  const [name, at] = elrsString(data, 2);
  const entry = { index, parent: data[0], type, hidden: (data[1] & 0x80) !== 0, name };
  if (type === ELRS_TYPE.SELECT) {
    const [options, next] = elrsString(data, at);
    entry.options = options.split(";");
    entry.value = data[next];
    entry.unit = elrsString(data, next + 4)[0].trim();
  } else if (type === ELRS_TYPE.UINT8) {
    [entry.value, entry.min, entry.max] = data.subarray(at, at + 3);
    entry.unit = elrsString(data, at + 4)[0].trim();
  } else if (type === ELRS_TYPE.INFO || type === ELRS_TYPE.STRING) {
    entry.text = elrsString(data, at)[0];
  } else if (type === ELRS_TYPE.COMMAND) {
    entry.status = data[at];
    entry.text = elrsString(data, at + 2)[0];
  }
  return entry;
}

// Read one entry, all its chunks; null if the module does not answer.
async function readEntry(index) {
  const parts = [];
  for (let chunk = 0; chunk < 16; chunk++) {
    const frame = await elrsRequest(0x2C, [index, chunk], (f) => f[2] === 0x2B && f[5] === index);
    if (!frame) return null;
    parts.push(...frame.subarray(7, frame.length - 1));
    if (frame[6] === 0) return parseEntry(index, Uint8Array.from(parts));
  }
  return null;
}

// Read the device info and every entry, then show them.
async function readModule() {
  if (!elrs.open || !elrs.moduleOn) return;
  const info = await elrsRequest(0x28, [], (f) => f[2] === 0x29);
  if (!info) { elrs.failed = true; showElrs(); return; }
  const [name, at] = elrsString(info, 5);
  elrs.device = { name, count: info[at + 12] };
  const entries = [];
  for (let index = 1; index <= elrs.device.count; index++) {
    const entry = await readEntry(index);
    if (!entry) { elrs.failed = true; showElrs(); return; }
    entries[index] = entry;
  }
  // Redrawing would close a list that is open for choosing; the next reading shows the news.
  const choosing = document.activeElement && document.activeElement.dataset && document.activeElement.dataset.elrs;
  if (choosing || (JSON.stringify(entries) === JSON.stringify(elrs.entries) && !elrs.failed)) return;
  elrs.entries = entries;
  elrs.failed = false;
  showElrs();
}

function elrsWrite(index, value) {
  send("crsf " + elrsFrame(0x2D, [index, value]));
}

// ---- View

const escapeHtml = (text) => text.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

// Label and group of an entry; an unknown entry goes to its folder's group, or to "Modul".
function elrsPlace(entry) {
  if (ELRS_LABELS[entry.name]) return ELRS_LABELS[entry.name];
  const folder = elrs.entries[entry.parent];
  return [entry.name, folder ? (ELRS_LABELS[folder.name] || [folder.name.replace(/ \(.*\)$/, "")])[0] : "Modul"];
}

function elrsTile(entry, label) {
  const name = escapeHtml(label);
  if (entry.type === ELRS_TYPE.SELECT || entry.type === ELRS_TYPE.UINT8) {
    // A unit in brackets, such as "(78bps)", describes the current value only.
    const note = entry.unit.startsWith("(");
    const unit = (value) => (!entry.unit || (note && value !== entry.value) ? "" : " " + entry.unit);
    const choices = entry.type === ELRS_TYPE.SELECT
      ? entry.options.map((text, i) => [i, text])
      : Array.from({ length: entry.max - entry.min + 1 }, (_, i) => [entry.min + i, String(entry.min + i)]);
    const options = choices.filter(([, text]) => text !== "").map(([value, text]) =>
      `<option value="${value}"${value === entry.value ? " selected" : ""}>${escapeHtml(text + unit(value))}</option>`).join("");
    return `<label class="tile"><span>${name}</span><select data-elrs="${entry.index}">${options}</select></label>`;
  }
  if (entry.type === ELRS_TYPE.COMMAND) {
    const confirm = entry.status === COMMAND.CONFIRM_NEEDED;
    const text = entry.text && entry.status !== COMMAND.READY ? `<span class="value">${escapeHtml(entry.text)}</span>` : "";
    return `<div class="tile"><span>${name}</span>${text}<button class="btn" data-command="${entry.index}" data-confirm="${confirm}">` +
           `${confirm ? "Bestätigen" : ELRS_BUTTONS[entry.name] || "Starten"}</button></div>`;
  }
  return `<div class="tile"><span>${name}</span><span class="value mono">${escapeHtml(entry.text || "")}</span></div>`;
}

// Next to the title: whether the radio sends to a receiver right now, from the mode in the state line.
function elrsHeader() {
  const mode = lastFields ? lastFields.mode : "";
  const shown = !$("elrsGroups").hidden;
  $("elrsState").textContent = !shown ? ""
    : mode === "elrs_active" ? "TX aktiv" : mode === "elrs_locked" ? "TX gesperrt" : "";
  // The same colours as the status LED in the drawing: blue while sending, orange while locked.
  $("elrsState").style.color = mode === "elrs_active" ? ACCENT : mode === "elrs_locked" ? WARN : "";
}

function showElrs() {
  const on = elrs.moduleOn;
  // The module answers about 1.8 s after it is switched on; silence before that is no fault.
  const silent = elrs.failed && Date.now() - elrs.onSince >= ELRS_START_MS;
  const shown = on && elrs.device !== null && elrs.entries.length > 0 && !silent;
  $("elrsGroups").hidden = !shown;
  $("elrsOff").hidden = shown;
  $("elrsNoteTitle").textContent = !on ? "Fernsteuerung ist aus" : silent ? "Modul antwortet nicht" : "Modul wird gelesen …";
  $("elrsNoteText").textContent = !on ? "Zum Einstellen die Fernsteuerung einschalten." : silent ? "Es wird weiter gefragt." : "";
  elrsHeader();
  if (!shown) return;

  const groups = new Map([...ELRS_COLUMNS.flat()].map((g) => [g, []]));
  // Within a group, values first and commands last.
  const ordered = elrs.entries.filter(Boolean).sort((a, b) =>
    (a.type === ELRS_TYPE.COMMAND) - (b.type === ELRS_TYPE.COMMAND) || a.index - b.index);
  for (const entry of ordered) {
    if (entry.hidden || entry.type === ELRS_TYPE.FOLDER) continue;
    // The version entry carries the version as its name and the commit as its text.
    if (entry.type === ELRS_TYPE.INFO && /^\d+\.\d+/.test(entry.name)) continue;
    const [label, group] = elrsPlace(entry);
    if (!groups.has(group)) groups.set(group, []);
    groups.get(group).push(elrsTile(entry, label));
  }
  // The receiver's link from the state line, above what the module lists.
  groups.get("Empfänger").unshift(
    `<div class="tile"><span>Verbunden</span><span class="value" id="elrsLinked">–</span></div>`,
    `<div class="tile"><span>Qualität</span><span class="value" id="elrsQuality">–</span></div>`);
  const version = elrs.entries.find((e) => e && e.type === ELRS_TYPE.INFO && /^\d+\.\d+/.test(e.name));
  groups.get("Modul").push(`<div class="tile"><span>Name</span><span class="value mono">${escapeHtml(elrs.device.name)}</span></div>`);
  if (version) groups.get("Modul").push(`<div class="tile"><span>Version</span><span class="value mono">${escapeHtml(version.name)}</span></div>`);

  const known = ELRS_COLUMNS.flat();
  const columns = [ELRS_COLUMNS[0].concat([...groups.keys()].filter((g) => !known.includes(g))), ELRS_COLUMNS[1]];
  $("elrsGroups").innerHTML = columns.map((names) => `<div class="stack">` + names.filter((g) => groups.get(g).length).map((g) =>
    `<section class="group"><h2><svg class="i" viewBox="0 0 24 24"><path d="${ELRS_ICONS[g] || ELRS_ICONS.Modul}"/></svg>${escapeHtml(g)}</h2>` +
    `<div class="tiles">${groups.get(g).join("")}</div></section>`).join("") + `</div>`).join("");
  if (lastFields) elrsState(lastFields);
}

// Called with every state line.
function elrsState(fields) {
  const on = fields.module === "1";
  if (on !== elrs.moduleOn) {
    elrs.moduleOn = on;
    elrs.onSince = Date.now();
    elrs.device = null;
    elrs.entries = [];
    elrs.failed = false;
    if (elrs.open) { showElrs(); elrsQueue(readModule); }
  }
  if (!elrs.open) return;
  elrsHeader();
  if (!$("elrsLinked")) return;
  $("elrsLinked").textContent = fields.lq === "0" ? "nein" : "ja";
  $("elrsQuality").textContent = fields.lq + " %";
}

// Open or leave the tab; the radio prints module frames only while it is open.
function elrsShow(open) {
  if (open === elrs.open) return;
  elrs.open = open;
  send(open ? "frames 1" : "frames 0");
  if (open) { showElrs(); elrsQueue(readModule); }
}

document.getElementById("elrsGroups").addEventListener("change", (event) => {
  const index = Number(event.target.dataset.elrs);
  if (!index) return;
  elrsWrite(index, Number(event.target.value));
  elrsQueue(readModule);
});
document.getElementById("elrsGroups").addEventListener("click", (event) => {
  const button = event.target.closest("[data-command]");
  if (!button) return;
  elrsWrite(Number(button.dataset.command), button.dataset.confirm === "true" ? COMMAND.CONFIRM : COMMAND.START);
  // The module reports progress in the entry; read again once it had time to start.
  setTimeout(() => elrsQueue(readModule), 500);
});
// Read the list again every 2 s: the receiver's bad/good count changes all the
// time, and a module that did not answer yet (about 1.8 s after switching on) is asked again.
setInterval(() => { if (elrs.open && elrs.moduleOn && elrs.queued === 0) elrsQueue(readModule); }, 2000);
