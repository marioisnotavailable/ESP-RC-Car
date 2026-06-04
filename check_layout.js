// Prüft Koordinaten-Überlappungen und Text-Inhalt im JS-Skript
// Analysiert make_pptx.js nach offensichtlichen Layout-Fehlern

const fs = require("fs");
const src = fs.readFileSync("make_pptx.js", "utf8");

// Slide width = 10", height = 5.625"
const SW = 10, SH = 5.625;

// Alle addShape/addText calls mit x,y,w,h extrahieren
const re = /(?:addShape|addText)\s*\([^,]+,\s*\{[^}]*x\s*:\s*([\d.]+)\s*,[^}]*y\s*:\s*([\d.]+)\s*,[^}]*w\s*:\s*([\d.]+)\s*,[^}]*h\s*:\s*([\d.]+)/g;

let m;
const elems = [];
while ((m = re.exec(src)) !== null) {
  const [_, x, y, w, h] = m.map(Number);
  if (x + w > SW + 0.01) elems.push({ type: "OVERFLOW_X", x, y, w, h, right: x + w });
  if (y + h > SH + 0.01) elems.push({ type: "OVERFLOW_Y", x, y, w, h, bottom: y + h });
}

if (elems.length === 0) {
  console.log("✓ Keine x/y Overflows gefunden");
} else {
  console.log("⚠ Mögliche Overflows:");
  elems.forEach(e => console.log(` ${e.type}: x=${e.x} y=${e.y} w=${e.w} h=${e.h}  →  ${e.right||""} ${e.bottom||""}`));
}

// Speziell Folie 2 prüfen: Motor-Node vs Energy-Bar
console.log("\n── Folie 2 (Systemarchitektur) ──");
const f2nodes = [
  {n:"App",      x:1.4,  y:1.52, h:0.72},
  {n:"ESP32",    x:1.4,  y:2.85, h:0.82},
  {n:"DRV8323",  x:1.4,  y:3.97, h:0.62},
  {n:"Motor",    x:1.4,  y:4.87, h:0.5},
  {n:"EnergyBar",x:0.38, y:5.17, h:0.35},
];
f2nodes.forEach(n => {
  const bottom = n.y + n.h;
  const flag = bottom > SH ? " ← ÜBER SLIDE!" : bottom > 5.17 && n.n !== "EnergyBar" ? " ← ÜBERLAPPT EnergyBar!" : "";
  console.log(`  ${n.n.padEnd(12)} y=${n.y} h=${n.h} → bottom=${bottom.toFixed(3)}${flag}`);
});

// Folie 3 prüfen: PCB Diagramm
console.log("\n── Folie 3 (PCB) ──");
const f3nodes = [
  {n:"USB-C",     x:0.38, y:1.54, w:1.9, h:0.58},
  {n:"LiPo",      x:0.38, y:3.0,  w:1.9, h:0.58},
  {n:"LadeIC",    x:0.38, y:2.44, w:1.9, h:0.58},
  {n:"BMS",       x:2.78, y:3.0,  w:1.9, h:0.58},
  {n:"MUX",       x:1.58, y:4.28, w:1.9, h:0.58},
  {n:"Buck",      x:3.86, y:4.28, w:1.9, h:0.58},
  {n:"ESP32",     x:6.14, y:4.28, w:1.9, h:0.58},
  {n:"DRV8323",   x:5.0,  y:1.54, w:1.9, h:0.58},
  {n:"hconn_bat_drv", x:2.28, y:3.12, w:3.6, h:0.04},
  {n:"vconn_drv", x:5.84, y:1.54, w:0.04, h:1.62},
  {n:"IC-table",  x:8.12, y:1.54, w:1.5,  h:3.28},
];
f3nodes.forEach(n => {
  const issues = [];
  if (n.x + n.w > SW + 0.01) issues.push(`OVERFLOW_X (${(n.x+n.w).toFixed(2)})`);
  if (n.y + n.h > SH + 0.01) issues.push(`OVERFLOW_Y (${(n.y+n.h).toFixed(2)})`);
  // Prüfe ob hconn durch BMS läuft
  if (n.n === "hconn_bat_drv") {
    // BMS: x=2.78..4.68, y=3.0..3.58
    // hconn: y=3.12 (liegt in 3.0..3.58), x=2.28..5.88 (überlappt 2.78..4.68)
    issues.push("LÄUFT DURCH BMS-BOX (y=3.12 ∈ [3.0,3.58], x=2.28..5.88 ∩ [2.78,4.68])");
  }
  if (n.n === "vconn_drv") {
    // DRV: x=5.0..6.9, y=1.54..2.12
    // vconn: x=5.84 (∈ 5.0..6.9), starts at y=1.54 (top of DRV!) → läuft durch DRV
    issues.push("STARTET AN DRV-OBERKANTE, läuft durch DRV-Box nach unten");
  }
  const flag = issues.length ? ` ← ${issues.join(", ")}` : "";
  console.log(`  ${n.n.padEnd(18)} x=${n.x} y=${n.y} w=${n.w} h=${n.h}${flag}`);
});
