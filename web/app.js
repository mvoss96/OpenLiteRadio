// OpenLiteRadio configurator. The page talks to the radio through the Web
// Serial API (Chrome, Edge). Both sides send one text line per message; the
// protocol is described in docs/ARCHITECTURE.md.
//
// The radio reports its settings (key, type, range, default, value) after
// "list" and its profiles after "profiles". This page adds labels and layout.
// The radio stores, per channel, which control it follows (ch.N.source); the
// assignment view shows the same thing from the control's side.
//
// Every change is stored at once: settings with "save", a change of the
// assignment with "profile save" when an own profile is active.

const USB_VENDOR_ID = 0x2fe3;
const CHANNEL_COUNT = 16;
const PROFILE_SLOTS = 4;
const NAME_BYTES = 15;
const BATTERY_THRESHOLDS = [3232, 3298, 3391, 3552];   // stock firmware: 1, 2, 3, 4 LEDs
const ACCENT = "#5aa2ff", LEVER = "#4f535a", CAP = "#6b6f76", SURFACE = "#17181b", HAIR = "#2b2e34",
      STROKE = "#3a3e45", FG1 = "#f2f3f5", FG3 = "#8b9099", WARN = "#f2a655", RAISED = "#202227";

// One entry per control. source is the value of ch.N.source in the firmware
// (core::Source). side, y and target place the label in the drawing.
// axis is the stick axis (cal.N.*) of a self-centring stick, which has a dead zone.
// stick: axis, self-centring, raw value falls towards the high end.
// positions: words of a switch's positions, in the order of its state value.
const CONTROLS = [
  { source: 6, name: "SB", hint: "Hebel links", side: "L", y: 58, target: [213, 64], positions: ["oben", "Mitte", "unten"] },
  { source: 9, name: "SE", hint: "Taste links", side: "L", y: 112, target: [166, 72], positions: ["losgelassen", "gedrückt"] },
  { source: 5, name: "SA", hint: "Wippe links", side: "L", y: 166, target: [114, 103], positions: ["unten", "oben"] },
  { source: 3, name: "Throttle", hint: "Linker Knüppel, senkrecht", side: "L", y: 232, target: [110, 196], stick: [0, false, false] },
  { source: 4, name: "Yaw", hint: "Linker Knüppel, waagerecht", side: "L", y: 286, target: [110, 204], axis: 1, stick: [1, true, true] },
  { source: 7, name: "SC", hint: "Hebel rechts", side: "R", y: 58, target: [427, 64], positions: ["oben", "Mitte", "unten"] },
  { source: 10, name: "SF", hint: "Taste rechts", side: "R", y: 112, target: [474, 72], positions: ["losgelassen", "gedrückt"] },
  { source: 8, name: "SD", hint: "Wippe rechts", side: "R", y: 166, target: [526, 103], positions: ["unten", "oben"] },
  { source: 2, name: "Pitch", hint: "Rechter Knüppel, senkrecht", side: "R", y: 232, target: [530, 196], axis: 2, stick: [2, true, true] },
  { source: 1, name: "Roll", hint: "Rechter Knüppel, waagerecht", side: "R", y: 286, target: [530, 204], axis: 3, stick: [3, true, false] },
];
const STATE_KEY = { 5: "sa", 6: "sb", 7: "sc", 8: "sd", 9: "se", 10: "sf" };
const LIST_ORDER = [3, 4, 2, 1, 5, 6, 7, 8, 9, 10];   // list that replaces the labels in a narrow window

// Templates are written with "set"; the radio does not store them.
const TEMPLATES = [
  { name: "Betaflight-Standard (AETR)", sources: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 0, 0, 0, 0, 0] },
  { name: "TAER", sources: [3, 1, 2, 4, 5, 6, 7, 8, 9, 10, 0, 0, 0, 0, 0, 0] },
];

const $ = (id) => document.getElementById(id);
const ICON = {
  check: "m5 12 5 5 9-10", pen: "M4 20h4L19 9l-4-4L4 16v4z", plus: "M12 5v14M5 12h14",
  delete: "M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13",
};
const icon = (name) => `<svg class="i" viewBox="0 0 24 24"><path d="${ICON[name]}"/></svg>`;
const escape = (text) => text.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

let writer = null;
let reader = null;
let linesThisSecond = 0;
const settings = new Map();        // key -> { type, min, max, def, value }
let lastFields = null;             // fields of the last state line
let lastChannels = [];
let selected = 0;                  // source of the control open in the detail card, 0 = none
let advancedOpen = false;
let asking = 0;                    // channel the open control wants while the question is shown
let profiles = [];                 // { slot, name } of the used slots
let incomingProfiles = [];
let activeProfile = 0;
let menu = null;                   // null closed; "list"; { edit: slot }; { create: slot }; { remove: slot }
let calibrating = false;
let calStage = 1;                  // 1: move the sticks, 2: release them
let seen = null;                   // per stick [minX, maxX, minY, maxY] as fractions, during a run

const setting = (key) => (settings.has(key) ? settings.get(key).value : undefined);
const channelsOf = (source) =>
  [...Array(CHANNEL_COUNT).keys()].map((i) => i + 1).filter((n) => setting(`ch.${n}.source`) === source);
