import mermaid from "https://cdn.jsdelivr.net/npm/mermaid@11.17.0/dist/mermaid.esm.min.mjs";

mermaid.initialize({
  startOnLoad: false,
  securityLevel: "loose",
  theme: "neutral",
  flowchart: {
    useMaxWidth: true,
  },
});
globalThis.mermaid = mermaid;

const renderMermaidDiagrams = async () => {
  const diagrams = [...document.querySelectorAll("pre.z80-mermaid")].map(
    (source) => {
      const diagram = document.createElement("div");
      diagram.className = "mermaid";
      diagram.textContent = source.textContent ?? "";
      source.replaceWith(diagram);
      return diagram;
    },
  );

  if (diagrams.length !== 0) {
    await mermaid.run({ nodes: diagrams });
  }
};

if (globalThis.document$) {
  globalThis.document$.subscribe(renderMermaidDiagrams);
} else {
  await renderMermaidDiagrams();
}