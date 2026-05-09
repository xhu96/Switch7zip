# Changelog

All notable project milestones are documented here.

This changelog was reconstructed from the original development conversation and early prototype archives. Some early versions were internal prototype packages rather than polished GitHub releases. The project remains **pre-1.0** and **not fully tested**.

## [0.9.11-pre] - FAT32 split and Switch concatenation modes

### Added

- Added FAT32 oversized-file handling modes for extracted files larger than the FAT32 single-file limit:
  - `BLOCK` — safest default; refuses to write oversized files.
  - `SPLIT` — writes oversized entries into `.split/` folders with FAT32-safe part files.
  - `CONCAT` — writes oversized entries as Switch concatenation folders and attempts to set the archive/concatenation bit.
- Added chunked large-file extraction path for oversized archive entries.
- Added `.partial` folder safety for split/concat extraction output.
- Added logging for split/concat output mode, chunk counts, and oversized-entry handling.

### Notes

- `SPLIT` mode is generic and portable, but other apps will not automatically treat the parts as one file.
- `CONCAT` mode is Switch-specific and only useful when the consuming software understands Horizon OS concatenated-file folders.

## [0.9.10-pre] - FAT32 4 GiB guard

### Added

- Added FAT32 4 GiB per-file guard.
- Added archive preflight detection for:
  - largest internal file
  - number of internal files over the FAT32 limit
  - first oversized archive entry path
- Added Properties/Diagnostics warnings for archives containing files too large for FAT32.
- Added Settings toggle for FAT32 4 GiB file guard.

### Changed

- Extraction now fails early with a clear FAT32 warning instead of stopping after writing around 4 GiB.

### Fixed

- Avoided misleading partial extractions when the target filesystem cannot store the extracted file.

## [0.9.9-pre] - GitHub-ready branding pass

### Changed

- Rebranded the project as **Switch 7zip**.
- Updated author metadata to **Xhulio**.
- Updated NRO output target to `Switch7zip.nro`.
- Updated runtime folder to:

```text
sdmc:/switch/Switch7zip/
```

### Added

- Added GitHub-ready README.
- Added pre-1.0 warning language.
- Added “not fully tested” language.
- Added honest “What I am not proud of / still to develop” section.
- Added project status documentation.
- Added known limitations documentation.
- Added review notes documentation.
- Added GitHub issue templates.
- Added pull request template.
- Added GitHub Actions host smoke-test workflow.
- Added source-file header comments describing project status and authorship.

### Notes

- The project name is descriptive and archive-focused, but the project is not affiliated with Nintendo or the official 7-Zip project.

## [0.9.9-clean] - Warning cleanup for external review

### Fixed

- Fixed devkitA64 compiler warnings found during a stricter local build.
- Fixed warning-prone archive prefix formatting in `archive_browse.c`.
- Fixed multipart filename formatting warnings in `diagnostics.c`.
- Replaced warning-prone `strncpy` and `snprintf` patterns in `main.c`.
- Fixed clipboard path copy warnings.
- Fixed archive title copy warning.
- Fixed rate-formatting warning.
- Removed accidental packaged host build artifact from the source archive.

### Notes

- This was the first package intended to be clean enough for external code review.

## [0.9.9-reviewed] - Pre-review safety pass

### Fixed

- Moved very large archive buffers from stack allocation to heap allocation.
- Moved large app state away from the main stack.
- Fixed Trash restore selection ordering bug.
- Fixed Job Center history rotation bug.
- Preserved cancellation messages instead of overwriting them with generic write errors.
- Removed duplicate declarations and unreachable return code.

### Notes

- Review identified that `source/main.c` was still too large and should eventually be split into smaller UI/input/state/action modules.

## [0.9.8-pre] - Homebrew metadata and folder comparison

### Added

- Added Homebrew/NACP info viewer.
- Added `.nro` header inspection for `NRO0` data.
- Added `.nacp` title, author, and version inspection where available.
- Added Compare with target pane.
- Added top-level comparison counts:
  - same
  - different
  - active-only
  - target-only
- Added example differences in the comparison report.

## [0.9.7-pre] - Partial-file recovery and safer cleanup

### Added

- Added `.partial` output handling for extraction.
- Added `.partial` output handling for copy operations.
- Added `.partial` output handling for ZIP compression.
- Added failed-operation report:

```text
sdmc:/switch/Switch7zip/logs/failed_operation.txt
```

- Added Clean `.partial` files action.
- Added Empty Trash action.

### Changed

- Completed files are now renamed from `.partial` only after the write finishes successfully.

### Fixed

- Cancelled or failed copy/extract/compress operations no longer look like completed files.

## [0.9.6-pre] - Dual-pane workspace and recovery helpers

### Added

- Added dual-pane workspace foundation.
- Added ability to set current folder as target pane.
- Added active/target pane swap.
- Added paste-to-target-pane workflow.
- Added recent folders to Quick Paths.
- Added Trash restore workflow.
- Added diagnostic bundle export:

```text
sdmc:/switch/Switch7zip/logs/diagnostic_bundle.txt
```

### Notes

