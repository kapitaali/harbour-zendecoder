// Symbology names + parsing. No dependencies beyond headers.
#include "omniscan/symbology.h"
#include <cctype>
#include <cstring>

namespace omniscan {

const char* to_string(Symbology s) noexcept {
    switch (s) {
        case Symbology::QRCode: return "QRCode";
        case Symbology::MicroQRCode: return "MicroQRCode";
        case Symbology::DataMatrix: return "DataMatrix";
        case Symbology::Aztec: return "Aztec";
        case Symbology::PDF417: return "PDF417";
        case Symbology::Code128: return "Code128";
        case Symbology::Code39: return "Code39";
        case Symbology::Code93: return "Code93";
        case Symbology::Codabar: return "Codabar";
        case Symbology::ITF: return "ITF";
        case Symbology::UPCA: return "UPC-A";
        case Symbology::UPCE: return "UPC-E";
        case Symbology::EAN8: return "EAN-8";
        case Symbology::EAN13: return "EAN-13";
        case Symbology::DataBar: return "DataBar";
        case Symbology::DataBarExpanded: return "DataBarExpanded";
        case Symbology::MSI: return "MSI";
        case Symbology::Plessey: return "Plessey";
        case Symbology::Telepen: return "Telepen";
        case Symbology::Pharmacode: return "Pharmacode";
        case Symbology::CodablockF: return "CodablockF";
        case Symbology::Code16K: return "Code16K";
        case Symbology::Code49: return "Code49";
        case Symbology::DotCode: return "DotCode";
        case Symbology::HanXin: return "HanXin";
        case Symbology::GridMatrix: return "GridMatrix";
        case Symbology::SwissQR: return "SwissQR";
        case Symbology::MaxiCode: return "MaxiCode";
        case Symbology::MicroPDF417: return "MicroPDF417";
        case Symbology::USPSIMb: return "USPS-IMb";
        case Symbology::RM4SCC: return "RM4SCC";
        case Symbology::AustraliaPost: return "AustraliaPost";
        case Symbology::JapanPost: return "JapanPost";
        case Symbology::DeutschePost: return "DeutschePost";
        case Symbology::KIX: return "KIX";
        case Symbology::GS1Composite: return "GS1Composite";
        case Symbology::RmQR: return "rMQR";
        case Symbology::Unknown: return "Unknown";
    }
    return "Unknown";
}

// Normalize: lowercase alnum only.
static void normalize(const char* in, char* out, size_t cap) noexcept {
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 1 < cap; ++i) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (std::isalnum(c)) out[j++] = static_cast<char>(std::tolower(c));
    }
    out[j] = '\0';
}

Symbology symbology_from_string(const char* name) noexcept {
    if (!name) return Symbology::Unknown;
    char n[32];
    normalize(name, n, sizeof n);
    struct Entry { const char* key; Symbology s; };
    static const Entry table[] = {
        {"qrcode", Symbology::QRCode}, {"qr", Symbology::QRCode},
        {"microqrcode", Symbology::MicroQRCode}, {"microqr", Symbology::MicroQRCode},
        {"rmqr", Symbology::RmQR},
        {"datamatrix", Symbology::DataMatrix},
        {"aztec", Symbology::Aztec},
        {"pdf417", Symbology::PDF417},
        {"code128", Symbology::Code128},
        {"code39", Symbology::Code39},
        {"code93", Symbology::Code93},
        {"codabar", Symbology::Codabar},
        {"itf", Symbology::ITF}, {"interleaved2of5", Symbology::ITF},
        {"itf14", Symbology::ITF},
        {"upca", Symbology::UPCA},
        {"upce", Symbology::UPCE},
        {"ean8", Symbology::EAN8},
        {"ean13", Symbology::EAN13},
        {"databar", Symbology::DataBar}, {"gs1databar", Symbology::DataBar},
        {"databarexpanded", Symbology::DataBarExpanded},
        {"msi", Symbology::MSI}, {"msiplessey", Symbology::MSI},
        {"plessey", Symbology::Plessey},
        {"telepen", Symbology::Telepen},
        {"pharmacode", Symbology::Pharmacode},
        {"codablockf", Symbology::CodablockF},
        {"code16k", Symbology::Code16K},
        {"code49", Symbology::Code49},
        {"dotcode", Symbology::DotCode},
        {"hanxin", Symbology::HanXin},
        {"gridmatrix", Symbology::GridMatrix},
        {"swissqr", Symbology::SwissQR}, {"swissqrcode", Symbology::SwissQR},
        {"maxicode", Symbology::MaxiCode},
        {"micropdf417", Symbology::MicroPDF417},
        {"uspsimb", Symbology::USPSIMb}, {"imb", Symbology::USPSIMb},
        {"intelligentmail", Symbology::USPSIMb},
        {"rm4scc", Symbology::RM4SCC},
        {"australiapost", Symbology::AustraliaPost},
        {"japanpost", Symbology::JapanPost},
        {"deutschepost", Symbology::DeutschePost}, {"identcode", Symbology::DeutschePost},
        {"leitcode", Symbology::DeutschePost},
        {"kix", Symbology::KIX},
        {"gs1composite", Symbology::GS1Composite}, {"composite", Symbology::GS1Composite},
    };
    for (const auto& e : table) {
        if (std::strcmp(n, e.key) == 0) return e.s;
    }
    return Symbology::Unknown;
}

} // namespace omniscan