const controlOf = (source) => CONTROLS.find((control) => control.source === source);

// ---- Storing changes

async function send(text) {
  if (writer) await writer.write(new TextEncoder().encode(text + "\n"));
}

// Store the assignment: into the active own profile, or as the plain settings.
function storeAssignment() {
  const own = profiles.find((p) => p.slot === activeProfile);
  send(own ? `profile save ${own.slot} ${own.name}` : "save");
}

// ---- Profile selector and its menu

function currentTemplate() {
  return TEMPLATES.find((t) => t.sources.every((source, i) =>
    setting(`ch.${i + 1}.source`) === source && setting(`ch.${i + 1}.weight`) === 100 && setting(`ch.${i + 1}.reverse`) === 0));
}

function profileTitle() {
  const own = profiles.find((p) => p.slot === activeProfile);
  if (own) return own.name;
  const template = settings.size ? currentTemplate() : null;
  return template ? template.name : "Ohne Profil";
}

const nameBytes = (text) => new TextEncoder().encode(text).length;
const nameValid = (text) => text.trim().length > 0 && nameBytes(text.trim()) <= NAME_BYTES;

function freeSlot() {
  for (let slot = 1; slot <= PROFILE_SLOTS; slot++) if (!profiles.some((p) => p.slot === slot)) return slot;
  return 0;
}

function showProfiles() {
  $("profileButton").textContent = profileTitle();
  $("profileButton").setAttribute("aria-expanded", menu !== null);
  $("profileMenu").hidden = menu === null;
  if (menu === null) return;

  const editing = menu.edit || menu.create;
  const editRow = (value) => `<div class="medit"><input class="field" id="menuName" value="${escape(value)}" aria-label="Name"></div>`;
  let rows = "";
  if (profiles.length || menu.create) rows += `<div class="mhead">Eigene Profile</div>`;
  for (const p of profiles) {
    if (menu.edit === p.slot) { rows += editRow(p.name); continue; }
    const active = p.slot === activeProfile;
    // While a name is edited the other rows stay as they are but do nothing.
    rows += `<div class="mitem${active ? " sel" : ""}" data-act="${editing ? "" : "load"}" data-slot="${p.slot}"><span>${escape(p.name)}</span>` +
            (active ? `<span class="slot check">${icon("check")}</span>` : "") +
            `<button class="icon" data-act="${editing ? "" : "edit"}" data-slot="${p.slot}" aria-label="${escape(p.name)} umbenennen">${icon("pen")}</button></div>`;
  }
  if (menu.create) rows += editRow(`Profil ${menu.create}`);
  rows += `<div class="mhead">Vorlagen</div>`;
  TEMPLATES.forEach((t, i) => {
    rows += `<div class="mitem" data-act="${editing ? "" : "template"}" data-index="${i}"><span>${t.name}</span></div>`;
  });

  let footer = "";
  if (editing) {
    const left = menu.edit ? `<button class="icon" data-act="remove" aria-label="Profil löschen">${icon("delete")}</button>` : "";
    footer = `<div class="actions">${left}<span></span><button class="btn" data-act="cancel">Abbrechen</button>` +
             `<button class="btn primary" data-act="${menu.edit ? "rename" : "create"}" id="menuSave">${menu.edit ? "Speichern" : "Anlegen"}</button></div>`;
  } else if (menu.remove) {
    const p = profiles.find((item) => item.slot === menu.remove);
    footer = `<div class="actions"><span>${escape(p.name)} löschen?</span><button class="btn" data-act="cancel">Abbrechen</button>` +
             `<button class="btn primary" data-act="delete">Löschen</button></div>`;
  } else {
    const slot = freeSlot();
    rows += `<div class="mgap"></div><div class="mitem${slot ? "" : " off"}" data-act="new"><span>Neues Profil</span>` +
            `<span class="sec">${profiles.length} von ${PROFILE_SLOTS}</span><span class="slot">${icon("plus")}</span></div>`;
  }
  $("profileMenu").innerHTML = rows + footer;

  const field = $("menuName");
  if (field) {
    field.focus();
    field.select();
    const check = () => { $("menuSave").disabled = !nameValid(field.value); };
    field.addEventListener("input", check);
    check();
  }
}

function applyTemplate(template) {
  template.sources.forEach((source, i) => {
    const n = i + 1;
    if (setting(`ch.${n}.source`) !== source) send(`set ch.${n}.source ${source}`);
    if (setting(`ch.${n}.weight`) !== 100) send(`set ch.${n}.weight 100`);
    if (setting(`ch.${n}.reverse`) !== 0) send(`set ch.${n}.reverse 0`);
  });
  send("save");
  send("profile none");
}

