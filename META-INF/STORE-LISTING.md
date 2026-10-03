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
  EAN-13, UPC-A/E, ISBN, ITF, Code 39/93/128, Codabar, DataBar, DX film edge,
  Aztec, Data Matrix, MaxiCode, PDF417
- **Import from gallery** — decode codes from screenshots and photos
- **History** — every scan kept, with symbology and timestamp; export the
  whole log as CSV or JSON
- **Product lookup** — optionally look up scanned EAN/UPC codes against
  Open Food Facts (decoding itself never leaves the phone)
- **Your formats, your rules** — switch code groups on or off in Settings;
  the scanner only hunts for what you care about
- **Considered battery use** — the camera fully releases while the app is
  in the background

Decoding is powered by the vendored zxing-cpp library (GPL-3.0 / Apache-2.0
/ BSD-3-Clause).

## What's new (0.1.0)

First release.

## Privacy (short version for the form)

All decoding happens on the device. The only network traffic is the optional
product lookup, which sends the scanned numeric code to Open Food Facts when
you enable it. Full policy: PRIVACY.md shipped with the package / [URL].

## Store listing assets

- Icon: `store-screenshots/harbour-zendecoder/icon-512.png`
- Screenshots (1080×2378, upscaled from device-native 1032×2272):
  `store-screenshots/harbour-zendecoder/01-scanner.png` … `04-settings.png`
