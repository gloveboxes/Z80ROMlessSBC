import { writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "..");
const outputPath = path.join(repoRoot, "images", "breadboard-layout.svg");

const ROWS = 63;
const BOARD_TOP = 180;
const BOARD_HEIGHT = 780;
const BOARD_WIDTH = 300;
const VIEW_WIDTH = 1060;
const BOARD_X = { memory: 50, core: 380, peripheral: 710 };
const BOARD_NAMES = {
  memory: "Memory Board (left)",
  core: "Core Board (center)",
  peripheral: "Peripheral Board (right)",
};

const WIDTHS = {
  narrow: { body: 68, label: "0.3in pin-row span" },
  wide: { body: 136, label: "0.6in pin-row span" },
  pico: { body: 184, label: "0.7in header span" },
  supply: { body: 250, label: "photo-derived clearance" },
};

const chips = [
  { key: "GAL", board: "memory", label: ["ATF22V10B/C", "GAL (24-pin)"], start: 9, end: 20, width: "narrow", color: "#36a2bd" },
  { key: "SRAM", board: "memory", label: ["AS6C1008-55PCN", "SRAM (32-pin)"], start: 22, end: 37, width: "wide", color: "#8ecae6" },

  { key: "SUPPLY", board: "core", label: ["5V supply clearance", "rows 1-3 reserved"], start: 1, end: 3, width: "supply", color: "#d2d2d2", compact: true },
  { key: "HCT541", board: "core", label: ["SN74HCT541N", "buffer (20-pin)"], start: 8, end: 17, width: "narrow", color: "#ffbd20" },
  { key: "Z80", board: "core", label: ["Z84C0020PEC", "Z80 CPU (40-pin)"], start: 19, end: 38, width: "wide", color: "#fb8500" },
  { key: "HCT245_LO", board: "core", label: ["SN74HCT245N", "low byte (20-pin)"], start: 40, end: 49, width: "narrow", color: "#ffbd20" },
  { key: "HCT245_HI", board: "core", label: ["SN74HCT245N", "high byte (20-pin)"], start: 51, end: 60, width: "narrow", color: "#ffbd20" },

  { key: "CARRIER", board: "peripheral", label: ["SN74LVC8T245PW", "carrier (24-pin)"], start: 1, end: 12, width: "wide", color: "#5fbc98" },
  { key: "PICO", board: "peripheral", label: ["Raspberry Pi Pico 2", "2x20-pin module"], start: 14, end: 33, width: "pico", color: "#91cf35" },
  { key: "LVC244", board: "peripheral", label: ["SN74LVC244AN", "buffer (20-pin)"], start: 35, end: 44, width: "narrow", color: "#5fbc98" },
  { key: "MCP", board: "peripheral", label: ["MCP23S17-E/SP", "28-pin SPDIP"], start: 46, end: 59, width: "narrow", color: "#91cf35" },
];

const connections = [
  { a: "Z80", b: "HCT541", kind: "clock" },
  { a: "Z80", b: "HCT245_LO", kind: "bus" },
  { a: "Z80", b: "HCT245_HI", kind: "bus" },
  { a: "CARRIER", b: "PICO", kind: "bus" },
  { a: "LVC244", b: "PICO", kind: "control" },
  { a: "MCP", b: "LVC244", kind: "spi" },

  { a: "GAL", b: "Z80", kind: "control" },
  { a: "GAL", b: "HCT541", kind: "control" },
  { a: "HCT541", b: "SRAM", kind: "control" },
  { a: "SRAM", b: "Z80", kind: "bus" },

  { a: "HCT541", b: "PICO", kind: "clock" },
  { a: "HCT541", b: "MCP", kind: "spi" },
  { a: "HCT245_LO", b: "MCP", kind: "bus" },
  { a: "HCT245_HI", b: "MCP", kind: "bus" },
  { a: "HCT245_LO", b: "PICO", kind: "control" },
  { a: "HCT245_HI", b: "PICO", kind: "control" },
  { a: "Z80", b: "LVC244", kind: "control" },
  { a: "Z80", b: "CARRIER", kind: "bus" },
  { a: "Z80", b: "PICO", kind: "control" },

  { a: "GAL", b: "PICO", kind: "control", long: true },
];

const styles = {
  bus: { color: "#1769e0", dash: "", width: 3 },
  control: { color: "#d97706", dash: "", width: 2.5 },
  spi: { color: "#079455", dash: "9 6", width: 2.5 },
  clock: { color: "#d62828", dash: "", width: 4.5 },
};

const chipByKey = new Map(chips.map((chip) => [chip.key, chip]));

function escapeXml(value) {
  return value.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;");
}

function rowY(row) {
  return BOARD_TOP + ((row - 1) / (ROWS - 1)) * BOARD_HEIGHT;
}

