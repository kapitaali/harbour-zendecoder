/*
 * Static barcode decoding — see staticdecoder.h for the threading design.
 *
 * The decode itself runs libomniscan (3rdparty/omniscan, backend ON, so
 * Grade-1 formats go through its vendored zxing-cpp 2.3.0 and the native
 * Tier-2 codecs answer directly): a grayscale ImageView over the QImage's
 * bytes fed to decode_into with the format-group mask, default settings
 * first, escalated to try_harder on a miss only. Grayscale conversion
 * happens here, off the UI thread, and the result is reported back on the
 * Decoder's thread by a queued signal.
 *
 * Diagnostics go to stderr with fprintf (same reason as decoder.cpp:
 * qWarning lands in the journal the app cannot read back). Every gallery
 * decode logs its symbology and duration — the live loop logs only hits,
 * since a miss there is the normal case.
 */
#include "staticdecoder.h"

#include "formatgroups.h"

#include <omniscan/omniscan.h>

#include <QElapsedTimer>
#include <QThread>

#include <cstdio>
#include <initializer_list>
#include <vector>

namespace {

/**
 * Map the FormatGroup bits to libomniscan's symbology mask. Group
 * membership is deliberately generous: every readable variant of a
 * symbology belongs to its group, because the dispatcher filters by
 * intersection — leaving a variant out would silently stop it from ever
 * matching. Postal codes have no settings group of their own; they are
 * linear barcodes, so they ride with the "other 1D" group.
 *
 * Deliberately NOT enabled: SwissQR (a QR payload schema — enabling its
 * bit would relabel Swiss-bill QR reads instead of decoding anything new).
 */
omniscan::SymMask selectedSymbologies(quint32 mask)
{
    using S = omniscan::Symbology;
    using omniscan::symbology_bit;
    omniscan::SymMask out = 0;
    const auto add = [&out, mask](quint32 group,
                                  std::initializer_list<S> syms) {
        if (mask & group)
            for (S s : syms)
                out |= symbology_bit(s);
    };

    add(FormatGroup::Retail,
        {S::EAN13, S::EAN8, S::UPCA, S::UPCE, S::ITF});
    add(FormatGroup::Linear,
        {S::Code39, S::Code93, S::Code128, S::Codabar, S::DataBar,
         S::DataBarExpanded, S::MSI, S::Plessey, S::Telepen, S::Pharmacode,
         S::CodablockF, S::Code16K,
         S::USPSIMb, S::RM4SCC, S::AustraliaPost, S::JapanPost,
         S::DeutschePost, S::KIX});
    add(FormatGroup::Matrix,
        {S::QRCode, S::MicroQRCode, S::RmQR, S::DataMatrix, S::Aztec,
         S::MaxiCode});
    add(FormatGroup::Pdf417, {S::PDF417});
    return out;
}

/**
 * Display/store labels. Identical to the old zxing ToString() names for
 * every symbology the app showed before the libomniscan migration, so
 * history entries keep their labels; native-only formats (no old
 * equivalent) keep their lib names. Any rename is documented here, not
 * silent: ISBN/EAN-2/5 addons, Code 39 Std/Ext, Code 32, PZN, ITF-14 and
 * the DataBar/Aztec/QR/PDF417 sub-variants no longer exist as separate
 * zxing 2.3.0 results — they read as their base symbology with identical
 * text (verified on fixtures; MicroPDF417 and DX Film Edge have no
 * reader yet — reported, not renamed). DataBar Limited now reads as
 * its umbrella "DataBar" (lib adapter fix).
 */
QString appLabel(omniscan::Symbology s)
{
    using S = omniscan::Symbology;
    switch (s) {
    case S::QRCode: return QStringLiteral("QR Code");
    case S::MicroQRCode: return QStringLiteral("Micro QR Code");
    case S::RmQR: return QStringLiteral("rMQR Code");
    case S::DataMatrix: return QStringLiteral("Data Matrix");
    case S::Aztec: return QStringLiteral("Aztec");
    case S::PDF417: return QStringLiteral("PDF417");
    case S::Code128: return QStringLiteral("Code 128");
    case S::Code39: return QStringLiteral("Code 39");
    case S::Code93: return QStringLiteral("Code 93");
    case S::Codabar: return QStringLiteral("Codabar");
    case S::ITF: return QStringLiteral("ITF");
    case S::UPCA: return QStringLiteral("UPC-A");
    case S::UPCE: return QStringLiteral("UPC-E");
    case S::EAN8: return QStringLiteral("EAN-8");
    case S::EAN13: return QStringLiteral("EAN-13");
    case S::DataBar: return QStringLiteral("DataBar");
    case S::DataBarExpanded: return QStringLiteral("DataBar Expanded");
    case S::CodablockF: return QStringLiteral("Codablock F");
    case S::Code16K: return QStringLiteral("Code 16K");
    case S::MaxiCode: return QStringLiteral("MaxiCode");
    default: break;
    }
    // Native-only symbologies (MSI, KIX, ...) and anything unexpected:
    // the lib's own stable name.
    return QString::fromLatin1(omniscan::to_string(s));
}

} // namespace

