# ZenDecoder — Privacy Policy

**Last updated: 2026-10-03**

ZenDecoder (harbour-zendecoder) is a barcode and QR code reader for
Sailfish OS. The short version: **nothing you scan leaves the phone
unless you turn on product lookup, and there is no tracking of any kind.**

## What the app does with your data

| Data | Where it stays | Leaves the device? |
|------|----------------|--------------------|
| Camera images used for scanning | Temporary file in the app's private cache, deleted immediately after each decode | No |
| Gallery images you import | Read only, never copied, never modified | No |
| Scan history | Local SQLite database in the app's private storage | No — unless *you* export it |
| CSV/JSON export | Written to a directory you pick | Only where you put it |
| Product lookup (off by default) | — | See below |

## Product lookup (Open Food Facts)

When — and only when — you enable product lookup in Settings and scan a
product code (EAN/UPC/GTIN), the app requests that code's name, brand and
nutritional summary from the **Open Food Facts** API
(https://world.openfoodfacts.org). Only the barcode number is sent. No
identifiers, no history, no device information is attached to the request.

Open Food Facts is a non-profit collaborative database and applies its own
privacy policy: https://world.openfoodfacts.org/privacy

## What is explicitly absent

- No advertising, no ad SDKs.
- No analytics, no telemetry, no crash reporting.
- No accounts, no registration, no user identifiers.
- No third-party trackers.
- Camera frames and gallery images are processed entirely on the device.

## Permissions and why they are requested

- **Camera** — to read codes through the viewfinder.
- **UserDirs** — to read images you pick and to write exports you request.
- **Internet** — only for optional product lookup.
- **Audio** — carried by the standard camera integration on Sailfish OS.

## Children

The app is a general-purpose utility and collects no personal information
at all, so it is equally unusable for profiling anyone, including children.

## Changes

Any future change to this policy will be published in the project
repository (https://github.com/kapitaali/harbour-zendecoder) together with
the release that introduces it. The date at the top of this file always
reflects the last change.

## Contact

Questions? Reach the developer on Ko-fi: https://ko-fi.com/kapitaali
