# The random-present-while-parked bug

## Symptom
The handles present by themselves (then retract on their timeout) while the car is
parked and untouched.

## Root cause
`CAN_decode_car_tsl` (0x08005924), CAN ID **0x339** (lock/unlock): on *any* lock-state
transition it increments the present-request counter (0x2000003d), which is forwarded
to the handles as the pop trigger. **Ungated by gear, no debounce.**

This path is intended for the display's lock/unlock button (feature #3), but it fires
identically when the **Tesla phone-key passive entry** flickers the lock state (phone at
the edge of range while parked) → each flicker pops the handles.

## Power-up variant
Observed: unplug/replug the ECU in the car → all handles popped. Two candidates:
- **Handle modules' own power-on self-test** (in their firmware; would fire even with no
  ECU logic — the most likely explanation for *all* popping at once).
- **ECU first-CAN-frame lock sync**: on boot RAM = 0, so the first 0x339 frame carrying the
  car's real lock state reads as a "change" → present. Same ungated root cause, no
  first-boot guard.

Disambiguate: watch the debug UART on power-up (a `lock state/src` print ⇒ the ECU path),
or power the ECU with no CAN bus connected (still pops ⇒ the handle modules).

## Confirming on-device
The firmware prints `lock state %d` / `lock src %d` over its debug UART on every 0x339
event. If a random present is immediately preceded by such a print, 0x339 is confirmed —
and the printed `lock_src` (values 1/3/4 seen) identifies the passive source to filter.

## Fix options
- **Debounce** — ignore repeat lock-change pops within ~1–2 s (needs no extra data).
- **Source gate** — only pop for a real button `lock_src`; ignore passive/walk-away.
- **First-frame guard** — skip the initial lock-state sync at boot.
- **Disable** — drop the lock-change pop entirely (loses feature #3).
