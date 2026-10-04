/*
 * Symbology toggle groups — the four bits behind Settings' format
 * switches (all ON by default, per PLAN.md).
 *
 * The bit values travel Settings -> Decoder -> StaticDecoder as a
 * quint32 mask; the actual mapping to libomniscan Symbology bits lives
 * in staticdecoder.cpp, next to the only code that needs it. Decoder
 * also uses FormatGroup::Matrix to decide whether the QR-only D-Bus
 * fallback is allowed to answer at all (it must not decode codes the
 * user has just turned off).
 */
#ifndef FORMATGROUPS_H
#define FORMATGROUPS_H

#include <QtGlobal>

namespace FormatGroup {

/** Retail & logistics: EAN-8/13, UPC-A/E, ISBN, ITF. */
const quint32 Retail = 1u << 0;
/** Other 1D: Code 39/93/128, Codabar, DataBar family, DX Film Edge. */
const quint32 Linear = 1u << 1;
/** 2D matrix: QR (incl. Micro/rMQR), Aztec, Data Matrix, MaxiCode. */
const quint32 Matrix = 1u << 2;
/** Stacked linear 2D: PDF417, Compact/Micro PDF417. */
const quint32 Pdf417 = 1u << 3;

/** Every group — also the default and the "no settings wired" value. */
const quint32 All = Retail | Linear | Matrix | Pdf417;

} // namespace FormatGroup

#endif // FORMATGROUPS_H
