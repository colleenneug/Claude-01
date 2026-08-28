# Backdrop art

Drop planet backdrops here. The game looks for these exact names:

| File | Used by |
|---|---|
| `desert-planet-bg.webp` | Thresher's Reach — the desert destination |
| `frozen-planet-bg.webp` | Cold Lantern — the frozen destination |

They are used as the sky dome, so a wide panoramic image works best
(2:1 is ideal — the image is wrapped around the horizon).

Nothing here is required. When a file is missing the sky is generated
procedurally instead, so both destinations work with an empty folder.

`tools/bundle.py` inlines everything in this directory as a data URI, so
supplied art also survives into `dist/erebus-cradle.html`.
