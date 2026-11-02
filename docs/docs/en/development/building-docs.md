# Building the documentation

The MkDocs source lives in `docs/docs/en`, with navigation in
`docs/docs/mkdocs.yml`. It is the single source of truth for the complete
engineering specification; do not recreate a monolithic copy in the project
README.

## Build the website

From `docs/docs`, create an environment, install the documentation dependencies,
and run a strict build:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
.venv/bin/mkdocs build --strict
```

The generated website is written to `docs/site` from the repository root.

## Source-backed phase code

Phase 1-10 use the existing `include-markdown` plugin's plain `include`
directive inside fenced code blocks and collapsed `??? example` sections.
Each section includes one complete maintained file from `src/`; do not paste
firmware into Markdown or use line-number slices that drift when code changes.
Use `include`, not `include-markdown`, for source code so Markdown link and
heading transformations do not alter it.

Paths are relative to the phase page. From `en/implementation/`, the repository
source tree is `../../../../src/`; the BIOS page includes its example from
`../../../../examples/`. Keep the source path in the section title
and retain links to related headers and build definitions in the
[source index](../reference/source-index.md).

Every documentation build reads the current checkout. The MkDocs configuration
explicitly watches `../../src` and `../../examples` during `mkdocs serve`, so
source edits continue to trigger preview rebuilds even if the include plugin loses its per-file
watches after a rebuild. A published site reflects its last deployed revision,
not subsequent local changes. Build from the complete repository so the included
sources are available; a missing include is a build error.

## PCB documentation

The current PCB pages in `docs/docs/en/pcb/` are included in navigation and
search. They describe the compact four-layer assisted-loader board, OLED
mounting, resistor arrays, corner M2.5 standoffs and its 20 MHz target.
Regenerate KiCad artifacts with the separate full KiCad pipeline before
updating route measurements or the board preview; MkDocs never routes a PCB.

## Serve locally

```sh
.venv/bin/mkdocs serve
```

MkDocs prints the preview URL, normally `http://127.0.0.1:8000/`.

## Deploy

`.github/workflows/docs.yml` performs a strict build and publishes the site to
the `gh-pages` branch whenever `main` changes. It can also be run manually with
the GitHub Actions `workflow_dispatch` control.