function chipBox(chip) {
  const centerX = BOARD_X[chip.board] + BOARD_WIDTH / 2;
  const top = rowY(chip.start - 0.45);
  const bottom = rowY(chip.end + 0.45);
  const width = WIDTHS[chip.width].body;
  return { x: centerX - width / 2, y: top, width, height: bottom - top, centerX, centerY: (top + bottom) / 2 };
}

const boxes = new Map(chips.map((chip) => [chip.key, chipBox(chip)]));

function boardSvg(board) {
  const x = BOARD_X[board];
  const fieldX = x + 46;
  const fieldWidth = BOARD_WIDTH - 92;
  const railWidth = 18;
  const centerX = x + BOARD_WIDTH / 2;
  const gapWidth = 14;
  const rowLines = Array.from({ length: ROWS }, (_, index) => {
    const y = rowY(index + 1);
    return `<line x1="${fieldX}" y1="${y.toFixed(2)}" x2="${(fieldX + fieldWidth).toFixed(2)}" y2="${y.toFixed(2)}" class="row-line"/>`;
  }).join("\n");

  const numbers = Array.from({ length: 13 }, (_, index) => 1 + index * 5)
    .filter((row) => row <= ROWS)
    .map((row) => `<text x="${x + 35}" y="${(rowY(row) + 4).toFixed(2)}" class="row-number">${row}</text>`)
    .join("\n");

  return `
    <g id="board-${board}">
      <text x="${centerX}" y="145" class="board-title">${BOARD_NAMES[board]}</text>
      <rect x="${x}" y="${BOARD_TOP - 18}" width="${BOARD_WIDTH}" height="${BOARD_HEIGHT + 36}" class="board-outline"/>
      <rect x="${x + 12}" y="${BOARD_TOP}" width="${railWidth}" height="${BOARD_HEIGHT}" class="power-rail"/>
      <rect x="${x + BOARD_WIDTH - 30}" y="${BOARD_TOP}" width="${railWidth}" height="${BOARD_HEIGHT}" class="power-rail"/>
      <rect x="${fieldX}" y="${BOARD_TOP}" width="${fieldWidth}" height="${BOARD_HEIGHT}" class="terminal-field"/>
      <rect x="${centerX - gapWidth / 2}" y="${BOARD_TOP}" width="${gapWidth}" height="${BOARD_HEIGHT}" class="center-gap"/>
      ${rowLines}
      ${numbers}
    </g>`;
}

function chipSvg(chip) {
  const box = boxes.get(chip.key);
  const lines = chip.compact
    ? chip.label
    : [...chip.label, `rows ${chip.start}-${chip.end}`, WIDTHS[chip.width].label];
  const firstY = box.centerY - ((lines.length - 1) * 13) / 2;
  const text = lines.map((line, index) =>
    `<tspan x="${box.centerX}" y="${(firstY + index * 13).toFixed(2)}">${escapeXml(line)}</tspan>`
  ).join("");
  return `
    <g id="chip-${chip.key}">
      <rect x="${box.x.toFixed(2)}" y="${box.y.toFixed(2)}" width="${box.width}" height="${box.height.toFixed(2)}" rx="5" fill="${chip.color}" class="chip"/>
      <text class="chip-label ${chip.width}-label">${text}</text>
    </g>`;
}

function connectionPath(connection) {
  const aChip = chipByKey.get(connection.a);
  const bChip = chipByKey.get(connection.b);
  const a = boxes.get(connection.a);
  const b = boxes.get(connection.b);
  const style = styles[connection.kind];
  let d;

  if (connection.long) {
    const startX = a.x + a.width;
    const endX = b.x;
    d = `M ${startX.toFixed(2)} ${a.centerY.toFixed(2)} C ${(startX + 45).toFixed(2)} ${a.centerY.toFixed(2)}, ${(startX + 45).toFixed(2)} 112, ${(startX + 110).toFixed(2)} 112 L ${(endX - 110).toFixed(2)} 112 C ${(endX - 45).toFixed(2)} 112, ${(endX - 45).toFixed(2)} ${b.centerY.toFixed(2)}, ${endX.toFixed(2)} ${b.centerY.toFixed(2)}`;
  } else if (aChip.board === bChip.board) {
    const boardRight = BOARD_X[aChip.board] + BOARD_WIDTH - 39;
    const startX = a.x + a.width;
    const endX = b.x + b.width;
    d = `M ${startX.toFixed(2)} ${a.centerY.toFixed(2)} Q ${boardRight.toFixed(2)} ${((a.centerY + b.centerY) / 2).toFixed(2)}, ${endX.toFixed(2)} ${b.centerY.toFixed(2)}`;
  } else {
    const aBoardX = BOARD_X[aChip.board];
    const bBoardX = BOARD_X[bChip.board];
    const leftIsA = aBoardX < bBoardX;
    const start = leftIsA ? a : b;
    const end = leftIsA ? b : a;
    const startX = start.x + start.width;
    const endX = end.x;
    d = `M ${startX.toFixed(2)} ${start.centerY.toFixed(2)} C ${(startX + 65).toFixed(2)} ${start.centerY.toFixed(2)}, ${(endX - 65).toFixed(2)} ${end.centerY.toFixed(2)}, ${endX.toFixed(2)} ${end.centerY.toFixed(2)}`;
  }

  const dash = style.dash ? ` stroke-dasharray="${style.dash}"` : "";
  return `
    <path d="${d}" class="connection-halo"/>
    <path d="${d}" fill="none" stroke="${style.color}" stroke-width="${style.width}"${dash} class="connection"/>`;
}

