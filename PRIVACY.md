# ZenDecoder — Privacy Policy

**Last updated: 2026-10-09**

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

## Product lookup

When — and only when — you enable product lookup in Settings and scan a
code the app can look up, a single GET request is sent containing only the
barcode number (or, for GS1 element strings, the GTIN extracted from it).
No identifiers, no history, no device information is attached. Depending on
the code:

| Code | Endpoint |
|------|----------|
| EAN/UPC/GTIN | Chained, first match wins: **Open Food Facts** → **Open Beauty Facts** → **Open Pet Food Facts** → **Open Products Facts** (all https://world.open*.org, same API), then **UPCitemdb** (https://upcitemdb.com, commercial index, keyless trial tier capped by us at 100 lookups/day) |
| ISBN (books) | Open Library (https://openlibrary.org) |
| GS1 element string | its (01) GTIN, through the chain above |

Open Food Facts and its sibling projects are non-profit collaborative
databases and apply their own privacy policy:
https://world.openfoodfacts.org/privacy. UPCitemdb is a commercial
service with its own terms; it is only reached as the last step, for
codes none of the open databases know.

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
