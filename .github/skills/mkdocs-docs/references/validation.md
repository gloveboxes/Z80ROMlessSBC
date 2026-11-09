# Documentation Validation

```sh
npm run layout
docs/docs/.venv/bin/mkdocs build --strict -f docs/docs/mkdocs.yml
git diff --check HEAD -- README.md docs .github/workflows/docs.yml
```

`npm run layout` regenerates `docs/docs/en/images/breadboard-layout.svg`. The
pinout images live beside it. Inspect representative generated pages for broken
tables, Mermaid failures and poor heading placement after structural changes.

A strict build validates Markdown and links but does not execute JavaScript.
After changing Mermaid source, `mermaid.mjs`, CSS, navigation or the theme,
also use a browser at desktop and 390 px mobile widths to verify:

- every `pre.z80-mermaid` becomes one populated `.mermaid svg`
- the expected diagram count renders on every affected page
- SVG and table containers do not cause page-level horizontal overflow
- labels remain readable and client-side navigation reruns the Mermaid adapter

## C Excerpts

The phase-specific C excerpts are explanatory and cumulative. When editing them:

- Keep maintained-source links adjacent.
- Validate Markdown fence balance.
- Keep each page internally coherent; do not imply that a snippet is the
  canonical maintained implementation.
- Validate the maintained source and stage builds rather than trusting
  hand-written SDK stubs; stubs can miss incorrect or absent real SDK includes.
