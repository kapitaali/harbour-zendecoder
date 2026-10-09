# ZenDecoder — Store listing draft

**Name:** ZenDecoder
**Package:** harbour-zendecoder
**Category:** Utilities
**Summary:** Barcode & QR scanner for Sailfish OS — on-device, private, all formats.

## Description

ZenDecoder reads every code your camera, gallery or clipboard can show it:
retail barcodes, industrial 1D codes, 2D matrix codes and PDF417 — decoded
entirely on your device, with no account, no ads and no tracking.

- **Live scanning** — point and go: QR (incl. Micro QR and rMQR), EAN/8,
  EAN-13, UPC-A/E, ISBN, ITF, Code 39/93/128, Codabar, DataBar, stacked
  Code 16K and Codablock F, Aztec, Data Matrix, MaxiCode, PDF417,
  postal codes (KIX, RM4SCC), MSI/Plessey, Telepen, Pharmacode
- **Import from gallery** — decode codes from screenshots and photos
- **History** — every scan kept, with symbology and timestamp; export the
  whole log as CSV or JSON
- **Product & book lookup** — optionally look up scanned codes online:
  a chain of open product databases (Open Food Facts, Beauty Facts,
  Pet Food Facts, Products Facts) and a public barcode index for
  EAN/UPC/GTINs, and Open Library for book ISBNs — first match wins,
  and decoding itself never leaves the phone
- **Your formats, your rules** — switch code groups on or off in Settings;
  the scanner only hunts for what you care about
- **Considered battery use** — the camera fully releases while the app is
  in the background

Decoding is powered by libomniscan with zxing-cpp (Apache-2.0).

## What's new (0.2.0)

- New decoding engine (libomniscan), decoding entirely on-device as before
- Newly readable formats: postal codes (KIX, RM4SCC), MSI, Plessey,
  Telepen, Pharmacode, and the stacked codes Code 16K and Codablock F
- DataBar Limited and all DataBar variants now read (reported as DataBar)
- Book ISBNs look up title and author via Open Library; product codes
  now chain four open databases (food, cosmetics, pet food, general
  goods) plus a rate-limited barcode index — first match wins
- Torch button removed — the OS sandbox blocks in-app flashlight control;
  use the pull menu's torch instead

## Privacy (short version for the form)

All decoding happens on the device. The only network traffic is the optional
lookup, which sends the scanned numeric code (nothing else) to open product
databases — Open Food Facts, Open Beauty Facts, Open Pet Food Facts, Open
Products Facts — then a rate-limited barcode index, or to Open Library for
book ISBNs, and only when you enable it. Full policy: PRIVACY.md shipped with
the package, and at
https://github.com/kapitaali/harbour-zendecoder/blob/main/PRIVACY.md.

## Store listing assets

- Icon: `store-screenshots/harbour-zendecoder/icon-512.png`
- Cover image (1080×540, PNG and JPG):
  `store-screenshots/harbour-zendecoder/cover-1080x540.{png,jpg}`
  (regenerate with `tools/make-cover.py`; palette and motif are taken
  from the icon, so re-run it after any icon change)
- Screenshots (1080×2378, upscaled from device-native 1032×2272):
  `store-screenshots/harbour-zendecoder/01-scanner.png` … `04-settings.png`
