# PLAN-nfc.md — NFC tag reading (planned, v0.3)

**Status: PLANNED — not implemented.** Gate: finish the 0.2.0 Harbour
submission first; ideally wait for the verdict on zensors (it already
declares the same `NFC` permission, so it is our Harbour precedent for
free).

Goal in one line: an NFC tag is just another code — read its NDEF
payload and push it through the existing ResultPage pipeline (open URL,
save vCard, join Wi-Fi, copy/share, history entry). No new action types
needed for v1 of this feature.

## 1. Facts we already established (from `~/Jolla/docs/data-sources.md` §7)

- Sailjail permission **`NFC` exists and is on the Harbour-allowed list**
  (Allowed_Permissions: Audio, Bluetooth, Camera, Internet, Location,
  MediaIndexing, Microphone, **NFC**, RemovableMedia, UserDirs, …).
- The permission grants D-Bus access to **`org.sailfishos.nfc.daemon`**
  and **`org.neard`**: tag discovery events, reader/poll state, settings.
  Presence arrives as an event/counter, not a continuous meter.
- **`org.sailfishos.system_nfc` is on the bus but unreachable to
  sandboxed apps** — never target it.
- No Qt NFC module in the target (same situation as Bluetooth) ⇒ plain
  `QDBus`; `QT += dbus` is already in the `.pro` and we already speak
  D-Bus for the zxing live-QR fallback.
- Emulator shows the bus name but the adapter column is `?` — only a
  real Jolla Phone 2026 run can confirm hardware.
- Upstream contract: `github.com/sailfishos/nfcd` — reader/writer for
  ISO-DEP and Type 2 tags; its D-Bus interface is the API we code to.
- `harbour-zensors.desktop` already declares `Permissions=…;NFC`
  (unused) — copy that line's shape.

## 2. Phase 0 — spike (hard gate, half a day, needs the owner + a tag)

Nothing below is implemented until these answer yes:

1. Add `NFC` to `Permissions=` in a throwaway dev build, install on the
   phone, accept the consent prompt, then confirm from inside the app's
   sandbox that `org.sailfishos.nfc.daemon` / `org.neard` are visible and
   reachable (introspect their interfaces; the daemon, not
   `system_nfc`). Fetch output to the host — no inline quoting games over
   ssh (established lesson).
2. Physical check on the Jolla Phone 2026: does it poll and deliver a
   Type 2 tag (NTAG213 sticker) as `defaultuser`? Need at least one real
   tag — order NTAG213/215 stickers if we have none.
3. Record the interface surface (objects, methods, signals, tag
   properties, NDEF record shape) in `docs/nfc-sources.md`, same style as
   `data-sources.md`.

**Kill criteria:** daemon unreachable from the sandbox despite the
permission, or no NFC adapter on JP2026 → document the negative result,
drop the feature, no code ships.

## 3. Design (assumes Phase 0 passes)

* `src/nfcclient.{h,cpp}` — QObject owning the `QDBusInterface` to the
  daemon; exposes `active: bool`, `start()/stop()`, and one signal
  `tagRead(QString text, QString kind)`.
  * NDEF → text: RTD_TEXT (with language code), RTD_URI (URI-prefix
    table), Smart Poster (unwrap to its URI), everything else → best
    effort raw text. Parsing is pure and unit-testable (bytes in,
    text out).
  * Payload arrives as plain text → existing ResultPage logic takes it
    from there (`isUrl`, vCard/Wi-Fi sniff, manual=false so lookup still
    fires when the text is a GTIN).
* **UI:** listen only while ScannerPage is active; a small
  "Hold near a tag" hint under the viewfinder. Presentation is
  event-driven (tag appeared), so the camera keeps working untouched —
  no camera/NFC interaction in v1.
* **Settings:** new `NFC reading` switch, `settings.nfcEnabled`,
  default ON. Turning it off stops listening (and avoids the bus talk).
* **History:** format label `NFC URI` / `NFC Text` / `NFC vCard` (a
  string like any other symbology label), value = payload. Debounce
  re-presentations of the same tag for ~3 s so holding a tag still does
  not spam the list (same spirit as live-decode duplicate suppression).
* **Lifecycle:** stop listening when the app goes inactive — same hook
  the camera release already uses (`application inactive`), so battery
  behaviour stays "nothing runs in the background".
* **Pro gating:** none — core capability, available in trial/free
  (consistent with live scanning being free).
* **Permissions:** `Permissions=Audio;Camera;NFC;UserDirs;Internet`.
  Adding a permission changes the first-run consent prompt — note it in
  the release notes and expect it in QA.
* **Battery:** polling is daemon-side while we listen; we never poll
  ourselves when idle/backgrounded.

## 4. Implementation order (after Phase 0)

1. `NFC` permission in the `.desktop` + dev build + consent-prompt check.
2. `NfcClient` D-Bus wrapper + NDEF→text parser (host-testable).
3. QML wiring: scanner hint, tag event → ResultPage + history write,
   debounce.
4. Settings switch + start/stop tied to page active state.
5. Docs: PRIVACY.md (new row "NFC tags | read on-device | No"), store
   listing bullet, permissions list in the privacy table.
6. `sfdk check` + 3-arch build + device QA.
7. Screenshots: **04-settings.png must be re-shot** (new switch), and
   optionally the scanner hint → owner-shot, re-upscaled to 1080×2378.

## 5. QA matrix (device)

- NTAG213 URL tag → ResultPage with open-link, ≤1 s from presentation.
- Text tag → shown as text, history row `NFC Text`.
- Tag containing a vCard/Wi-Fi string → existing actions appear.
- Payment card / transit card presented → no crash, no bogus history row
  (ISO-DEP may be readable; must degrade to "unsupported tag" not spam).
- Camera scan and tag read at the same time → camera unaffected.
- Background/foreground cycling → polling stops/starts, log confirms.
- Settings switch off → no D-Bus traffic, no toast.
- Emulator → feature hides itself (no adapter), no crash.
- Validator green on aarch64/armv7hl/i486.

## 6. Risks

* Sandbox reaches the daemon but tag delivery needs extra setup we don't
  know about yet → Phase 0 exists to find this out.
* No NFC hardware on JP2026 → feature is dead on our only test device
  (kill criterion above).
* Harbour: `NFC` is allowed-listed, but our own precedent (zensors) is
  still in review — do not rely on it until it ships.
* nfcd covers Type 2 + ISO-DEP only → some Type 4 tags/NTAG variants may
  not read; document what works instead of promising "all tags".
* Consent-prompt change on an already-published app can draw reviewer
  questions — include a line in the submission notes when 0.3 goes in.

## 7. Estimate

Phase 0: ~0.5 day (incl. owner + tags). Implementation: 1–1.5 days.
QA + docs + 3-arch rebuild + screenshot re-shoot: ~0.5 day.
**Total ~2–3 days, entirely after 0.2.0 is submitted and gated on
Phase 0 passing.**