function menuAction(act, target) {
  if (menu === null) return;
  const slot = Number(target.dataset.slot || 0);
  const name = $("menuName") ? $("menuName").value.trim() : "";
  if (act === "load" && !(menu.edit || menu.create)) {
    if (slot !== activeProfile) send(`profile load ${slot}`);
    menu = null;
  } else if (act === "template" && !(menu.edit || menu.create)) {
    applyTemplate(TEMPLATES[Number(target.dataset.index)]);
    menu = null;
  } else if (act === "edit") {
    menu = { edit: slot };
  } else if (act === "new" && freeSlot()) {
    menu = { create: freeSlot() };
  } else if (act === "cancel") {
    menu = "list";
  } else if (act === "rename" && nameValid(name)) {
    send(`profile name ${menu.edit} ${name}`);
    menu = "list";
  } else if (act === "create" && nameValid(name)) {
    send(`profile save ${menu.create} ${name}`);
    menu = null;
  } else if (act === "remove") {
    menu = { remove: menu.edit };
  } else if (act === "delete") {
    send(`profile delete ${menu.remove}`);
    menu = "list";
  }
  showProfiles();
}

// ---- Labels in the drawing, and the list for narrow windows

function buildLabels() {
  $("callouts").innerHTML = CONTROLS.map((c) => {
    const [x0, x1] = c.side === "L" ? [-350, -90] : [730, 990];
    const near = c.side === "L" ? x1 : x0;
    return `<g class="callout" tabindex="0" role="button" data-source="${c.source}" aria-label="${c.name} einstellen">
      <rect class="hit" x="${x0 - 14}" y="${c.y - 34}" width="${x1 - x0 + 28}" height="46" rx="10"></rect>
      <text id="coName${c.source}" x="${x0}" y="${c.y - 10}" font-size="17" font-weight="600" fill="${FG1}">${c.name}</text>
      <text id="coChannel${c.source}" x="${x0 + 14 + 10 * c.name.length}" y="${c.y - 10}" font-size="14" fill="${FG3}"></text>
      <text id="coValue${c.source}" x="${x1}" y="${c.y - 10}" text-anchor="end" font-size="16" fill="${FG1}"></text>
      <path d="M ${x0} ${c.y} H ${x1}" stroke="${HAIR}" stroke-width="3" stroke-linecap="round"></path>
      <path id="coMeter${c.source}" data-x0="${x0}" data-x1="${x1}" data-y="${c.y}" d="" stroke="${ACCENT}" stroke-width="3" stroke-linecap="round"></path>
      <path d="M ${near} ${c.y} L ${c.target[0]} ${c.target[1]}" fill="none" stroke="${LEVER}"></path>
      <circle cx="${c.target[0]}" cy="${c.target[1]}" r="3.5" fill="${FG3}"></circle>
    </g>`;
  }).join("");
  $("list").innerHTML = LIST_ORDER.map((source) => {
    const c = controlOf(source);
    return `<div class="lrow" data-source="${source}"><b>${c.name}</b><span id="liChannel${source}"></span><b id="liValue${source}"></b></div>`;
  }).join("");
}

function channelText(source) {
  const channels = channelsOf(source);
  return channels.length ? "Kanal " + channels.join(", ") : "kein Kanal";
}

// Live value of each control: the output of the first channel that follows it.
function showLabels() {
  for (const c of CONTROLS) {
    const text = channelText(c.source);
    $("coChannel" + c.source).textContent = text;
    $("liChannel" + c.source).textContent = text;
    const channel = channelsOf(c.source)[0];
    const value = channel ? lastChannels[channel - 1] : undefined;
    const part = value === undefined ? 0 : Math.max(0, Math.min(1, (value - 988) / 1024));
    $("coValue" + c.source).textContent = value === undefined ? "–" : value;
    $("liValue" + c.source).textContent = value === undefined ? "–" : value;
    const meter = $("coMeter" + c.source);
    const x0 = Number(meter.dataset.x0);
    meter.setAttribute("d", part > 0 ? `M ${x0} ${meter.dataset.y} H ${x0 + (Number(meter.dataset.x1) - x0) * part}` : "");
  }
}

// ---- Detail card of one control

function openDetail(source) {
  selected = source;
  asking = 0;
  for (const element of document.querySelectorAll("[data-source]")) {
    element.classList.toggle("selected", Number(element.dataset.source) === source);
  }
  showDetail();
}

// Weight and reverse of the open control: those of its first channel.
function channelShape() {
  const channel = channelsOf(selected)[0];
  return channel ? { weight: setting(`ch.${channel}.weight`), reverse: setting(`ch.${channel}.reverse`) !== 0 }
                 : { weight: 100, reverse: false };
}

const output = (raw, shape) => Math.round(1500 + (shape.reverse ? -1 : 1) * (raw - 1500) * shape.weight / 100);

