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

## Serve locally

```sh
.venv/bin/mkdocs serve
```

MkDocs prints the preview URL, normally `http://127.0.0.1:8000/`.

## Deploy

`.github/workflows/docs.yml` performs a strict build and publishes the site to
the `gh-pages` branch whenever `main` changes. It can also be run manually with
the GitHub Actions `workflow_dispatch` control.