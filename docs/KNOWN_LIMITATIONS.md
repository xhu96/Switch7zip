# Known limitations and honesty list

This is the section to keep visible before external review. It is better to be clear than to overpromise.

## UX/UI

- Cleaner UX/UI design is still needed.
- The modal system is functional but not elegant.
- Some action-menu items are too numerous and need grouping.
- Touch input is not yet treated as a first-class interaction model.
- There is no dedicated Delete button; Trash exists, but the delete flow should be more obvious.

## Archive workflow

- FAT32 has a hard per-file limit of 4 GiB minus 1 byte. Switch 7zip can block oversized files, write `.split/` folders, or write Switch concatenation folders, but it cannot make FAT32 store a single ordinary file larger than that. Split/concat outputs depend on the target app/tool understanding them. Oversized routing also depends on libarchive reporting the entry size before data extraction starts.

- Missing a clear **Extract here** action.
- Extraction does not run in the background.
- Archive preview exists, but archive search/sort/select-all inside archives needs refinement.
- Password-protected archives need a proper password prompt and clearer error states.
- RAR/RAR5 support depends on libarchive and may not match desktop 7-Zip.

## Logs

- Logs exist, but they are not easy enough to open from every failure path.
- `latest.log`, `failed_operation.txt`, and `diagnostic_bundle.txt` should be unified into a dedicated Logs screen.

## File manager

- Dual-pane mode is a foundation, not a polished commander UI yet.
- Trash restore works but needs a better browser and confirmation flow.
- Folder comparison is top-level only.
- Resume/retry is not a true operation checkpoint system yet.

## Architecture

- `source/main.c` is too large.
- UI, input, actions, state, overlays, and viewers should be separated.
- A real background job queue would require a more deliberate threading/cancellation model.

## Testing

- Not fully tested on every Switch model, firmware, CFW, archive format, or SD-card type.
- Large archive behavior should be considered experimental until more real-device logs are collected.