function showDetail() {
  const control = controlOf(selected);
  const open = Boolean(control) && settings.size > 0;
  $("detail").hidden = !open;
  document.body.classList.toggle("has-detail", open);
  if (!open) return;

  $("dName").textContent = control.name;
  $("dHint").textContent = control.hint;

  // Chips: while asking, they preview the swap.
  const names = {};
  for (let n = 1; n <= CHANNEL_COUNT; n++) names[n] = controlOf(setting(`ch.${n}.source`));
  const own = channelsOf(selected);
  let target = 0;
  if (asking) {
    const occupant = names[asking];
    if (own[0]) { names[own[0]] = occupant; target = own[0]; }
    names[asking] = control;
  }
  let chips = "";
  for (let n = 1; n <= CHANNEL_COUNT; n++) {
    const c = names[n];
    const state = c === control ? "own" : n === target ? "taken target" : c ? "taken" : "free";
    chips += `<button class="chip ${state}" data-channel="${n}">${n}<small>${c ? c.name : "frei"}</small></button>`;
  }
  $("dChips").innerHTML = chips;

  const shape = channelShape();
  let middle = "";
  if (control.axis) {
    const dead = setting(`cal.${control.axis}.deadzone`);
    middle = `<div class="cap">Totzone</div><div class="big" id="dDeadOut">${dead} %</div>` +
             `<input type="range" id="dDead" min="0" max="10" value="${dead}" aria-label="Totzone">`;
  } else if (control.positions) {
    const now = lastFields ? Number(lastFields[STATE_KEY[control.source]]) : -1;
    const raw = control.positions.length === 3 ? [988, 1500, 2012] : [988, 2012];
    middle = `<div class="cap">Stellungen</div><div class="positions">` + control.positions.map((word, i) =>
      `<div class="mitem${i === now ? " sel" : ""}"><span>${word}</span><span class="sec">${output(raw[i], shape)}</span></div>`).join("") + `</div>`;
  }
  $("dMiddle").innerHTML = middle;
  if ($("dDead")) fill($("dDead"));

  if (document.activeElement !== $("dWeight")) $("dWeight").value = shape.weight;
  $("dWeightOut").textContent = $("dWeight").value + " %";
  fill($("dWeight"));
  $("dReverse").checked = shape.reverse;
  $("dWeight").disabled = $("dReverse").disabled = own.length === 0;

  $("dToggle").hidden = asking !== 0;
  $("dToggle").classList.toggle("open", advancedOpen);
  $("dAdvanced").setAttribute("aria-expanded", advancedOpen);
  $("dAdvancedRow").hidden = !advancedOpen;
  // Open, the toggle heads the section; closed, it sits at the bottom.
  $("detail").insertBefore($("dToggle"), advancedOpen ? $("dAdvancedRow") : $("dAsk"));
  $("dAsk").hidden = asking === 0;
  if (asking) {
    const occupant = controlOf(setting(`ch.${asking}.source`));
    $("dAskText").innerHTML = `Kanal ${asking} ist belegt von <b>${occupant.name}</b>`;
    $("askSwap").hidden = own.length === 0;
  }
  drawCurve();
}

// Filled part of a slider.
function fill(input) {
  input.style.setProperty("--fill", (input.value - input.min) / (input.max - input.min) * 100 + "%");
}

// Stick position of the open control in percent, -100 to 100, from the raw reading.
function stickPercent(control) {
  const [axis, centred, inverted] = control.stick;
  const raw = lastFields ? Number(lastFields.sticks.split(",")[axis]) : undefined;
  const low = setting(`cal.${axis}.low`), centre = setting(`cal.${axis}.centre`), high = setting(`cal.${axis}.high`);
  if (raw === undefined || low === undefined) return 0;
  let percent;
  if (!centred) percent = ((raw - low) / (high - low)) * 200 - 100;
  else if (raw >= centre) percent = ((raw - centre) / (high - centre)) * 100;
  else percent = -((centre - raw) / (centre - low)) * 100;
  percent = Math.max(-100, Math.min(100, percent));
  return inverted ? -percent : percent;
}

// Response of the open control: no axes and no labels; dead band, curve and live dot.
function drawCurve() {
  const control = controlOf(selected);
  if (!control) return;
  const W = 340, H = 200, pad = 16;
  const shape = { weight: Number($("dWeight").value), reverse: $("dReverse").checked };
  const Y = (v) => pad + (1 - (v - 988) / 1024) * (H - 2 * pad);
  let grid = "";
  for (const x of [W / 4, 3 * W / 4]) grid += `<path d="M ${x} 0 V ${H}" stroke="${FG1}" stroke-opacity=".05"></path>`;
  for (const y of [H / 4, 3 * H / 4]) grid += `<path d="M 0 ${y} H ${W}" stroke="${FG1}" stroke-opacity=".05"></path>`;
  grid += `<path d="M 0 ${H / 2} H ${W} M ${W / 2} 0 V ${H}" stroke="${FG1}" stroke-opacity=".16"></path>`;
  const dot = (x, y) => `<circle cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" r="7" fill="#fff" stroke="${RAISED}" stroke-width="2"></circle>`;
  let inner = "";
  if (control.stick) {
    const dead = control.axis ? Number($("dDead") ? $("dDead").value : 0) : 0;
    const X = (p) => pad + (p + 100) / 200 * (W - 2 * pad);
    const out = (p) => output(Math.abs(p) <= dead ? 1500 : 1500 + 512 * p / 100, shape);
    const points = dead ? [-100, -dead, dead, 100] : [-100, 100];
    const line = points.map((p, i) => `${i ? "L" : "M"} ${X(p).toFixed(1)} ${Y(out(p)).toFixed(1)}`).join(" ");
    if (dead) inner += `<rect x="${X(-dead)}" y="0" width="${X(dead) - X(-dead)}" height="${H}" fill="${ACCENT}" fill-opacity=".14"></rect>`;
    inner += `<path d="${line}" fill="none" stroke="${ACCENT}" stroke-width="3" stroke-linejoin="round" stroke-linecap="round"></path>`;
    const live = stickPercent(control);
    inner += dot(X(live), Y(out(live)));
  } else {
    const raw = control.positions.length === 3 ? [988, 1500, 2012] : [988, 2012];
    const seg = (W - 2 * pad) / raw.length;
    raw.forEach((value, i) => {
      inner += `<path d="M ${pad + i * seg + 8} ${Y(output(value, shape))} H ${pad + (i + 1) * seg - 8}" stroke="${ACCENT}" stroke-width="3" stroke-linecap="round"></path>`;
    });
    const now = lastFields ? Number(lastFields[STATE_KEY[control.source]]) : 0;
    inner += dot(pad + (now + 0.5) * seg, Y(output(raw[now] ?? 1500, shape)));
  }
  $("dCurve").innerHTML = `<rect width="${W}" height="${H}" rx="12" fill="${RAISED}"></rect>${grid}${inner}`;
}

