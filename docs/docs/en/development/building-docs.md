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

Phases 1-10 use the existing `include-markdown` plugin's plain `include`
directive inside fenced code blocks and collapsed `??? example` sections.
Each section includes one complete maintained file from `src/`; do not paste
firmware into Markdown or use line-number slices that drift when code changes.
Use `include`, not `include-markdown`, for source code so Markdown link and
heading transformations do not alter it.

Paths are relative to the phase page. From `en/implementation/`, the repository
source tree is `../../../../src/`. Keep the source path in the section title
and retain links to related headers and build definitions in the
[source index](../reference/source-index.md).

Every documentation build reads the current checkout. The MkDocs configuration
explicitly watches `../../src` during `mkdocs serve`, so source edits continue
to trigger preview rebuilds even if the include plugin loses its per-file
watches after a rebuild. A published site reflects its last deployed revision,
not subsequent local changes. Build from the complete repository so the included
sources are available; a missing include is a build error.

## Deferred PCB documentation

The historical PCB pages remain in `docs/docs/en/pcb/`, but `exclude_docs`
in the MkDocs configuration excludes them from generated pages and search.
They are also absent from navigation. Keep them hidden until PCB migration
is separately reviewed; do not regenerate the historical PCB or fabrication
artifacts as part of a documentation build.

## Serve locally

```sh
.venv/bin/mkdocs serve
```

MkDocs prints the preview URL, normally `http://127.0.0.1:8000/`.

## Deploy

`.github/workflows/docs.yml` performs a strict build and publishes the site to
the `gh-pages` branch whenever `main` changes. It can also be run manually with
the GitHub Actions `workflow_dispatch` control.