/*
 * Static barcode decoding — see staticdecoder.h for the threading design.
 *
 * The decode itself mirrors the host proof (/tmp/opencode/hosttest): a
 * grayscale ImageView over the QImage's bytes fed to ReadBarcode with
 * all symbologies on, tryHarder and tryDownscale at their defaults'
 * strength. Grayscale conversion happens here, off the UI thread, and the
 * result is reported back on the Decoder's thread by a queued signal.
 *
 * Diagnostics go to stderr with fprintf (same reason as decoder.cpp:
 * qWarning lands in the journal the app cannot read back). Every gallery
 * decode logs its symbology and duration — the live loop logs only hits,
 * since a miss there is the normal case.
 */
#include "staticdecoder.h"

#include "Barcode.h"
#include "BarcodeFormat.h"
#include "ImageView.h"
#include "ReadBarcode.h"
#include "ReaderOptions.h"
#include "formatgroups.h"

#include <QElapsedTimer>
#include <QThread>

#include <cstdio>
#include <initializer_list>
#include <utility>
#include <vector>

namespace {

/**
 * Map the FormatGroup bits to zxing's format enum. Group membership is
 * deliberately generous: every readable variant of a symbology belongs to
 * its group (Code39Std/Ext with Code39, the DataBar flavours together),
 * because the reader filters by "any intersection" — leaving a variant out
 * would silently stop it from ever matching.
 */
std::vector<ZXing::BarcodeFormat> selectedFormats(quint32 mask)
{
    using F = ZXing::BarcodeFormat;
    std::vector<F> out;
    const auto add = [&out, mask](quint32 group,
                                  std::initializer_list<F> formats) {
        if (mask & group)
            out.insert(out.end(), formats.begin(), formats.end());
    };

    add(FormatGroup::Retail,
        {F::EAN13, F::EAN8, F::UPCA, F::UPCE, F::ISBN, F::EAN2, F::EAN5,
         F::ITF, F::ITF14});
    add(FormatGroup::Linear,
        {F::Code39, F::Code39Std, F::Code39Ext, F::Code32, F::PZN, F::Code93,
         F::Code128, F::Codabar, F::DataBar, F::DataBarOmni, F::DataBarStk,
         F::DataBarStkOmni, F::DataBarLtd, F::DataBarExp, F::DataBarExpStk,
         F::DXFilmEdge});
    add(FormatGroup::Matrix,
        {F::QRCode, F::QRCodeModel1, F::QRCodeModel2, F::MicroQRCode,
         F::RMQRCode, F::Aztec, F::AztecCode, F::AztecRune, F::DataMatrix,
         F::MaxiCode});
    add(FormatGroup::Pdf417, {F::PDF417, F::CompactPDF417, F::MicroPDF417});
    return out;
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

    if (!image.isNull() && mask != 0) {
        QElapsedTimer timer;
        timer.start();
        try {
            const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
            if (!gray.isNull()) {
                const ZXing::ImageView view(gray.constBits(), gray.width(),
                                            gray.height(), ZXing::ImageFormat::Lum,
                                            gray.bytesPerLine());
                auto options = ZXing::ReaderOptions()
                        .setTryHarder(true)
                        .setTryDownscale(true);
                bool filterUsable = true;
                if (mask == FormatGroup::All) {
                    // The default: keep the exact pre-toggle behaviour
                    // (BarcodeFormat::All, not an explicit list).
                    options.setFormats(ZXing::BarcodeFormat::All);
                } else {
                    // Every group maps to formats, so this is only empty
                    // if a group bit had no entries — and passing an empty
                    // set to zxing means "no filter", i.e. decoding
                    // everything the user asked to disable.
                    std::vector<ZXing::BarcodeFormat> selected = selectedFormats(mask);
                    if (selected.empty())
                        filterUsable = false;
                    else
                        options.setFormats(ZXing::BarcodeFormats(std::move(selected)));
                }

                if (filterUsable) {
                    const auto result = ZXing::ReadBarcode(view, options);
                    if (result.isValid() && !result.text().empty()) {
                        found = true;
                        text = QString::fromStdString(result.text());
                        format = QString::fromStdString(ZXing::ToString(result.format()));
                    }
                }
            }
        } catch (...) {
            // zxing throws on malformed input paths rather than crashing;
            // report it as a miss so the caller can fall back or say so.
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