// Put the open control on a channel. Weight and reverse move with it.
// swap: the control on that channel takes over the open control's channel.
function moveTo(channel, swap) {
  const own = channelsOf(selected);
  const from = own[0];
  const shapeOf = (n) => n ? [setting(`ch.${n}.weight`), setting(`ch.${n}.reverse`)] : [100, 0];
  const [weight, reverse] = shapeOf(from);
  if (swap && from) {
    const occupant = setting(`ch.${channel}.source`);
    const [otherWeight, otherReverse] = shapeOf(channel);
    send(`set ch.${from}.source ${occupant}`);
    send(`set ch.${from}.weight ${otherWeight}`);
    send(`set ch.${from}.reverse ${otherReverse}`);
  } else if (from) {
    send(`set ch.${from}.source 0`);
    send(`set ch.${from}.weight 100`);
    send(`set ch.${from}.reverse 0`);
  }
  for (const other of own.slice(1)) send(`set ch.${other}.source 0`);
  send(`set ch.${channel}.source ${selected}`);
  send(`set ch.${channel}.weight ${weight}`);
  send(`set ch.${channel}.reverse ${reverse}`);
  storeAssignment();
  asking = 0;
  showDetail();
}

// ---- Live state

function setFill(id, colour) { $(id).setAttribute("fill", colour); }

const TRIM_PARTS = [["Up", 1], ["Down", 2], ["Left", 4], ["Right", 8], ["Centre", 16]];
function showTrim(prefix, bits) {
  for (const [part, bit] of TRIM_PARTS) setFill(prefix + part, bits & bit ? ACCENT : STROKE);
}

// Stick position as fractions: x from 0 (left) to 1 (right), y from 0 (top) to 1 (bottom).
function stickFractions(sticks) {
  return [[1 - sticks[1] / 4095, 1 - sticks[0] / 4095], [sticks[3] / 4095, sticks[2] / 4095]];
}

