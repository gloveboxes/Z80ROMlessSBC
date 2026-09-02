# Z80 ROMless SBC MkDocs site

This directory contains the MkDocs configuration and English engineering
documentation for the Z80 ROMless SBC.

## Layout

- `mkdocs.yml` defines navigation, theme, plugins, and strict link handling.
- `requirements.txt` pins the supported documentation dependencies.
- `en/` contains the English documentation pages.
- `en/images/` contains documentation images; `npm run layout` regenerates the
	breadboard layout there.

## Build locally

From this directory:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
.venv/bin/mkdocs build --strict
```

The generated site is written to `docs/site` from the repository root.

## Serve locally

```sh
.venv/bin/mkdocs serve
```

MkDocs prints the preview URL, normally `http://127.0.0.1:8000/`.

## Deploy

`.github/workflows/docs.yml` performs a strict build and deploys the site to
the `gh-pages` branch on each push to `main` or by manual dispatch.
