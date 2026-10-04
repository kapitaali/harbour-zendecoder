#pragma once
// Symbology identifiers. Values are stable (persisted in bitmasks/logs).
// NOTE: the C API mask is 32-bit and covers values 0..31 (Grade-1 set).
// The C++ Options mask is 64-bit and covers all values below.
#include <cstdint>
#include "omniscan/export.h"

namespace omniscan {

enum class Symbology : uint8_t {
    QRCode = 0,
    MicroQRCode = 1,
    DataMatrix = 2,
    Aztec = 3,
    PDF417 = 4,
    Code128 = 5,
    Code39 = 6,
    Code93 = 7,
    Codabar = 8,
    ITF = 9,            // Interleaved 2 of 5
    UPCA = 10,
    UPCE = 11,
    EAN8 = 12,
    EAN13 = 13,
    // --- Grade-2 (values 14..63 reserved) ---
    DataBar = 14,         // GS1 DataBar (all variants; expanded flagged via text prefix)
    DataBarExpanded = 15,
    MSI = 16,
    Plessey = 17,
    Telepen = 18,
    Pharmacode = 19,
    CodablockF = 20,
    Code16K = 21,
    Code49 = 22,
    DotCode = 23,
    HanXin = 24,
    GridMatrix = 25,
    SwissQR = 26,         // QR variant with defined payload schema
    MaxiCode = 27,
    MicroPDF417 = 28,
    USPSIMb = 29,
    RM4SCC = 30,
    AustraliaPost = 31,
    JapanPost = 32,
    DeutschePost = 33,    // Identcode / Leitcode
    KIX = 34,
    GS1Composite = 35,
    RmQR = 36,            // rMQR (ISO/IEC 23958), decoded via backend when available
    Unknown = 255
};

constexpr int kSymbologyCount = 37; // number of real (non-Unknown) entries above

// Bitmask helpers (64-bit for C++).
using SymMask = uint64_t;
constexpr SymMask symbology_bit(Symbology s) {
    return (s == Symbology::Unknown) ? 0ULL
         : (static_cast<unsigned>(s) >= 64 ? 0ULL
            : (1ULL << static_cast<unsigned>(s)));
}

constexpr SymMask kMaskGrade1 =
    symbology_bit(Symbology::QRCode) | symbology_bit(Symbology::MicroQRCode) |
    symbology_bit(Symbology::DataMatrix) | symbology_bit(Symbology::Aztec) |
    symbology_bit(Symbology::PDF417) | symbology_bit(Symbology::Code128) |
    symbology_bit(Symbology::Code39) | symbology_bit(Symbology::Code93) |
    symbology_bit(Symbology::Codabar) | symbology_bit(Symbology::ITF) |
    symbology_bit(Symbology::UPCA) | symbology_bit(Symbology::UPCE) |
    symbology_bit(Symbology::EAN8) | symbology_bit(Symbology::EAN13);

constexpr SymMask kMaskAll = ~0ULL;

// Human-readable name. Never returns nullptr.
OMNISCAN_API const char* to_string(Symbology s) noexcept;

// Parse name (case-insensitive, ignores '-', '_', ' '). Returns Unknown if no match.
OMNISCAN_API Symbology symbology_from_string(const char* name) noexcept;

} // namespace omniscan