- This release intentionally avoided FTP, HTTP upload, browser upload, and other network-transfer features.

## [0.9.5-pre] - Sorting, filtering, editor, hex viewer, and archive bit

### Added

- Added sorting modes:
  - name
  - date
  - size
  - type
- Added filters:
  - all
  - archives
  - images
  - text files
  - NROs
  - folders
- Added hidden-file toggle for dotfiles such as `.trash`.
- Added new empty file creation.
- Added small text/config editor.
- Added `.bak` backup before saving edited text files.
- Added read-only hex/ASCII viewer.
- Added Switch archive/concatenation-bit action for selected folders.

### Changed

- Kept network-transfer features out of scope by request.

## [0.9.4-pre] - PNG/JPG image viewer

### Added

- Added SDL2_image-backed image decoding.
- Added PNG image viewing.
- Added JPG/JPEG image viewing.
- Added image viewer controls:
  - zoom in/out
  - pan
  - reset zoom/pan
  - reload
  - close
- Added viewer diagnostics when images cannot be decoded.

### Changed

- Updated Makefile to link SDL2_image through Switch portlibs/pkg-config where available.

## [0.9.3-pre] - SD-card benchmark and image-viewer foundation

### Added

- Added SD-card benchmark tool.
- Added temporary 64 MiB write/read benchmark in the current folder.
- Added benchmark progress overlay.
- Added benchmark result logging.
- Added cancellation support for benchmark operations.
- Added lightweight image-viewer overlay.
- Added direct BMP viewing.
- Added PNG/JPG detection placeholder before SDL2_image support was added.

## [0.9.2-pre] - Free-space preflight and diagnostics layer

### Added

- Added free-space preflight before extraction, compression, and copy.
- Added archive unpacked-size estimation before extraction.
- Added multipart diagnostics for:
  - `.partN.rar`
  - `.zip.001`
  - `.7z.001`
  - `.rar.001`
  - split ZIP `.z01/.zip` sets
- Added Properties/Diagnostics overlay.
- Added built-in text/log/config viewer.
- Added Find in current folder.
- Added Bookmarks / Quick Paths overlay.
- Added scrollable action menu.

## [0.9.1-pre] - Multi-select and file-manager operations

### Added

- Added SD-card multi-select.
- Added mark/unmark workflow.
- Added Select all and Clear selection.
- Added Copy selection.
- Added Move selection.
- Added Paste here.
- Added Move to Trash.
- Added New folder.
- Added Rename selected.
- Added safe Trash folder:

```text
sdmc:/switch/Switch7zip/.trash/
```

- Added file-operation progress overlay for copy, move, and trash operations.
- Added logging for copy/move/trash operations.
- Added Switch software keyboard support for new-folder and rename actions.

### Changed

- Dedicated `Y` to marking items instead of compression for a more file-manager-like workflow.

## [0.9.0-pre] - Archive integrity testing

### Added

- Added Archive Integrity Test mode.
- Added action-menu item: Test archive integrity.
- Added archive read-through without extracting files.
- Added test-mode progress, speed, elapsed time, and ETA where available.
- Added cancellation support for archive testing.
- Added test results to Job Center.
- Added archive-test logs.

### Notes

- This helped diagnose large archives before writing huge outputs to the SD card.

## [0.8.0-pre] - Job Center and cancellation foundation

### Added

- Added Job Center from the action menu.
- Added recent extract/compress job history.
- Added job status, source, destination, bytes processed, files processed, and elapsed time.
- Added cooperative cancellation for extraction and compression.
- Added cancellation support in archive backends.
- Added clearer cancellation logs and status messages.

### Notes

- This was a job-center foundation, not a fully asynchronous background job engine.

## [0.7.0-pre] - Archive preview and selective extraction

### Added

- Added archive preview mode.
- Added ability to open archives like folders.
- Added internal archive breadcrumb display.
- Added archive-internal folder navigation.
- Added selective extraction for selected archive-internal files/folders.
- Added path normalization for archive preview and selective extraction.

### Changed

- Compression is disabled while browsing inside archive preview mode.

## [0.6.0-pre] - Action menu and persistent settings

### Added

- Added `+` action menu.
- Added Settings screen and persistent config file:

```text
sdmc:/switch/Switch7zip/config.ini
```

- Added settings toggles for:
  - extract into archive-named folder
  - overwrite existing files
  - done/failed sounds
  - applet-mode warning
- Added configurable extraction destination behavior.
- Added skipped-existing-file counts in logs.

### Changed

- `+` no longer immediately exits the app.
- Existing files are skipped by default unless overwrite is enabled.

## [0.5.1-pre] - Better progress and in-app log viewer

### Added

- Added richer operation progress overlay.
- Added overall percentage where available.
- Added current-file progress.
- Added speed, elapsed time, ETA, and files processed.
- Added compression current-file progress.
- Added in-app latest-log viewer.
- Added log refresh controls.
- Added elapsed time in success/failure logs.

### Changed

- `A` was made open/select only.
- `X` became the explicit extraction action to avoid accidental large extractions.

## [0.5.0-pre] - Modern UI redesign

### Added

