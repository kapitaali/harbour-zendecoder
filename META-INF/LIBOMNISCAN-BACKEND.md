
Prompt: make libomniscan the decode backend of harbour-zendecoder
Goal
Replace the vendored zxing-cpp v3.0.2 in ~/Jolla/harbour-zendecoder with ~/Jolla/libomniscan (v0.2.0, Apache-2.0) as the sole on-device decode engine, keeping the app's UI, scan flow, history, and QR D-Bus fallback behavior identical. A barcode that decodes today must decode after the swap; formats the lib additionally supports natively (postal 4-state codes, MSI/Plessey/Telepen/Pharmacode, DataBar/MaxiCode via its backend) become available through the app's existing format-group settings.
Non-goals
- No UI/QML redesign. No changes to scan UX, history schema, trial/licensing, or product lookup.
- Do not fork libomniscan. If the integration needs something from the lib (e.g. a wider C API mask, a pkg-config file), implement it in the lib repo following its conventions (per-format docs-before-code, tests, changelog) and say so explicitly — do not carry a private patch in the app.
- Do not regress Harbour store compliance (allowed modules only, licensing clean).
Starting state — read before touching anything
App (~/Jolla/harbour-zendecoder, qmake, CONFIG += sailfishapp, C++20, Qt 5.6-era APIs):
- src/decoder.{h,cpp} — the pipeline contract. Still captures → file → submitImageFile(); decodeFile() for gallery (never deletes). One decode in flight (m_busy); m_oneShot = gallery imports surface notFound(); live loop stays silent on misses. Preserve these semantics exactly.
- src/staticdecoder.* — worker thread wrapping the vendored zxing-cpp v3.0.2 (3rdparty/, readers-only static build). This is the code being replaced.
- src/formatgroups.h — the app's format-group settings; the new backend's symbology mask must be driven by these, not hardcoded.
- Second backend org.amberapi.zxing over Qt5DBus (QR-only fallback). Keep it as insurance; keep the "static decoded" vs "service decoded" log distinction.
- rpm/harbour-zendecoder.spec — current packaging.
Library (~/Jolla/libomniscan, commit b52ab7a):
- README.md "Usage (C++)", include/omniscan/omniscan.h (decode_into, Options, 64-bit enabled_symbologies, try_harder, max_symbols), docs/abi.md, docs/support_matrix.md (what each format needs and costs).
- Thread-safe, deterministic, no exceptions across the API. Returns Ok with zero results on a miss — a miss is data, not an error.
- Orientation: 90°-rotated symbols decode at default settings; do not add manual rotation.
- omniscan_decode C API exists but its mask covers Grade-1 only — use the C++ API so native Tier-2 formats are reachable.
Step 0 — baseline (no behavior changes)
Build the app unmodified with the Sailfish SDK (sfdk, aarch64 target) and record: build log, binary size, and decode results on a fixed fixture set (at least one image each of QR, EAN-13, Code 128, plus one image with no code). This fixture set is the regression gate for everything below.
Step 1 — resolve the zxing collision (decision required, then execute)
The app statically links zxing-cpp 3.0.2; the lib fetches 2.3.0. Both define ZXing::* symbols — linking both into one binary is an ODR violation. Recommended resolution: delete the vendored 3rdparty/ copy entirely and build libomniscan with its backend ON as the single decoder. Verify with nm that no duplicate ZXing:: symbols remain in the final link. If you choose otherwise (e.g. lib with backend OFF + keep vendored 3.0.2), justify it in writing — you then keep two barcode stacks and must explain why.
Step 2 — consume the library
Constraints: the app is qmake (the lib is CMake, no .pc file — link via INCLUDEPATH/LIBS against an installed or in-tree build, not via find_package); store/OBS builders may lack network, so FetchContent downloading zxing at configure time is a build-break risk — pre-seed it (FETCHCONTENT_SOURCE_DIR_ZXING_CPP) or vendor the 2.3.0 tarball with hash check. Decide: (a) separate libomniscan RPM + Requires (clean, needs store acceptance of the dependency), or (b) compile the lib's sources into the app binary (single RPM, Apache-2.0 permits it with attribution, matches how the app already ships vendored code). Recommend (b) unless you confirm (a) passes Harbour validation — and say which you picked.
Step 3 — swap the decode call, keep the architecture
- Keep StaticDecoder's worker-thread structure; replace only its decode body with decode_into on a grayscale frame: QImage::convertToFormat(Format_Grayscale8) → bits()/bytesPerLine() → ImageView. No copies beyond that conversion; respect stride.
- Policy: decode at default settings first; escalate to try_harder on a miss only (it costs extra scan passes — see support_matrix.md for per-format costs). max_symbols small (1–4) for the live loop.
- Build a symbology-name mapping table: omniscan::to_string(symbology) → the names the app displays and stores in history today (EAN-13, Code 128, …). Any name the app shows that has no lib equivalent is a gap — report it, don't silently rename.
- Error contract: Ok+empty = miss (silent live, notFound() one-shot); BackendNotAvailable (Grade-1 bits with backend OFF) must fall through to the D-Bus path exactly like today's static-miss path; anything else → today's abort path.
Step 4 — acceptance (all must pass before you call it done)
1. sfdk build clean for the aarch64 target, no new warnings, nm-verified single zxing.
2. Fixture set from Step 0: every previously-decoding image decodes with identical text; the no-code image stays silent; symbology labels match or are covered by your mapping table with justification per rename.
3. Extended fixtures: one postal-code image (e.g. KIX/RM4SCC — newly available natively) and one 90°-rotated barcode decode correctly.
4. Live-loop sanity: no decode-thread stalls, m_busy never sticks, gallery imports still surface "no code found".
5. License check: grep -ri "general public license" over everything linked into the binary returns empty; libomniscan LICENSE attribution present in the RPM/about page.
6. Report: what you changed per file, the Step-1/Step-2 decisions with reasons, measured binary-size delta, and any lib-side gaps you hit (do not paper over them).
If any acceptance item fails and the fix belongs in the library rather than the app, stop, describe the proposed lib-side change with evidence, and ask before implementing it there.
