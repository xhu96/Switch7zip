# Development notes

Switch 7zip is a public pre-1.0 project. This file collects technical notes for contributors and advanced users who want to understand the current implementation state.

## Current technical direction

- The app is built with devkitPro/libnx for Nintendo Switch homebrew.
- The GUI uses SDL2 and SDL2_image.
- Archive support is provided through libarchive, so supported formats can differ from desktop 7-Zip.
- The app is designed primarily for HAC-001 handheld use at 1280x720.
- Large archive workflows are a primary focus, especially FAT32 limitations and 4 GiB-per-file handling.

## Important implementation notes

- FAT32 cannot store a single normal file larger than 4 GiB minus 1 byte.
- Switch 7zip can block oversized extracted files, split them into FAT32-safe parts, or write Switch concatenation folders.
- Split and concatenation output modes are workarounds, not magic replacements for a normal large file.
- RAR/RAR5 support depends on the libarchive build shipped by the Switch toolchain.
- Encrypted archives still need better password-prompt and error-reporting flows.

## Not fully tested yet

The following areas need more real-device reports:

- ZIP64 archives larger than 4 GiB.
- RAR5 archives.
- Encrypted ZIP, 7z, and RAR archives.
- Very large multipart archive sets.
- FAT32 split/concat output compatibility with other Switch tools.
- Long copy, move, extract, and compress jobs in Applet Mode.
- exFAT behavior after interrupted writes.
- Different firmware, Atmosphere, libnx, and devkitPro versions.

## Known technical debt

- `source/main.c` is still too large.
- UI, input, actions, state, overlays, viewers, and settings should be split into separate modules.
- Long operations are cancellable, but they are not true background jobs yet.
- The dual-pane workflow is functional but still needs UI polish.
- Logs exist, but the app still needs a more discoverable dedicated Logs screen.
- The delete/Trash flow needs clearer controls and better restore UX.
- `Extract here` should become a first-class action.

## Contributor priorities

Before a stable 1.0 release, the highest-value work is:

1. Refactor `source/main.c` into smaller modules.
2. Add a clear Extract Here / Extract To workflow.
3. Add a dedicated Logs screen.
4. Improve long-operation responsiveness.
5. Improve archive password diagnostics.
6. Collect real-device test reports for large archives on FAT32.
