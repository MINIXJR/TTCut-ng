# Coding conventions

Measured by the `code-audit` scanner on 2026-09-03 and agreed as the yardstick.
Each rule line below is read by the scanner; it reports drift between this file
and the measured majority on every audit run. Keep the line shape
`- **feature**: `value`` when editing a rule. Prose is free-form.

Existing code is not reformatted to match these rules. They apply to new files
and to files that are being reworked anyway.

## C/C++

- **cpp/indent**: `2`
  Two spaces, no tabs (the TTCut heritage). The H.26x/Smart-Cut block
  (`avstream/tth26*`, `ttnaluparser`, `extern/ttessmartcut`, `ttffmpegwrapper`,
  `tthevcseam`, `ttmkvmergeprovider`, the settings pages and the C tools under
  `tools/`) uses four spaces consistently and stays that way; do not mix widths
  inside a file.
- **cpp/class_prefix**: `TT`
  Exception: pure interfaces carry an `I` prefix (`IStatusReporter`,
  `IMuxProvider`, `ITTMpvBackend`).
- **cpp/member_prefix**: `m`
  `mFoo` for members, `mpFoo` for pointer members, `sFoo` for statics. Legacy
  headers without a prefix are renamed when the class is reworked (as done for
  TTMessageLogger, TTEncodeParameter and TTTranscodeProvider in the 2026-09
  audit), not in bulk.
- **cpp/connect_style**: `pointer`
  Pointer-to-member `connect(sender, &Class::signal, receiver, &Class::slot)`;
  never the `SIGNAL()`/`SLOT()` macros. Overloaded signals use `qOverload<>`.
- **cpp/logging**: `qdebug`
  `qDebug`/`qWarning`/`qCritical` — the Qt message handler installed in
  `gui/ttcutmain.cpp` routes them into `TTMessageLogger`, so direct
  `log->xxxMsg()` calls are legacy. Verbose per-subsystem output stays behind
  the `TTSettings` log switches (`logCutPipeline()`, `logAVStream()`, ...).
  The standalone C tools under `tools/` print with `printf`/`fprintf`.
- **cpp/header_comment**: `//!`
  Doxygen line comments before declarations and definitions. The older
  `/*!`, `/**` and `/* //// */` banner forms remain in place.

## Bash

- **bash/set_flags**: `e`
  `set -e` is the measured baseline (`ttcut-demux` and the example scripts).
  New scripts start with `set -euo pipefail`; converting `ttcut-demux` to `-u`
  is a separate project with its own ES byte-identity gate.
- **bash/func_decl**: `paren`
  `name() {`, no `function` keyword.
- **bash/quoting**: `quoted`
  Every expansion quoted; `read -r`; array subscripts as `[i]`.