StaticDecoder::StaticDecoder(QObject *parent)
    : QObject(parent)
    , m_thread(new QThread)
    , m_formatMask(FormatGroup::All)
{
    moveToThread(m_thread);
    m_thread->setObjectName(QLatin1String("zendecoder-static"));
    m_thread->start();
}

StaticDecoder::~StaticDecoder()
{
    // decode() only ever runs as a queued invocation, so the loop drains on
    // quit() and wait() returns once the last decode has emitted. Deleting
    // the image after that is safe — decode() holds its own reference.
    m_thread->quit();
    m_thread->wait();
    delete m_thread;
    m_image = QImage();
}

void StaticDecoder::submit(const QImage &image, quint32 formatMask)
{
    m_image = image;
    m_formatMask = formatMask;
    // No-argument invocation: a queued call carrying the QImage would need
    // the metatype registered for queued delivery. The image rides in the
    // member instead, and the event queue is the memory barrier.
    QMetaObject::invokeMethod(this, "decode", Qt::QueuedConnection);
}

void StaticDecoder::decode()
{
    // Local copies first: after emitting, the main thread may submit again
    // and replace these while this frame's decode is still running.
    const QImage image = m_image;
    const quint32 mask = m_formatMask;

    bool found = false;
    QString text;
    QString format;
    int elapsedMs = 0;

    const omniscan::SymMask syms = (mask == 0) ? 0 : selectedSymbologies(mask);
    if (!image.isNull() && syms != 0) {
        QElapsedTimer timer;
        timer.start();
        try {
            const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
            if (!gray.isNull()) {
                const omniscan::ImageView view(gray.constBits(), gray.width(),
                                               gray.height(), gray.bytesPerLine());
                omniscan::Options options;
                options.enabled_symbologies = syms;
                options.max_symbols = 1; // resultReady carries one result
                std::vector<omniscan::Result> out;
                omniscan::DecodeStatus st =
                    omniscan::decode_into(view, options, out);
                if (st == omniscan::DecodeStatus::NoBarcodeFound && out.empty()) {
                    // Miss at default settings: one harder retry (extra
                    // scan passes per the support matrix — confined here to
                    // images that found nothing).
                    options.try_harder = true;
                    st = omniscan::decode_into(view, options, out);
                }
                // Every non-Ok status funnels into the miss path, exactly
                // like today's static miss: NoBarcodeFound is silent live /
                // notFound() one-shot via the downstream logic, and
                // BackendNotAvailable falls through to the D-Bus QR path
                // the same way (the backend is compiled in, so that only
                // fires for bits no backend handles).
                if (st == omniscan::DecodeStatus::Ok && !out.empty()
                        && !out.front().text.empty()) {
                    found = true;
                    text = QString::fromStdString(out.front().text);
                    format = appLabel(out.front().symbology);
                }
            }
        } catch (...) {
            // The C++ API promises no exceptions, but a corrupt image must
            // never take the worker down: report it as a miss so the
            // caller can fall back or say so.
            std::fprintf(stderr, "static decode: exception, treated as miss\n");
            found = false;
            text.clear();
            format.clear();
        }
        elapsedMs = int(timer.elapsed());
    } else if (mask == 0) {
        // All format groups off: scanning is off. Counted rather than
        // logged per frame — the live loop submits every ~150 ms.
        static int logged = 0;
        if (logged++ < 3)
            std::fprintf(stderr, "static decode: all format groups disabled, skipping\n");
    }

    emit resultReady(found, text, format, elapsedMs);
}
