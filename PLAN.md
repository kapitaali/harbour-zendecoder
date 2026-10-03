# harbour-zendecoder — PLAN.md

Code reader app for Sailfish OS (Jolla). Supports 1D / 2D barcodes and other camera-readable code formats.

Source input: `~/Jolla/harbour-zendecoder/planning.txt` (generic iOS/Android + ML Kit plan — does NOT apply to Sailfish, used only for format list reference).

Current repo state: only `planning.txt` exists. Greenfield app.

## 1. Locked decisions

1. Build system: **qmake** (`harbour-zendecoder.pro`).
2. Format scope: **full set** — the more the merrier (all `zxing-cpp` supported formats, ON by default).
3. Features v1: **gallery import + history export + product lookup** — all IN.
4. Monetization: **Ko-fi Shop** for Pro (digital asset / license key). No ads, no subscription.
5. Trial/licensing: build-time switch design. **Seed builds ship as Pro with full functionality.** Future flip enforces 14-day trial + license key.
6. Harbour policy on Ko-fi links: **unknown**. Owner has 2 other Harbour apps submitted with Ko-fi links, no feedback yet. Design must keep Ko-fi URL removable in 1 line.

## 2. Stack

* Sailfish Silica QML + Qt/C++ (Qt 5.x baseline per Sailfish SDK).
* `QT += qml quick multimedia sql concurrent network`
* Decoder: **zxing-cpp** (Apache-2.0, active, C++). Built from vendored source, linked as static lib via `INCLUDEPATH` + `LIBS` from qmake.
* No ML Kit / Apple Vision / Scandit. All decoding on-device.
* Biggest spike first: verify qmake + zxing-cpp (CMake upstream) builds for `aarch64` / `armv7hl` in Sailfish SDK + OBS. If Jolla Store build service blocks this, fall back to Chum-only + Store free build.

## 3. Format scope

Enable all zxing-cpp formats by default, user-togglable in Settings (fewer enabled = faster, fewer false positives):

`EAN-13/8 (+2/5 addons), UPC-A/E, DataBar Omni/Stacked/Limited/Expanded, Code39 (+VIN/Code32/PZN variants), Code93, Code128, ITF-14 (+DHL Leitcode/Identcode), Telepen, Codabar, QR (Model 1/2, Micro, rMQR), DataMatrix ECC200, Aztec (+Rune), PDF417 (+Compact/MicroPDF417), MaxiCode (partial), DXFilmEdge`

Explicit v1 non-goals (no open C++ lib, document as unsupported):
4-state postal (`USPS IMb, RM4SCC, AU/JP Post, KIX`), `MSI, Pharmacode, Plessey, Han Xin, Grid Matrix, DotCode, Codablock, Code 16K, Code 49, GS1 Composite`, all color codes (`HCCB, Ultracode, ColorCode, ShotCode, CrontoSign, HueCode, PM/CL Code, SpectraCode, MMCC, CQR-9, HCC2D`), `Softstrip`.

Structured payload parsing v1: `URL, WIFI, VCARD/MECARD, GEO, SMS, mailto, tel`. Swiss QR treated as plain text.

## 4. Architecture / layout

```
harbour-zendecoder/
  harbour-zendecoder.pro
  src/main.cpp
  src/decoder.{cpp,h}        # QObject wrapper around ZXing::ReadBarcodes
  src/framegrabber.{cpp,h}   # QAbstractVideoFilter -> grayscale QImage -> Decoder
  src/history.{cpp,h}        # QAbstractListModel + QtSql SQLite store
  src/trialmanager.{cpp,h}   # seed/trial/key logic (see §7)
  src/productlookup.{cpp,h}  # QNetworkAccessManager -> Open Food Facts, cached
  qml/harbour-zendecoder.qml
  qml/pages/ScannerPage.qml
  qml/pages/ResultPage.qml
  qml/pages/HistoryPage.qml
  qml/pages/SettingsPage.qml
  qml/pages/AboutPage.qml
  rpm/*.yaml, *.spec
  translations/*.ts
  icons/86x86,150x150,172x172 + harbour-zendecoder.desktop
```

* `Decoder`: `decodeImage(QImage) -> {format, text, points}`. Shared by live + still-image paths. `ReaderOptions().formats(...)` from Settings allow-list.
* `FrameGrabber`: `Camera + VideoOutput { filters: [decoderFilter] }`, throttle every 3rd–5th frame, crop to viewfinder ROI before decode, 1080p min, continuous autofocus if available, release camera on background.
* `History`: SQLite in `~/.local/share/harbour-zendecoder/`, fields `timestamp/format/value/productName/thumbnailPath`. Export CSV/JSON to `DocumentsLocation` + Sailfish Share. Copy/delete/clear.
* Settings persist via `QSettings` (`harbour-zendecoder`) or `Nemo.Configuration`.

## 5. Core scanning (Phase 1)

* `ScannerPage` = default page. ROI rectangle + bounding-box overlay from decoder points.
* Torch toggle, beep + vibration on success, manual-entry fallback always visible.
* Symbology filter UI (all ON by default).

