// Status strings.
#include "omniscan/result.h"

namespace omniscan {

const char* to_string(DecodeStatus s) noexcept {
    switch (s) {
        case DecodeStatus::Ok: return "Ok";
        case DecodeStatus::NoBarcodeFound: return "NoBarcodeFound";
        case DecodeStatus::InvalidImage: return "InvalidImage";
        case DecodeStatus::InvalidArgument: return "InvalidArgument";
        case DecodeStatus::UnsupportedSymbology: return "UnsupportedSymbology";
        case DecodeStatus::BackendNotAvailable: return "BackendNotAvailable";
        case DecodeStatus::BackendError: return "BackendError";
    }
    return "Unknown";
}

} // namespace omniscan
