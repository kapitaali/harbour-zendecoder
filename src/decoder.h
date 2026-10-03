/*
 * Barcode decoding.
 *
 * ScanPage's Camera takes still captures (the same camerabin image branch the
 * gallery app uses for photos); Camera.imageCapture emits imageSaved with the
 * file it wrote, and that file is handed to submitImageFile(), converted to
 * tightly packed ARGB32 and passed to the barcode service the system ships —
 * org.amberapi.zxing (zxing-daemon from the qr-filter-qml-plugin package) —
 * which answers with the text a code carries. The temporary file is deleted
 * after reading so scanning never litters the gallery.
 *
 * Talking to that service over Qt5DBus instead of importing Amber.QrFilter is
 * deliberate: Harbour rejects the Amber.* QML modules, while Qt5DBus and
 * QtMultimedia are both on the allowed list, and a type built into the
 * application needs no module at all (it reaches QML as a context property).
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
 *
 * v0.1 note: the daemon answers with the decoded TEXT only, no symbology.
 * The decoded() signal therefore carries format as an empty string; the
 * second parameter exists so QML already written against
 * decoded(text, format) keeps working once the vendored static decoder
 * (PLAN.md §2 plan B, full zxing-cpp with BarcodeFormat) lands.
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
     * The text carried by the code that was just decoded. Format is empty
     * in v0.1 (daemon returns text only); reserved for the static decoder.
     */
    void decoded(const QString &text, const QString &format);
    /**
     * A one-shot decodeFile() finished with no code found. The live loop
     * stays silent on empties (they happen every frame); this lets the
     * gallery UI say so instead of going quiet.
     */
    void notFound();

private slots:
    /** Queued: writes the pixels out and calls the service. */
    void submitPending();
    void callFinished(QDBusPendingCallWatcher *watcher);
    /** Timestamped readiness log line (cadence diagnostics). */
    void logReady(bool ready);

private:
    /** Converts one still to packed ARGB32 and queues the decode. */
    void submitFrame(const QImage &image);

    /**
     * Asks the camera for a scan-sized capture resolution. Returns true when
     * done (sizes set, or the camera had nothing to offer); false means
     * "camera still starting, try again later".
     */
    bool configureResolution(QObject *capture);
    void attemptResolution(int attempt);
    void loadFile(const QString &fileName, bool deleteAfter);

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
};

#endif // DECODER_H
