#pragma once
/// @file
/// Symbology identifiers and bitmask helpers.
///
/// Values are stable (persisted in bitmasks/logs).
/// NOTE: the C API mask is 32-bit and covers values 0..31 (Grade-1 set).
/// The C++ Options mask is 64-bit and covers all values below.
#include <cstdint>
#include "omniscan/export.h"

namespace omniscan {

/// Symbology identifiers. Numerical values are stable API: they appear
/// in masks, logs and persisted settings — never renumber them.
enum class Symbology : uint8_t {
    QRCode = 0,          ///< QR Code
    MicroQRCode = 1,     ///< MicroQR
    DataMatrix = 2,      ///< Data Matrix ECC200
    Aztec = 3,           ///< Aztec Code
    PDF417 = 4,          ///< PDF417
    Code128 = 5,         ///< Code 128
    Code39 = 6,          ///< Code 39
    Code93 = 7,          ///< Code 93
    Codabar = 8,         ///< Codabar (NW-RX)
    ITF = 9,             ///< Interleaved 2 of 5
    UPCA = 10,           ///< UPC-A
    UPCE = 11,           ///< UPC-E
    EAN8 = 12,           ///< EAN/UPC 8
    EAN13 = 13,          ///< EAN/UPC 13
    // --- Grade-2 (values 14..63 reserved) ---
    DataBar = 14,        ///< GS1 DataBar (all variants; expanded flagged via text prefix)
    DataBarExpanded = 15,///< GS1 DataBar Expanded (variant hint)
    MSI = 16,            ///< MSI / MSI Plessey
    Plessey = 17,        ///< Plessey
    Telepen = 18,        ///< Telepen
    Pharmacode = 19,     ///< Laetus Pharmacode (one- and two-track)
    CodablockF = 20,     ///< Codablock F
    Code16K = 21,        ///< Code 16K
    Code49 = 22,         ///< Code 49 (native decoder)
    DotCode = 23,        ///< DotCode (SPEC-BLOCKED)
    HanXin = 24,         ///< Han Xin Code (SPEC-BLOCKED)
    GridMatrix = 25,     ///< Grid Matrix (native decoder)
    SwissQR = 26,        ///< QR variant with defined payload schema
    MaxiCode = 27,       ///< MaxiCode (modes 2-6)
    MicroPDF417 = 28,    ///< MicroPDF417
    USPSIMb = 29,        ///< USPS Intelligent Mail (IMb)
    RM4SCC = 30,         ///< Royal Mail 4-State Customer Code
    AustraliaPost = 31,  ///< Australia Post 4-State
    JapanPost = 32,      ///< Japan Post 4-State (Kanji)
    DeutschePost = 33,   ///< Identcode / Leitcode
    KIX = 34,            ///< PostNL KIX
    GS1Composite = 35,   ///< GS1 Composite (SPEC-BLOCKED)
    RmQR = 36,           ///< rMQR (ISO/IEC 23958), decoded via backend when available
    Unknown = 255        ///< not a symbology; masks to 0
};

/// Number of real (non-Unknown) entries above.
constexpr int kSymbologyCount = 37;

// Bitmask helpers (64-bit for C++).
/// Bitmask type for Options::enabled_symbologies.
using SymMask = uint64_t;

/// Bit for one symbology; 0 for Unknown and out-of-range values.
constexpr SymMask symbology_bit(Symbology s) {
    return (s == Symbology::Unknown) ? 0ULL
         : (static_cast<unsigned>(s) >= 64 ? 0ULL
            : (1ULL << static_cast<unsigned>(s)));
}

/// The Grade-1 set: what the optional zxing-cpp backend covers.
constexpr SymMask kMaskGrade1 =
    symbology_bit(Symbology::QRCode) | symbology_bit(Symbology::MicroQRCode) |
    symbology_bit(Symbology::DataMatrix) | symbology_bit(Symbology::Aztec) |
    symbology_bit(Symbology::PDF417) | symbology_bit(Symbology::Code128) |
    symbology_bit(Symbology::Code39) | symbology_bit(Symbology::Code93) |
    symbology_bit(Symbology::Codabar) | symbology_bit(Symbology::ITF) |
    symbology_bit(Symbology::UPCA) | symbology_bit(Symbology::UPCE) |
    symbology_bit(Symbology::EAN8) | symbology_bit(Symbology::EAN13);

/// All symbologies (default Options mask).
constexpr SymMask kMaskAll = ~0ULL;

/// Human-readable name. Never returns nullptr; "Unknown" for bad values.
OMNISCAN_API const char* to_string(Symbology s) noexcept;

/// Parse name (case-insensitive, ignores '-', '_', ' '). Returns Unknown if no match.
OMNISCAN_API Symbology symbology_from_string(const char* name) noexcept;

} // namespace omniscan