- Introduced modern NX Commander-style visual direction.
- Added larger HAC-001-friendly file rows.
- Added top status bar with version, renderer, memory mode, and logs indicators.
- Added breadcrumb/location card.
- Added file-type icons.
- Added selected-row accent highlight.
- Added right-side Inspector panel.
- Added bottom command bar.
- Added status drawer.
- Added centered operation overlay.

### Notes

- This was the first major UI/UX modernization pass.

## [0.4.1-pre] - Performance, logs, icon, and alert sounds

### Added

- Added persistent log file:

```text
sdmc:/switch/Switch7zip/logs/latest.log
```

- Added generated success/failure alert sounds.
- Added NRO icon asset and Makefile icon packaging.
- Added accelerated SDL renderer request with fallback.

### Changed

- Removed archive pre-scan that caused huge archives to be read/decompressed twice.
- Increased archive I/O buffers.
- Reduced progress callback frequency.
- Extraction output changed to be created beside the source archive.
- Archive extraction now creates a destination folder named after the archive.
- Existing destination folders get `_1`, `_2`, etc. suffixes to avoid overwriting.

### Notes

- GPU acceleration helps UI rendering only. Archive extraction remains primarily CPU and SD-card I/O bound.

## [0.4.0-pre] - Compression, loader, and Applet Mode warning

### Added

- Added improved GUI/UX operation panel.
- Added extraction loader with percentage where archive metadata supports it.
- Added bytes written, entries, files, and current-file display.
- Added indeterminate progress fallback.
- Added ZIP compression option.
- Added startup Applet Mode warning.
- Added full-memory/Applet Mode status indication.

### Notes

- Applet Mode warning was added because large archives are more likely to fail in low-memory homebrew launch mode.

## [0.3.4-pre] - Warning-free status formatting

### Fixed

- Replaced warning-prone status `snprintf` calls with formatted status helper.
- Removed remaining status truncation warnings found during devkitA64 builds.

## [0.3.3-pre] - Larger status buffer

### Changed

- Increased GUI status buffer size to reduce truncation during long path/error messages.

### Notes

- This did not fully eliminate all compiler warnings, leading to the improved 0.3.4 approach.

## [0.3.2-pre] - Large-file extension handling

### Added

- Added recognition for `.rar`, `.cbr`, `.zipx`, `.zip.001`, and `.7z.001`.
- Added large-file build flags.
- Added ability to attempt extraction on regular files through libarchive rather than rejecting by extension.

### Notes

- RAR support depends on the libarchive build and archive type.
- Encrypted RAR, unsupported RAR5 features, and missing multipart volumes may still fail.

## [0.3.1-pre] - SDL2 link fix

### Fixed

- Added missing `-lm` math library link flag for SDL2/EGL dependencies.
- Fixed unresolved references to math functions such as `sin`, `cos`, `sqrt`, and `powf` during linking.

## [0.3.0-pre] - First GUI explorer

### Added

- Replaced console UI with SDL2 GUI.
- Added two-pane explorer layout.
- Added SD-card browsing.
- Added selected-file details/status panel.
- Added controller-driven navigation.
- Added extraction from selected archive-like files.

### Notes

- This was the first GUI build and still had a simple utility-style layout.

## [0.2.1-pre] - libarchive disk-writer fix

### Fixed

- Removed `archive_write_disk_*` extraction path because the Switch libarchive static build referenced unsupported POSIX `umask` symbols.
- Switched extraction to manual `archive_read_data()` + `fopen()` + `fwrite()` output.

### Notes

- This directly fixed the `undefined reference to umask` linker failure.

## [0.2.0-pre] - libarchive backend

### Changed

- Replaced manual LZMA SDK placeholder approach with libarchive backend.
- Added support for archive formats exposed by Switch `switch-libarchive`, including ZIP, 7z, TAR-derived formats, and supported RAR variants.
- Added path traversal protection.
- Added symlink/hardlink skipping for safer extraction.
- Added host smoke-test scaffolding.

## [0.1.0-pre] - Initial prototype scaffold

### Added

- Created initial Nintendo Switch homebrew project scaffold using devkitPro/libnx.
- Added simple console UI.
- Added fixed input/output folder concept:

```text
sdmc:/switch/Switch7zip/in/
sdmc:/switch/Switch7zip/out/
```

- Added early archive extraction design based on the official LZMA SDK concept.

### Notes

- This was a proof-of-concept scaffold, not a complete application.
- The project later moved away from a vendored SDK requirement toward devkitPro portlibs/libarchive.

## Future work before 1.0

The following work is still planned or recommended before calling the project stable:

- Cleaner UI/UX design and stronger visual consistency.
- Dedicated **Extract here** and **Extract to...** workflows.
- Easier in-app access to `latest.log`, `failed_operation.txt`, and diagnostic bundles.
- True background extraction/compression/copy jobs.
- More responsive UI during long archive operations.
- More complete delete button / delete UX.
- Better Trash browser and restore destination selection.
- Cleaner source architecture by splitting `source/main.c` into smaller modules.
- Better archive password handling and RAR/RAR5 diagnostics.
- More real-device testing on FAT32 and exFAT SD cards.
