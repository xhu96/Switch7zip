# Review notes

Switch 7zip is intentionally published as a pre-1.0 preview.

## What reviewers should know

- The archive engine depends on the Switch devkitPro/libarchive package. Format support can differ from desktop 7-Zip.
- Large archive workflows are the main focus, especially FAT32 limitations and 4 GiB-per-file handling.
- The app contains early versions of several file-manager features. They are useful, but they still need more UX polish and field testing.
- The UI is optimized first for HAC-001 handheld use at 1280x720.
- The source is functional but not yet fully modularized. `source/main.c` is still too large and should be split before a stable 1.0.

## Not fully tested

The following cases still need real-device reports:

- RAR5 archives
- encrypted ZIP/7z/RAR archives
- very large multipart sets
- FAT32 split/concat outputs consumed by third-party tools
- long copy/move/extract jobs in Applet Mode
- exFAT behavior after interrupted writes
- firmware/libnx/devkitPro version differences

## Areas I am not proud of yet

- UI/UX design is improved but not final.
- Logs exist, but they are not obvious enough to open from a dedicated screen.
- Extraction/compression blocks the main workflow instead of running as a true background job.
- Delete/Trash workflows need a clearer dedicated button and restore UX.
- `Extract here` should exist as a first-class context action.
- The source tree needs better module boundaries before 1.0.