function showState(fields) {
  lastFields = fields;
  const sticks = fields.sticks.split(",").map(Number);
  const fractions = stickFractions(sticks);
  [["capLeft", 173], ["capRight", 467]].forEach(([id, centre], index) => {
    const [x, y] = fractions[index];
    $(id).setAttribute("cx", centre + (x - 0.5) * 64);
    $(id).setAttribute("cy", 200 + (y - 0.5) * 44);
    setFill(id, Math.abs(x - 0.5) > 0.06 || Math.abs(y - 0.5) > 0.06 ? ACCENT : CAP);
  });
  $("rockSA").setAttribute("stroke", ACCENT);
  $("rockSD").setAttribute("stroke", ACCENT);
  $("rockSA").setAttribute("d", fields.sa === "1" ? "M 130 92.2 L 150 80" : "M 110 104.5 L 130 92.2");
  $("rockSD").setAttribute("d", fields.sd === "1" ? "M 510 92.2 L 490 80" : "M 530 104.5 L 510 92.2");
  for (const [id, key] of [["knobSB", "sb"], ["knobSC", "sc"]]) {
    $(id).setAttribute("cy", 95 + Number(fields[key]) * 9);
    setFill(id, ACCENT);
    setFill(id.replace("knob", "lever"), ACCENT);
  }
  setFill("keySE", fields.se === "1" ? ACCENT : LEVER);
  setFill("keySF", fields.sf === "1" ? ACCENT : LEVER);
  setFill("keyPower", fields.power === "1" ? ACCENT : SURFACE);
  showTrim("trimL", Number(fields.tl));
  showTrim("trimR", Number(fields.tr));
  for (const [key, button, text] of [["bind", "keyBind", "txtBind"], ["setup", "keySetup", "txtSetup"]]) {
    const pressed = fields[key] === "1";
    setFill(button, pressed ? ACCENT : HAIR);
    setFill(text, pressed ? ACCENT : FG3);
  }
  for (const source of [9, 10]) {
    setFill("coName" + source, fields[STATE_KEY[source]] === "1" ? ACCENT : FG1);
  }

  const moduleOn = fields.module === "1";
  const locked = fields.lock === "1";
  const battery = Number(fields.battery);
  const level = BATTERY_THRESHOLDS.filter((threshold) => battery >= threshold).length;
  document.querySelectorAll(".batLed").forEach((led, index) => led.setAttribute("fill", index < level ? FG1 : HAIR));
  setFill("statusLed", fields.calib === "1" ? "#f2d24b" : moduleOn && locked ? WARN : fields.on === "1" ? ACCENT : HAIR);

  $("lockNote").hidden = !(moduleOn && locked);
  $("lockNote").textContent = setting("unlock.mode") === 2 ? "Gesperrt: Gas hoch, dann runter" : "Gesperrt: Gas nach unten";
  $("stModule").textContent = !moduleOn ? "aus" : locked ? "läuft" : "sendet";
  $("stRate").textContent = Math.round(1e6 / Number(fields.period)) + " Hz";
  $("stLink").textContent = fields.lq === "0" ? "nicht verbunden" : `${fields.lq} %, -${fields.rssi} dBm`;
  $("stBattery").textContent = `${level} von 4`;
  $("sysBattery").textContent = battery;
  $("sysUsb").textContent = fields.usb === "1" ? "vorhanden" : "fehlt";

  lastChannels = fields.ch.split(",").map(Number);
  showLabels();
  if (selected) {
    // Only the live parts; a full redraw would reset a slider being dragged.
    const control = controlOf(selected);
    if (control.positions) {
      const now = Number(fields[STATE_KEY[control.source]]);
      document.querySelectorAll(".positions .mitem").forEach((row, i) => row.classList.toggle("sel", i === now));
    }
    drawCurve();
  }
  showCalibration(fractions, fields.calib === "1");
}

// Grey drawing without live values, as shown before connecting.
function resetDrawing() {
  for (const id of ["knobSB", "knobSC", "leverSB", "leverSC", "keySE", "keySF"]) setFill(id, LEVER);
  for (const id of ["knobSB", "knobSC"]) $(id).setAttribute("cy", 104);
  setFill("keyPower", SURFACE);
  showTrim("trimL", 0);
  showTrim("trimR", 0);
  for (const id of ["keyBind", "keySetup"]) setFill(id, HAIR);
  for (const id of ["txtBind", "txtSetup"]) setFill(id, FG3);
  for (const id of ["capLeft", "capRight"]) setFill(id, CAP);
  $("capLeft").setAttribute("cx", 173); $("capLeft").setAttribute("cy", 200);
  $("capRight").setAttribute("cx", 467); $("capRight").setAttribute("cy", 200);
  for (const id of ["rockSA", "rockSD"]) $(id).setAttribute("stroke", LEVER);
  setFill("statusLed", HAIR);
  document.querySelectorAll(".batLed").forEach((led) => led.setAttribute("fill", HAIR));
  lastFields = null;
  lastChannels = [];
  showLabels();
}

// ---- Calibration run

// Which end of a stick has not been reached yet, or "" when all four were.
function missingEnd([minX, maxX, minY, maxY]) {
  if (minX > 0.2) return "links fehlt";
  if (maxX < 0.8) return "rechts fehlt";
  if (minY > 0.2) return "oben fehlt";
  if (maxY < 0.8) return "unten fehlt";
  return "";
}

function showCalStage() {
  $("calStep").textContent = `Schritt ${calStage} von 2`;
  $("calTitle").textContent = calStage === 1 ? "In alle Ecken bewegen" : "Rechts loslassen, Gas ganz nach unten";
  $("calNext").textContent = calStage === 1 ? "Weiter" : "Fertig";
  for (const pad of ["padLeft", "padRight"]) $(pad).querySelector(".seen").hidden = calStage === 2;
}

// The run can start here or with SETUP on the radio; either way the state line says so.
function showCalibration(fractions, running) {
  if (running && !calibrating) {
    seen = fractions.map(([x, y]) => [x, x, y, y]);
    calStage = 1;
    showCalStage();
    showView("calibration");
  }
  if (!running && calibrating) {
    send("list");
    showView("assign");
  }
  calibrating = running;
  document.body.classList.toggle("calibrating", running);
  if (!running) return;

  let complete = true;
  [["padLeft", "padLeftNote"], ["padRight", "padRightNote"]].forEach(([padId, noteId], index) => {
    const [x, y] = fractions[index];
    const range = seen[index];
    range[0] = Math.min(range[0], x); range[1] = Math.max(range[1], x);
    range[2] = Math.min(range[2], y); range[3] = Math.max(range[3], y);
    const pad = $(padId);
    Object.assign(pad.querySelector(".dot").style, { left: x * 100 + "%", top: y * 100 + "%" });
    Object.assign(pad.querySelector(".seen").style, {
      left: range[0] * 100 + "%", top: range[2] * 100 + "%",
      width: (range[1] - range[0]) * 100 + "%", height: (range[3] - range[2]) * 100 + "%",
    });
    const note = $(noteId);
    if (calStage === 1) {
      const missing = missingEnd(range);
      complete = complete && !missing;
      note.textContent = missing || "fertig";
      note.className = missing ? "missing" : "ok";
    } else {
      const centred = Math.abs(x - 0.5) < 0.1 && Math.abs(y - 0.5) < 0.1;
      note.textContent = index === 0 ? (y > 0.85 ? "Gas unten" : "Gas nach unten") : centred ? "Mitte" : "loslassen";
      note.className = "";
    }
  });
  $("calNext").disabled = calStage === 1 && !complete;
}

