# reference (local only)

The factory firmware binaries live here for rollback and as the reverse-engineering
source of truth, but they are **git-ignored** — they're EVOffer's proprietary firmware
and are not redistributed in this public repo.

- `gd32_backup.bin` — verified 128 KB SWD dump of the stock unit (sha256 970934547eb7…)
- `CX_CAN_original.bin` — factory app carved from the dump (SD rollback for 0x08005000)

Keep your own local copies here; they are not part of the repo.
