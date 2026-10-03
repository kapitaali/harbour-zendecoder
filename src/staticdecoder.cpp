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

#include <QElapsedTimer>
#include <QThread>

#include <cstdio>

StaticDecoder::StaticDecoder(QObject *parent)
    : QObject(parent)
    , m_thread(new QThread)
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

void StaticDecoder::submit(const QImage &image)
{
    m_image = image;
    // No-argument invocation: a queued call carrying the QImage would need
    // the metatype registered for queued delivery. The image rides in the
    // member instead, and the event queue is the memory barrier.
    QMetaObject::invokeMethod(this, "decode", Qt::QueuedConnection);
}

void StaticDecoder::decode()
{
    // Local copy first: after emitting, the main thread may submit again
    // and replace m_image while this frame's pixels must stay alive.
    const QImage image = m_image;

    bool found = false;
    QString text;
    QString format;
    int elapsedMs = 0;

    if (!image.isNull()) {
        QElapsedTimer timer;
        timer.start();
        try {
            const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
            if (!gray.isNull()) {
                const ZXing::ImageView view(gray.constBits(), gray.width(),
                                            gray.height(), ZXing::ImageFormat::Lum,
                                            gray.bytesPerLine());
                const auto options = ZXing::ReaderOptions()
                        .setFormats(ZXing::BarcodeFormat::All)
                        .setTryHarder(true)
                        .setTryDownscale(true);
                const auto result = ZXing::ReadBarcode(view, options);
                if (result.isValid() && !result.text().empty()) {
                    found = true;
                    text = QString::fromStdString(result.text());
                    format = QString::fromStdString(ZXing::ToString(result.format()));
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
    }

    emit resultReady(found, text, format, elapsedMs);
}
