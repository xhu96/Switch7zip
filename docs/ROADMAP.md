# Roadmap

Switch 7zip is intentionally still pre-1.0. The next work should focus on quality, not feature bloat.

## Before 1.0

- Refactor `source/main.c` into smaller modules.
- Add a clear **Extract here** action.
- Add a dedicated Logs screen for `latest.log`, `failed_operation.txt`, and diagnostic bundles.
- Improve delete/Trash UX with clearer restore and empty-trash confirmations.
- Improve archive preview selection UX.
- Improve text/log opening from error states.
- Improve HAC-001 touch support and visual hierarchy.
- Add more real-device test reports for 12GB+ archives.
- Add password prompt and clearer encrypted-archive diagnostics where backend support allows it.
- Add a true background job engine only after state/action separation is complete.

## Explicitly out of scope

- FTP server.
- HTTP/browser upload.
- Web UI.
- Cloud integrations.
- NSP/XCI/title installation workflows.
