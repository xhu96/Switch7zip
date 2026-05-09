# Contributing to Switch 7zip

Thanks for considering contributing.

## Current priorities

1. Make large archive extraction safer and easier to diagnose.
2. Improve the HAC-001 handheld UI/UX.
3. Split `source/main.c` into smaller maintainable modules.
4. Add a clear **Extract here** action.
5. Improve the Logs screen.
6. Add a real background job engine later.

## Before opening a pull request

- Build with devkitPro and confirm `Switch7zip.nro` is produced.
- Avoid introducing new compiler warnings.
- Keep file operations conservative by default.
- Do not add network-transfer features unless the maintainer explicitly changes the roadmap.
- Do not include Nintendo SDK files, firmware keys, copyrighted assets, or proprietary console files.

## Code style

- Prefer explicit bounds checks.
- Prefer `.partial` outputs for long writes.
- Log useful failure context.
- Keep HAC-001 720p readability in mind.
- Avoid blocking UI work where possible, but do not fake background support until the job engine is real.
