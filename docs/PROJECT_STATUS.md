# Project status

Switch 7zip is a **pre-1.0 preview**. It is suitable for experimentation and review, but it should not yet be presented as a fully tested replacement for established Switch file managers.

## Release state

- Version: 0.9.11-pre
- Author: Xhulio
- Primary target: Nintendo Switch HAC-001 handheld use
- Runtime format: `.nro`
- Stability: not fully tested

## What should be tested before wider release

- ZIP64 archives larger than 4GB.
- 7z archives with many small files.
- RAR/RAR5 archives, including unsupported cases.
- Multipart archives with missing parts.
- Full-memory launch vs Applet Mode.
- Copy/move/delete/Trash restore on real SD cards.
- `.partial` cleanup safety.
- Text editor save/backup behavior.
- Image viewer memory behavior with large PNG/JPG files.
- SD benchmark on slow and fast cards.

## Quality notes

The code has had a warning-cleanup pass, stack-safety pass, and safer temporary-file pass. The largest remaining technical debt is architectural: `source/main.c` should be split into smaller modules before a 1.0 release.


## 0.9.11-pre note

Adds FAT32 split-part and Switch concatenation-folder modes for oversized archive entries.

## External review

See `docs/REVIEW_NOTES.md` for known review concerns, untested areas, and pre-1.0 limitations.
