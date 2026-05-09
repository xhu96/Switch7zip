# Changelog

## 0.9.11-pre FAT32 split and concatenation modes

- Added **FAT32 >4 GiB handling** setting with three modes: `BLOCK`, `SPLIT`, and `CONCAT`.
- `BLOCK` keeps the old safe behavior and stops before writing oversized files.
- `SPLIT` extracts oversized archive entries into `.split/` folders with FAT32-safe part files.
- `CONCAT` extracts oversized archive entries into Switch concatenation folders and attempts to set the archive/concatenation bit.
- Oversized extraction now logs split/concat counts and part counts.
- Limitations, status, and build metadata updated to 0.9.11-pre.

## 0.9.10-pre FAT32 guard

- Added a FAT32 4 GiB per-file extraction guard.
- Archive preflight now tracks largest internal file and counts files over the FAT32 per-file limit.
- Extraction now fails early with a clear FAT32 message instead of writing until a `.partial` file hits the 4 GiB boundary.
- Added a Settings toggle: **FAT32 4 GiB file guard**. Enabled by default.
- Properties/Diagnostics now show largest archive entry and FAT32 oversized-entry warnings.
- Limitations now document FAT32 behavior for large archives.

## 0.9.9-pre GitHub-ready branding pass

- Rebranded project from the prototype name to **Switch 7zip**.
- Updated app metadata author to **Xhulio**.
- Updated NRO target name to `Switch7zip.nro`.
- Moved default app data paths to `sdmc:/switch/Switch7zip/`.
- Added visible pre-1.0 safety alert in the app UI.
- Updated documentation for GitHub publication.
- Added project status, known limitations, GitHub description, contributing notes, issue templates, and pull-request template.
- Added source-file header comments describing author, purpose, and pre-1.0 status.
- Kept prior warning-clean code cleanup from 0.9.9-clean.

## 0.9.9-clean

- Cleaned devkitA64 truncation/string warnings reported during external-review build.
- Replaced warning-prone string copies with bounded copy helpers.
- Removed packaged host build artifact from the source distribution.

## 0.9.9 reviewed

- Moved large UI/archive buffers off the stack.
- Fixed Trash restore selection handling.
- Fixed Job Center history rotation.
- Improved cancellation error reporting.

## Earlier pre-1.0 milestones

- Archive preview and selective extraction.
- ZIP compression.
- Free-space preflight and multipart diagnostics.
- Text/log viewer, text editor, hex viewer, and image viewer.
- Sorting, filtering, bookmarks, recent paths, dual-pane helpers, Trash, `.partial` recovery, SD benchmark, NRO/NACP info, and diagnostics export.
