# AGENTS.md — harbour-zendecoder (ZenDecoder)

Instructions for any agent working in this repository. Read this first, then `README.md`.

## 1. What this repo is

ZenDecoder is a native Sailfish OS barcode and QR scanner. It decodes all common 1D/2D codes on-device (zxing-cpp v3.0.2 vendored as a static library, readers-only), with optional product/book lookup via open databases. qmake/Sailfish, built with sfdk.

## 2. The one command that decides whether you are done

```
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build
```

Builds the RPM. There is no automated test suite in this repo; verification is manual on device/emulator plus the Harbour static validator:

```
sfdk -c device= check -s harbour RPMS/SailfishOS-5.1.0.11-aarch64/harbour-zendecoder-*.aarch64.rpm
```

## 3. Layout

```
src/            C++ backend (decoder, history, settings, product lookup)
qml/            Silica UI pages
3rdparty/       vendored zxing-cpp v3.0.2 (static, readers-only)
docs/           screenshots + icon
rpm/            Harbour spec + changelog
PRIVACY.md      privacy policy
```

## 4. Laws. Breaking one is a bug even if the tests pass.

1. **Clean before switching targets.** `sfdk build` compiles in place; switching arch without cleaning links wrong objects.
2. **Harbour rules.** `Exec=harbour-zendecoder`; no `%post`/`%postun` scriptlets; `License: GPL-3.0-only AND Apache-2.0 AND BSD-3-Clause` (app GPL, vendored zxing-cpp Apache-2.0/BSD-3-Clause); minimal `[X-Sailjail]` permissions (`Audio;Camera;UserDirs;Internet`).
3. **Vendored library is read-only.** `3rdparty/zxing-cpp/` is a pinned static build; do not edit it. Re-vendor via the documented process only.
4. **Privacy.** All decoding happens on-device; frames never leave it. The only network traffic is the optional product lookup (sends the scanned numeric code only, off by default, one fetch per code per day).
5. **No secrets in committed files.**

## 5. Verification culture

- Build on aarch64 and armv7hl (and i486 for emulator); verify binary arch with `file`.
- Run the Harbour static validator on both architecture RPMs.
- Prefer measured numbers over adjectives. No marketing language.

## 6. Recording work

- Session notes in `META-INF/` (if present).

## 7. Git and secrets

- Never push. The owner pushes.
- Never commit credentials, tokens, `.env` files, or build artifacts (`RPMS/`, `*.o`, `moc_*.cpp`, `Makefile`, `.qmake.stash`).
- Commits are attributed `kapitaali <teppo@kapitaali.com>`.
- Do not stage another agent's work in progress — check `git status` and `git log --oneline -5` first.

## 8. Test commands

```
# build (phone)
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build
# build (emulator)
sfdk -c target=SailfishOS-5.1.0.11-i486 build
# Harbour validator
sfdk -c device= check -s harbour RPMS/SailfishOS-5.1.0.11-aarch64/harbour-zendecoder-*.aarch64.rpm
```

## 9. Current state

- Vendored zxing-cpp v3.0.2 (readers-only, no runtime dependencies); live-QR D-Bus fallback to system `org.amberapi.zxing` daemon.
- Product lookup: Open Food Facts/Beauty/Pet Food/Products Facts + barcode index for EAN/UPC, Open Library for ISBNs.
