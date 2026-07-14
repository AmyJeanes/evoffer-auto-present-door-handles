# Factory firmware packages

Where the stock firmware comes from, how it's packaged, and why we can't just unzip it.

## Upstream repo
EVOffer publishes the update files at **github.com/evoffer/auto-present-door-handles-firmware** —
~15 password-protected zips, one flashable payload each, named
`Handle <line> <mkX.Y> update<YYMMDD>[ (variant)].zip`. The payload filename encodes the hardware
generation: legacy v1/v2 = `App.zbo` / `Appa.zbo`; Mark 1.5 = `DOOR.bin`; **Mark 2.x / 3.0 =
`CX_CAN.bin`**. Amy's unit = `Handle RGB mk3.0 update250105.zip` → `CX_CAN.bin` (Light-Bar/RGB,
"1 Wireless Connected ECU").

`CX_CAN.bin` is the **application region only** (~24.6 KB), not the full 128 KB flash — the SD
updater erases + writes just the app slot at 0x08005000 (see [firmware-map.md](firmware-map.md)).
`reference/CX_CAN_original.bin` is the corresponding region carved from our own SWD dump.

## Encryption (why the zips don't just open)
The zip **passwords are not public** — EVOffer gates them behind Telegram support (t.me/evoffer).
Encryption is mixed per payload:
- Some are **WinZip AES-256 (method 99)** — not crackable.
- Many, **including 250105**, are traditional **ZipCrypto (method 8)** — vulnerable to a
  known-plaintext attack (bkcrack) *if* you already hold ~12+ contiguous plaintext bytes of the
  compressed stream.
- 250105 `CX_CAN.bin`: 24,588 bytes uncompressed, CRC-32 `0x400c4d10`.

## bkcrack attempt — no keys (dead end, don't repeat blind)
We already **have** the plaintext app image from the SWD dump, so a known-plaintext attack looked
viable: feed the dump's app bytes as the known plaintext, recover the ZipCrypto keys, decrypt every
ZipCrypto zip. A full sweep — multiple deflate re-compressions of the known plaintext (levels 1–9 ×
strategies) against every window offset — **found no keys**. The on-disk plaintext never lines up
with the zip's deflate stream (different compressor / parameters, or the payload is not a raw
deflate of our image). The AES-256 zips are unreachable regardless.

**Net: we don't need the zips.** We have the dumped app and a full SWD image for rollback, so
decrypting the factory packages was never on the critical path — this was an opportunistic side
quest, now closed.