function legendLine(x, y, kind, label) {
  const style = styles[kind];
  const dash = style.dash ? ` stroke-dasharray="${style.dash}"` : "";
  return `<line x1="${x}" y1="${y}" x2="${x + 55}" y2="${y}" stroke="${style.color}" stroke-width="${style.width}"${dash}/><text x="${x + 68}" y="${y + 5}" class="legend-text">${label}</text>`;
}

const svg = `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="${VIEW_WIDTH}" height="1110" viewBox="0 0 ${VIEW_WIDTH} 1110" role="img" aria-labelledby="title description">
  <title id="title">Three side-by-side BB830 breadboards with chip placement and grouped connections</title>
  <desc id="description">Memory, Core, and Peripheral breadboards share aligned row numbers. Colored lines show grouped package-level signal relationships; power and ground are omitted.</desc>
  <style>
    text { font-family: Avenir Next, Helvetica Neue, sans-serif; fill: #111827; }
    .title { font-size: 25px; font-weight: 700; text-anchor: middle; }
    .subtitle { font-size: 14px; text-anchor: middle; fill: #4b5563; }
    .board-title { font-size: 18px; font-weight: 700; text-anchor: middle; }
    .board-outline { fill: #fff; stroke: #111827; stroke-width: 2; }
    .terminal-field { fill: #fff; stroke: #9ca3af; stroke-width: 1; }
    .power-rail { fill: #fee2e2; stroke: #ef4444; stroke-width: 1.5; }
    .center-gap { fill: #f3f4f6; stroke: #d1d5db; stroke-width: 1; }
    .row-line { stroke: #d1d5db; stroke-width: 0.55; stroke-dasharray: 2 5; }
    .row-number { font-size: 10px; text-anchor: end; fill: #6b7280; }
    .chip { stroke: #111827; stroke-width: 2; fill-opacity: 0.96; }
    .chip-label { font-size: 10px; font-weight: 650; text-anchor: middle; }
    .narrow-label { font-size: 8.5px; }
    .supply-label { font-size: 9px; }
    .connection-halo { fill: none; stroke: white; stroke-width: 8; stroke-opacity: 0.85; }
    .connection { stroke-linecap: round; stroke-linejoin: round; opacity: 0.92; }
    .legend-text { font-size: 13px; }
    .note { font-size: 12px; text-anchor: middle; fill: #4b5563; }
    .direct-label { font-size: 12px; font-weight: 650; text-anchor: middle; fill: #b45309; }
  </style>

  <rect width="${VIEW_WIDTH}" height="1110" fill="#fafafa"/>
  <text x="${VIEW_WIDTH / 2}" y="38" class="title">BB830 physical placement and grouped chip connections</text>
  <text x="${VIEW_WIDTH / 2}" y="64" class="subtitle">Boards are side by side with long edges parallel; equal terminal-row numbers align laterally</text>
  <text x="${VIEW_WIDTH / 2}" y="91" class="direct-label">Pico to GAL: RESET# (also tapped by Z80), PICO_CE#, PICO_OE#, PICO_WE#</text>

  ${Object.keys(BOARD_X).map(boardSvg).join("\n")}
  ${connections.map(connectionPath).join("\n")}
  ${chips.map(chipSvg).join("\n")}

  <g id="legend">
    ${legendLine(70, 1012, "bus", "Address/data bus group")}
    ${legendLine(335, 1012, "control", "Control/status group")}
    ${legendLine(600, 1012, "spi", "SPI group")}
    ${legendLine(790, 1012, "clock", "Path containing CLK")}
  </g>
  <text x="${VIEW_WIDTH / 2}" y="1048" class="note">One line represents a signal group. Blue paths are shared trunks; intermediate chips are taps, not series logic.</text>
  <text x="${VIEW_WIDTH / 2}" y="1071" class="note">Power, ground, pull resistors, and decoupling are intentionally omitted.</text>
  <text x="${VIEW_WIDTH / 2}" y="1094" class="note">DIP widths show pin-row span; Pico shows its 0.7in header span (PCB body approximately 0.83in wide).</text>
</svg>
`;

const normalizedSvg = `${svg.split("\n").map((line) => line.trimEnd()).join("\n").trimEnd()}\n`;
await writeFile(outputPath, normalizedSvg, "utf8");
console.log(`Wrote ${outputPath}`);