// ---- Settings and system views

// Put a setting's value into every element bound to it with data-key.
function showSetting(key) {
  const value = settings.get(key).value;
  for (const element of document.querySelectorAll(`[data-key="${key}"]`)) {
    if (element.type === "checkbox") element.checked = value !== 0;
    else if (document.activeElement !== element) element.value = value;
  }
}

function buildRawTable() {
  $("rawRows").innerHTML = [...settings].map(([key, s]) =>
    `<div class="rawrow"><b>${key}</b><span><input type="number" min="${s.min}" max="${s.max}" data-key="${key}" aria-label="${key}"></span>` +
    `<span>${s.min}–${s.max}</span><span>${s.def}</span></div>`).join("");
  for (const key of settings.keys()) showSetting(key);
}

function showMessage(text) {
  $("message").textContent = text;
  $("message").hidden = false;
  clearTimeout(showMessage.timer);
  showMessage.timer = setTimeout(() => { $("message").hidden = true; }, 8000);
}

// ---- Lines from the radio

function handleLine(line) {
  const [kind, ...rest] = line.trim().split(" ");
  if (kind === "state") {
    showState(Object.fromEntries(rest.map((pair) => pair.split("="))));
    linesThisSecond++;
  } else if (kind === "hello") {
    $("deviceText").textContent = `LiteRadio 4 · Firmware ${rest[1]}`;
    $("sysFirmware").textContent = rest[1];
  } else if (kind === "setting") {
    const [key, type, min, max, def, value] = rest;
    settings.set(key, { type, min: Number(min), max: Number(max), def: Number(def), value: Number(value) });
  } else if (kind === "end") {
    buildRawTable();
    showLabels();
    showDetail();
    showProfiles();
  } else if (kind === "profile") {
    incomingProfiles.push({ slot: Number(rest[0]), name: rest.slice(1).join(" ") });
  } else if (kind === "active") {
    profiles = incomingProfiles;
    incomingProfiles = [];
    activeProfile = Number(rest[0]);
    showProfiles();
  } else if (kind === "ok") {
    handleOk(rest);
  } else if (kind === "error") {
    const reason = rest.join(" ");
    showMessage(reason === "calibration rejected"
      ? "Kalibrierung verworfen: Ein Knüppel war nicht an allen Enden oder am Schluss nicht losgelassen."
      : "Die Fernsteuerung hat abgelehnt: " + reason);
    send("list");
  }
}

function handleOk(words) {
  if (words[0] === "profile") {
    // A loaded profile changed the ch.N.* settings; every profile command may change the list.
    if (words[1] === "load") send("list");
    send("profiles");
  } else if (words[0] === "defaults") {
    send("save");
    send("profile none");
    send("list");
  } else if (words[0] === "calibrate" && words[1] === "end") {
    showMessage("Kalibrierung übernommen und gespeichert.");
  } else if (settings.has(words[0])) {
    settings.get(words[0]).value = Number(words[1]);
    showSetting(words[0]);
    if (words[0].startsWith("ch.") || words[0].startsWith("cal.")) {
      showLabels();
      if (document.activeElement !== $("dWeight") && document.activeElement !== $("dDead")) showDetail();
      showProfiles();
    }
  }
}

// ---- Connection

function setConnected(connected) {
  document.body.classList.toggle("offline", !connected);
  if (!connected) {
    resetDrawing();
    settings.clear();
    profiles = [];
    activeProfile = 0;
    menu = null;
    openDetail(0);
    showView("assign");
  }
}

async function connect() {
  if (!("serial" in navigator)) {
    showMessage("Dieser Browser kann keine seriellen Geräte ansprechen. Bitte Chrome oder Edge verwenden.");
    return;
  }
  const port = await navigator.serial.requestPort({ filters: [{ usbVendorId: USB_VENDOR_ID }] });
  await port.open({ baudRate: 115200 });
  writer = port.writable.getWriter();
  reader = port.readable.getReader();
  setConnected(true);
  await send("hello");
  await send("stream 1");
  await send("list");
  await send("profiles");

  const decoder = new TextDecoder();
  let pending = "";
  try {
    while (true) {
      // Disconnect cancels the reader, which ends this loop with done set.
      const { value, done } = await reader.read();
      if (done) break;
      pending += decoder.decode(value, { stream: true });
      const lines = pending.split("\n");
      pending = lines.pop();
      lines.forEach(handleLine);
    }
  } catch (error) {
    showMessage("Verbindung verloren: " + error.message);
  }
  // Closing the port frees it for other programs such as tools/flash.py.
  reader.releaseLock();
  writer.releaseLock();
  writer = null;
  await port.close().catch(() => {});
  setConnected(false);
}

