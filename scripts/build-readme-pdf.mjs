#!/usr/bin/env node

import { spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { basename, dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

import puppeteer from "puppeteer-core";

const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const inputPath = resolve(repositoryRoot, process.argv[2] ?? "README.md");
const outputPath = resolve(repositoryRoot, process.argv[3] ?? "README.pdf");
const temporaryDirectory = mkdtempSync(join(tmpdir(), "z80sbc-readme-pdf-"));
const htmlPath = join(temporaryDirectory, "README.html");
const cssPath = join(temporaryDirectory, "README.css");

function fail(message) {
  console.error(`PDF build failed: ${message}`);
  process.exitCode = 1;
}

function findBrowser() {
  const configured = process.env.BROWSER_EXECUTABLE;
  const candidates = [
    configured,
    "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
    "/usr/bin/microsoft-edge",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
  ].filter(Boolean);
  return candidates.find((candidate) => existsSync(candidate));
}

const css = String.raw`
@page {
  size: A4 portrait;
  margin: 11mm 9mm 14mm;
}

@page diagram-portrait {
  size: A4 portrait;
  margin: 7mm;
}

@page diagram-landscape {
  size: A4 landscape;
  margin: 7mm;
}

:root {
  color: #1f2933;
  font-family: Verdana, sans-serif;
  font-size: 9.4pt;
  line-height: 1.38;
}

body {
  margin: 0 auto;
  max-width: 190mm;
}

h1, h2, h3, h4, h5, h6 {
  color: #12263a;
  break-after: avoid-page;
  font-family: "Aptos", "Segoe UI", sans-serif;
  line-height: 1.18;
}

h1 {
  border-bottom: 2px solid #be3a2b;
  font-size: 25pt;
  margin: 0 0 8mm;
  padding-bottom: 3mm;
}

h2 {
  border-bottom: 1px solid #9aa5b1;
  break-before: page;
  font-size: 17pt;
  margin-top: 8mm;
  padding-bottom: 1.5mm;
}

h3 { font-size: 13pt; margin-top: 6mm; }
h4 { font-size: 11pt; margin-top: 5mm; }

a { color: #155e75; text-decoration: none; }
p, li { orphans: 3; widows: 3; }
blockquote {
  border-left: 3px solid #d97706;
  color: #3e4c59;
  margin-left: 0;
  padding: 1mm 0 1mm 4mm;
}

table {
  border-collapse: collapse;
  font-size: 7.4pt;
  margin: 3mm 0 5mm;
  width: 100%;
}

thead { display: table-header-group; }
tr { break-inside: avoid; }
th, td {
  border: 0.25mm solid #bcccdc;
  padding: 1.2mm 1.5mm;
  text-align: left;
  vertical-align: top;
}
th { background: #e8eef3; color: #102a43; }
tbody tr:nth-child(even) { background: #f6f8fa; }

img {
  height: auto;
  max-height: 235mm;
  max-width: 100%;
}

code {
  font-family: "SFMono-Regular", Consolas, monospace;
  font-size: 0.88em;
}

pre {
  background: #f2f4f7;
  border: 0.25mm solid #cbd2d9;
  border-radius: 1.5mm;
  font-size: 7.2pt;
  line-height: 1.28;
  overflow-wrap: anywhere;
  padding: 2.5mm;
  white-space: pre-wrap;
}

nav#TOC {
  break-after: page;
  font-size: 8.5pt;
}

nav#TOC::before {
  color: #12263a;
  content: "Contents";
  display: block;
  font-size: 17pt;
  font-weight: 700;
  margin-bottom: 3mm;
}

nav#TOC ul { list-style: none; padding-left: 4mm; }
nav#TOC > ul { padding-left: 0; }

.web-toc { display: none; }

.diagram-sheet {
  align-items: stretch;
  break-after: page;
  break-before: page;
  display: flex;
  flex-direction: column;
  height: 267mm;
  justify-content: flex-start;
  page: diagram-portrait;
  width: 190mm;
}

.diagram-sheet.landscape {
  height: 190mm;
  page: diagram-landscape;
  width: 277mm;
}

.diagram-title {
  color: #12263a;
  flex: 0 0 auto;
  font-size: 16pt;
  font-weight: 700;
  margin: 0 0 5mm;
}

.diagram-canvas {
  align-items: center;
  display: flex;
  flex: 1 1 auto;
  justify-content: center;
  min-height: 0;
  overflow: hidden;
  width: 100%;
}

.diagram-canvas svg {
  height: auto !important;
  max-height: 100% !important;
  max-width: 100% !important;
  width: 100% !important;
}

.diagram-canvas .nodeLabel,
.diagram-canvas .edgeLabel {
  font-size: 16px !important;
}

.mermaid-toolbar,
.diagram-canvas button,
[class*="mermaid"] button {
  display: none !important;
}

@media print {
  body { max-width: none; }
  a { color: #155e75; }
}
`;

async function buildPdf() {
  if (!existsSync(inputPath)) {
    throw new Error(`input does not exist: ${inputPath}`);
  }
  const browserExecutable = findBrowser();
  if (!browserExecutable) {
    throw new Error(
      "no supported browser found; install Edge, Chrome, or Chromium, " +
        "or set BROWSER_EXECUTABLE",
    );
  }

  const markdown = readFileSync(inputPath, "utf8");
  const expectedDiagramCount = (markdown.match(/^```mermaid\s*$/gm) ?? []).length;
  writeFileSync(cssPath, css, "utf8");

  const pandoc = spawnSync(
    "pandoc",
    [
      inputPath,
      "--from=gfm+tex_math_dollars",
      "--to=html5",
      "--standalone",
      "--embed-resources",
      `--resource-path=${repositoryRoot}`,
      "--mathml",
      "--toc",
      "--toc-depth=3",
      "--highlight-style=tango",
      `--css=${cssPath}`,
      `--metadata=title:${basename(inputPath, ".md")}`,
      `--output=${htmlPath}`,
    ],
    { encoding: "utf8" },
  );
  if (pandoc.error?.code === "ENOENT") {
    throw new Error("pandoc was not found; install it with `brew install pandoc`");
  }
  if (pandoc.status !== 0) {
    throw new Error(pandoc.stderr.trim() || `pandoc exited with ${pandoc.status}`);
  }

  const mermaidScript = join(
    repositoryRoot,
    "node_modules",
    "mermaid",
    "dist",
    "mermaid.min.js",
  );
  if (!existsSync(mermaidScript)) {
    throw new Error("Mermaid is not installed; run `npm ci` first");
  }

  const browser = await puppeteer.launch({
    executablePath: browserExecutable,
    headless: true,
    args: ["--allow-file-access-from-files", "--disable-gpu"],
  });
  try {
    const page = await browser.newPage();
    page.setDefaultTimeout(120_000);
    await page.setViewport({ width: 1600, height: 1000, deviceScaleFactor: 1 });
    await page.goto(pathToFileURL(htmlPath).href, { waitUntil: "networkidle0" });
    await page.addScriptTag({ path: mermaidScript });

    const renderedDiagramCount = await page.evaluate(async () => {
      const sourceBlocks = [...document.querySelectorAll("pre.mermaid")];
      const diagrams = sourceBlocks.map((sourceBlock) => {
        const canvas = document.createElement("div");
        canvas.className = "diagram-canvas mermaid";
        canvas.textContent = sourceBlock.textContent ?? "";
        const wrapper = sourceBlock.parentElement?.classList.contains("sourceCode")
          ? sourceBlock.parentElement
          : sourceBlock;
        wrapper.replaceWith(canvas);
        return canvas;
      });

      mermaid.initialize({
        startOnLoad: false,
        securityLevel: "loose",
        theme: "neutral",
        flowchart: {
          curve: "linear",
          htmlLabels: true,
          nodeSpacing: 24,
          rankSpacing: 32,
          useMaxWidth: false,
        },
        themeVariables: {
          fontFamily: "Aptos, Segoe UI, sans-serif",
          fontSize: "16px",
          lineColor: "#486581",
          primaryColor: "#edf2f7",
          primaryTextColor: "#102a43",
          primaryBorderColor: "#829ab1",
        },
      });
      await mermaid.run({ nodes: diagrams });
      await document.fonts.ready;

      const headings = [...document.querySelectorAll("h1, h2, h3, h4, h5, h6")];
      for (const diagram of diagrams) {
        const svg = diagram.querySelector("svg");
        if (!svg) {
          throw new Error("Mermaid did not produce an SVG");
        }
        const viewBox = svg.viewBox.baseVal;
        const aspectRatio = viewBox.height === 0 ? 1 : viewBox.width / viewBox.height;
        const ownerHeading = headings
          .filter(
            (heading) =>
              heading.compareDocumentPosition(diagram) &
              Node.DOCUMENT_POSITION_FOLLOWING,
          )
          .at(-1);
        const sheet = document.createElement("section");
        sheet.className = `diagram-sheet${aspectRatio > 1.35 ? " landscape" : ""}`;
        const title = document.createElement("div");
        title.className = "diagram-title";
        title.textContent = ownerHeading?.textContent ?? "System diagram";
        diagram.replaceWith(sheet);
        sheet.append(title, diagram);
      }
      return diagrams.length;
    });

    if (renderedDiagramCount !== expectedDiagramCount) {
      throw new Error(
        `rendered ${renderedDiagramCount} Mermaid diagrams; expected ${expectedDiagramCount}`,
      );
    }

    await page.emulateMediaType("print");
    await page.pdf({
      path: outputPath,
      displayHeaderFooter: true,
      headerTemplate: "<div></div>",
      footerTemplate: String.raw`
        <div style="box-sizing:border-box;color:#52606d;font-family:Aptos,Segoe UI,sans-serif;font-size:8px;text-align:center;width:100%;">
          Page <span class="pageNumber"></span> of <span class="totalPages"></span>
        </div>`,
      printBackground: true,
      preferCSSPageSize: true,
      tagged: true,
      outline: true,
    });
    console.log(
      `Wrote ${outputPath} with ${renderedDiagramCount} full-page Mermaid diagrams`,
    );
  } finally {
    await browser.close();
  }
}

try {
  await buildPdf();
} catch (error) {
  fail(error instanceof Error ? error.message : String(error));
} finally {
  rmSync(temporaryDirectory, { recursive: true, force: true });
}