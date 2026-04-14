# QtFRED Help Topics (Qt Help Framework)

This document describes how QtFRED can use Qt's help framework behind the existing **Help Topics** menu item.

## What is now implemented

- `Help -> Help Topics` now opens a `HelpTopicsDialog` backed by `QHelpEngine`.
- The dialog shows:
  - Contents tree (`QHelpContentWidget`)
  - Index (`QHelpIndexWidget`)
  - A right-side document view (`QTextBrowser`) that can load `qthelp://` URLs from the help collection.
- QtFRED looks for a help collection at:
  - `help/qtfred.qhc` relative to the QtFRED executable.
- If the collection file is missing, QtFRED shows a clear informational message.

## Packaging model

Qt Help normally uses:

- `.qhp`: Help project input (XML)
- `.qch`: Compiled help content bundle
- `.qhcp`: Collection project input (XML)
- `.qhc`: Compiled help collection database

At runtime, QtFRED should ship with:

- `help/qtfred.qhc`
- `help/qtfred.qch`

## Suggested authoring layout

Recommended source layout to add in a follow-up:

- `qtfred/help-src/qtfred.qhp`
- `qtfred/help-src/qtfred.qhcp`
- `qtfred/help-src/doc/index.html`
- `qtfred/help-src/doc/*.html`
- `qtfred/help-src/doc/css/*.css`
- `qtfred/help-src/doc/img/*`

## Build generation commands

Example commands (run from `qtfred/help-src`):

```bash
qhelpgenerator qtfred.qhp -o ../help/qtfred.qch
qcollectiongenerator qtfred.qhcp -o ../help/qtfred.qhc
```

In CMake, a follow-up can wire this into a custom target and install/copy `qtfred.qch` and `qtfred.qhc` into `${BINARY_DESTINATION}/help`.

## Minimal example identifiers

Use stable namespace/virtual-folder values in your `.qhp`:

- Namespace: `org.hard-light.qtfred`
- Virtual folder: `doc`

That lets pages resolve like:

- `qthelp://org.hard-light.qtfred/doc/index.html`

## Follow-up improvements

1. Add search integration (`QHelpSearchEngine`) and a Search tab.
2. Add context help entry points from major dialogs (e.g. Mission Events, Ship Editor).
3. Add a fallback action that opens online documentation if local help is not installed.
4. Add CI/build verification that generated `.qch`/`.qhc` are present for release artifacts.