// ---- Views and events

function showView(name) {
  for (const view of document.querySelectorAll(".view")) view.hidden = view.id !== "view-" + name;
  for (const button of $("nav").children) button.classList.toggle("active", button.dataset.view === name);
  $("statusbar").hidden = name !== "assign";
}

// The drawing alone when there is no room for the labels beside it.
const narrow = window.matchMedia("(max-width: 1000px)");
function fitStage() {
  $("stageSvg").setAttribute("viewBox", narrow.matches ? "0 44 640 440" : "-360 20 1360 470");
}

buildLabels();
fitStage();
narrow.addEventListener("change", fitStage);
setConnected(false);
showProfiles();

$("connectBig").addEventListener("click", () => connect().catch((error) => showMessage(error.message)));
$("connect").addEventListener("click", () => { if (writer) reader.cancel(); });
$("nav").addEventListener("click", (event) => {
  if (event.target.dataset.view && !calibrating) showView(event.target.dataset.view);
});

$("profileButton").addEventListener("click", () => { menu = menu === null ? "list" : null; showProfiles(); });
$("profileMenu").addEventListener("click", (event) => {
  const target = event.target.closest("[data-act]");
  if (target) { event.stopPropagation(); menuAction(target.dataset.act, target); }
});
$("profileMenu").addEventListener("keydown", (event) => {
  if (event.key === "Enter" && event.target.id === "menuName") menuAction(menu.edit ? "rename" : "create", event.target);
});
document.addEventListener("click", (event) => {
  if (menu !== null && !event.target.closest(".pop")) { menu = null; showProfiles(); }
  const control = event.target.closest("[data-source]");
  if (control) openDetail(Number(control.dataset.source));
});
document.addEventListener("keydown", (event) => {
  const label = event.target.closest && event.target.closest(".callout");
  if (label && (event.key === "Enter" || event.key === " ")) {
    event.preventDefault();
    openDetail(Number(label.dataset.source));
  }
  if (event.key === "Escape") {
    if (menu !== null) { menu = null; showProfiles(); } else openDetail(0);
  }
});

$("dClose").addEventListener("click", () => openDetail(0));
$("dAdvanced").addEventListener("click", () => { advancedOpen = !advancedOpen; showDetail(); });
$("dChips").addEventListener("click", (event) => {
  const chip = event.target.closest(".chip");
  if (!chip || chip.classList.contains("own") || asking) return;
  const channel = Number(chip.dataset.channel);
  if (chip.classList.contains("free")) moveTo(channel, false);
  else { asking = channel; showDetail(); }
});
$("askCancel").addEventListener("click", () => { asking = 0; showDetail(); });
$("askReplace").addEventListener("click", () => moveTo(asking, false));
$("askSwap").addEventListener("click", () => moveTo(asking, true));

// Sliders follow while dragging; the radio gets the value on release.
$("dWeight").addEventListener("input", () => { $("dWeightOut").textContent = $("dWeight").value + " %"; fill($("dWeight")); drawCurve(); });
$("dMiddle").addEventListener("input", (event) => {
  if (event.target.id !== "dDead") return;
  $("dDeadOut").textContent = event.target.value + " %";
  fill(event.target);
  drawCurve();
});
$("dMiddle").addEventListener("change", (event) => {
  if (event.target.id !== "dDead") return;
  send(`set cal.${controlOf(selected).axis}.deadzone ${event.target.value}`);
  send("save");
});
$("dWeight").addEventListener("change", () => {
  for (const channel of channelsOf(selected)) send(`set ch.${channel}.weight ${$("dWeight").value}`);
  storeAssignment();
});
$("dReverse").addEventListener("change", () => {
  for (const channel of channelsOf(selected)) send(`set ch.${channel}.reverse ${Number($("dReverse").checked)}`);
  storeAssignment();
  drawCurve();
});

// Settings and raw values: send the new value and store it at once.
document.addEventListener("change", (event) => {
  const key = event.target.dataset.key;
  if (!key) return;
  const value = event.target.type === "checkbox" ? Number(event.target.checked) : event.target.value;
  send(`set ${key} ${value}`);
  if (key.startsWith("ch.")) storeAssignment(); else send("save");
});
$("defaults").addEventListener("click", () => send("defaults"));

$("calOpen").addEventListener("click", () => send("calibrate start"));
$("calCancel").addEventListener("click", () => send("calibrate cancel"));
$("calNext").addEventListener("click", () => {
  if (calStage === 1) { calStage = 2; showCalStage(); } else send("calibrate end");
});

$("testBeep").addEventListener("click", () => send("beep 2700 200"));
$("testModuleOn").addEventListener("click", () => send("module 1"));
$("testModuleOff").addEventListener("click", () => send("module 0"));
$("testBind").addEventListener("click", () => send("bind"));
setInterval(() => { $("sysRate").textContent = linesThisSecond; linesThisSecond = 0; }, 1000);
