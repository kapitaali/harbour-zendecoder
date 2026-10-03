/*
 * Barcode decoding — hybrid pipeline.
 *
 * ScanPage's Camera takes still captures (the same camerabin image branch
 * the gallery app uses for photos); Camera.imageCapture emits imageSaved
 * with the file it wrote, and that file is handed to submitImageFile(),
 * decoded, and deleted again so scanning never litters the gallery.
 * Gallery imports go through decodeFile(), which never deletes.
 *
 * TWO DECODERS, STATIC FIRST:
 *
 *  1. StaticDecoder (src/staticdecoder.*) — the vendored zxing-cpp
 *     (3rdparty/, readers-only static build, ALL symbologies) on a worker
 *     thread. Primary path for both gallery and live captures: it names
 *     the symbology (EAN-13, Code 128, …), and the system daemon cannot
 *     see 1D codes at all.
 *  2. org.amberapi.zxing (zxing-daemon from qr-filter-qml-plugin) over
 *     Qt5DBus — the fallback when the static decoder finds nothing.
 *     QR-only by construction (service/service.cpp:90 sets formats to
 *     QRCode), but a proven path that has shipped on this phone all
 *     along, so it stays as insurance. The log line says which path
 *     answered: "static decoded" vs "service decoded".
 *
 * One decode in flight at a time: m_busy is acquired when an image enters
 * submitFrame() and released only when a terminal result arrives (static
 * hit, daemon reply, or an abortDecode() on any failure). m_oneShot marks
 * gallery imports — their failures surface as notFound() so the UI can
 * say "no code found"; the live loop stays silent on misses (they happen
 * every frame). m_pendingImage holds the frame for the daemon fallback.
 *
 * Talking to the service over Qt5DBus instead of importing Amber.QrFilter
 * is deliberate: Harbour rejects the Amber.* QML modules, while Qt5DBus
 * and QtMultimedia are both on the allowed list.
 *
 * WHY STILL CAPTURES INSTEAD OF VIEWFINDER FRAMES: on-device the camera
 * service exposes neither QVideoRendererControl nor the GStreamer sink
 * control, so QML VideoOutput falls back to Qt's window/overlay backend —
 * and that backend contains no filter chain at all (Qt 5.6,
 * qtmultimediaquicktools/qdeclarativevideooutput_window.cpp: the string
 * "filter" does not occur). Frames also cannot be taken from the camera any
 * other way: QVideoProbe's setSource() fails, and a custom
 * QCamera::setViewfinder() surface receives nothing. Still captures do work.
 * (The QML-visible preview signal imageCaptured never fires here — camerabin
 * only emits it with a preview buffer message that does not arrive — which is
 * why the flow keys off imageSaved, the signal the written file guarantees.)
 */
#ifndef DECODER_H
#define DECODER_H

#include <QAtomicInt>
#include <QByteArray>
#include <QImage>
#include <QPointer>
#include <QString>
#include <QTime>

class QDBusPendingCallWatcher;
class StaticDecoder;

class Decoder : public QObject
{
    Q_OBJECT
public:
    explicit Decoder(QObject *parent = Q_NULLPTR);
    ~Decoder() override;

    /**
     * Point the scanner at the QML Camera. The device encodes 8192x6144
     * stills by default — a ~3 s capture cycle — so the largest supported
     * capture resolution no bigger than kCaptureMaxEdge (1920x1440; 2560
     * kills the camera) is requested. Captures are directed at a private
     * file under the app's cache directory (see requestCapture()) so
     * scanning can never leave photos in the gallery.
     */
    Q_INVOKABLE void attachCamera(QObject *qmlCamera);

    /**
     * Ask the camera for one still, written to the scanner's private cache
     * file (imageSaved reports that path; submitImageFile() reads and
     * deletes it). Returns false without requesting anything when the
     * camera is not ready — so callers can fire it on every readiness edge
     * and from a fallback timer without queueing captures.
     */
    Q_INVOKABLE bool requestCapture();

    /**
     * Hand over the file Camera.imageCapture.onImageSaved reported. Reads it
     * (EXIF orientation honoured, scaled down only beyond kDecodeMaxEdge),
     * deletes it, and queues the decode.
     */
    Q_INVOKABLE void submitImageFile(const QString &fileName);

    /**
     * Decode an existing gallery file. Unlike submitImageFile() this NEVER
     * deletes the file. Used by the gallery-import path.
     */
    Q_INVOKABLE void decodeFile(const QString &fileName);

    /**
     * One-line note from QML into the sandbox log (QML console.log lands
     * in the journal, which the app cannot read back). Used for camera
     * errors, which otherwise stay invisible on-device.
     */
    Q_INVOKABLE void logMessage(const QString &message);

signals:
    /**
     * The text carried by the code that was just decoded, plus its
     * symbology name ("QR Code", "EAN-13", …) from the static decoder.
     * The daemon fallback only answers QR and reports no symbology, so
     * format comes back empty from that path.
     */
    void decoded(const QString &text, const QString &format);
    /**
     * A one-shot decodeFile() finished with no code found (static miss
     * AND daemon miss/failure). The live loop stays silent on empties
     * (they happen every frame); this lets the gallery UI say so instead
     * of going quiet.
     */
    void notFound();

private slots:
    /** Queued: writes the pixels out and calls the service. */
    void submitPending();
    void callFinished(QDBusPendingCallWatcher *watcher);
    /** StaticDecoder's worker thread reports (queued). */
    void staticDecodeFinished(bool found, const QString &text,
                              const QString &format, int elapsedMs);
    /** Timestamped readiness log line (cadence diagnostics). */
    void logReady(bool ready);

private:
    /**
     * One still into the pipeline: sample gate (bypassed for one-shot
     * imports), busy acquire, then static decode. oneShot is stored only
     * once the busy flag is held so a dropped frame can't leave it set.
     */
    void submitFrame(const QImage &image, bool oneShot);
    /** Static miss → pack m_pendingImage as ARGB32 for the daemon. */
    bool packPixels();
    /**
     * Terminal failure: release busy, consume m_oneShot (emitting
     * notFound() for gallery imports), log why.
     */
    void abortDecode(const char *why);

    /**
     * Asks the camera for a scan-sized capture resolution. Returns true when
     * done (sizes set, or the camera had nothing to offer); false means
     * "camera still starting, try again later".
     */
    bool configureResolution(QObject *capture);
    void attemptResolution(int attempt);
    void loadFile(const QString &fileName, bool deleteAfter, bool oneShot);

    QPointer<QObject> m_camera;         // the QML Camera
    QPointer<QObject> m_captureGroup;   // the camera's imageCapture group
    QString m_scanFilePath;             // private file captures are written to
    bool m_resolutionDone;              // camera configured (or gave up)
    QAtomicInt m_busy;         // one decode in flight at a time
    bool m_oneShot;             // current decode came from decodeFile(): report empties
    QTime m_lastSubmit;        // decodes are sampled a few times per second
    int m_frames;              // stills offered so far
    QByteArray m_pixels;       // tightly packed ARGB32 payload for submitPending()
    int m_width;
    int m_height;
    int m_fd;                  // kept open until the service has read it
    QImage m_pendingImage;     // original frame, kept for the daemon fallback
    StaticDecoder *m_static;   // vendored zxing-cpp on a worker thread
};

#endif // DECODER_H