Acceptance: time-to-first-scan <2s on Jolla C2 for QR/EAN in normal indoor light; no crash on background/foreground cycling.

## 6. Gallery import / export + result actions (Phase 2)

* Import: `Sailfish.Pickers ImagePickerPage` -> `QImage` -> same `Decoder`. Covers screenshots/saved tickets. Later: batch multi-pick = Pro-gated.
* Result actions per type: open URL (external browser), connect WiFi, save contact, copy to clipboard, share.
* History list/detail/delete/clear + CSV/JSON export.

Pro gating (enforced only after seed switch flips, see §7):
Free forever: live QR + EAN/UPC scan, basic copy/open.
Pro: full format set beyond QR/EAN, gallery batch, export, product-lookup cache. Seed builds: everything unlocked.

## 7. Trial / licensing / monetization (Ko-fi)

Design: build-time switch + runtime trial. No server, offline validation (Harbour-safe).

```qmake
# seed builds (current):
DEFINES += PRO_SEED_BUILD
# future monetized builds: remove the line
```

`TrialManager` API:
* `firstRun: QDateTime` stored in `QSettings` on first launch.
* `trialDaysLeft() = clamp(14 - firstRun.daysTo(now), 0, 14)`; if `now < firstRun` treat as tampered -> 0.
* `hasValidKey(): bool` — v1: compare against single shared `ZEN-PRO-XXXX` secret (obfuscated, not QML-plaintext). v2 path: Ed25519 per-email signed keys, embed public key only + offline Python keygen script.
* `isPro() = PRO_SEED_BUILD || hasValidKey() || trialDaysLeft() > 0`
* `mode: enum {SeedPro, Trial, ProUnlocked, Free}` for QML.

UX (`SettingsPage` + `AboutPage`):
* Status label: `Pro preview` (seed) / `Trial: X days left` / `Pro unlocked` / `Free mode`.
* `KOFI_URL` single constant (C++ + QML). Buttons: `Get Pro on Ko-fi`, key entry field (present but pre-unlocked in seed).
* Post-expiry behavior (future): fall back to free, remorse popup `Trial ended`, never block basic scanning.

Ko-fi setup:
* Option A now (zero license code risk): sell Pro RPM as digital product. Manual install, no auto-update.
* Option B next (recommended): sell key string/file on Ko-fi, one app everywhere. Chosen architecture supports B without UI change.
* Reinstall resets trial (Sailfish wipes app config on uninstall) — accepted for v1.

Harbour risk: external payment links may be rejected. Mitigation: Store build keeps trial/key UI but `KOFI_URL` empty (shows `Pro coming soon`); Chum/OpenRepos build sets `KOFI_URL` to shop link. One-line change.

## 8. Product lookup (Phase 2/3)

* Trigger only for `EAN-13/8, UPC-A/E`, only when enabled + network available.
* `QNetworkAccessManager` -> Open Food Facts `product/<barcode>.json`, parse `product_name/brands/image_url`, cache in history DB. Custom User-Agent, no tracking upload.
* Privacy policy + store listing must state: decoding on-device, network only for opt-in product lookup.

## 9. Packaging / release

* `.desktop`, `rpm/*.yaml + spec`, icons 86/150/172px, `lupdate/lrelease` translations (EN + FI/DE minimum).
* Targets: Chum/OpenRepos first (fast iteration + Ko-fi Pro), Jolla Store second (free/trial, `KOFI_URL` stripped if reviewer objects).
* License notices: zxing-cpp Apache-2.0 attribution in AboutPage + RPM.

## 10. QA

* SDK emulator + real Jolla C2 / Xperia 10 II/III.
* Matrix: bright sunlight / dim indoor / glare / curved / reflective / damaged codes.
* Metrics: time-to-first-scan, battery drain during continuous scan, false-positive rate with full set vs subset.
* `harbour-rpmvalidator`, Harbour QA checklist.

## 11. Build order

0. Scaffold qmake project + RPM + icons + empty Silica pages + `KOFI_URL` stub. Spike: zxing-cpp static link + `QAbstractVideoFilter` frame grab.
1. Live scan for full set + overlay + beep/vibrate + torch.
2. Result actions + gallery import + manual entry + history + export.
3. Settings (format toggles, torch/beep/vibrate, lookup on/off) + `TrialManager` in seed mode + About/donate.
4. Product lookup + caching.
5. Translations, icons, validators, Chum release, then Store submission.
6. Later flip: remove `PRO_SEED_BUILD`, enforce 14-day trial, issue Ko-fi keys (v1 shared key -> v2 signed keys).

## 12. Open risks

* qmake + CMake-based zxing-cpp vendoring on OBS / Store builders.
* `QVideoProbe` vs `QAbstractVideoFilter` backend differences on Sailfish devices.
* Harbour rejection of Ko-fi links (precedent pending on owner's 2 other apps).
* Full-set decode speed/battery on low-end devices — mitigate with format allow-list + frame throttle + ROI.
