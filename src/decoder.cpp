/*
 * Barcode decoding — see decoder.h for the two-decoder pipeline and the
 * capture-based flow.
 *
 * submitImageFile()/decodeFile() load one image (EXIF-oriented, scaled
 * while decoding) and hand it to submitFrame(), which acquires the busy
 * flag and submits to the StaticDecoder worker thread. staticDecodeFinished()
 * then either reports the hit (static decoded) or packs the frame as
 * tightly packed ARGB32 and falls back to the system service —
 * submitPending() writes the pixels into a memfd and calls
 * org.amberapi.zxing asynchronously -> callFinished() finishes the chain.
 * One decode in flight at a time keeps things cheap.
 *
 * Diagnostics go to stderr with fprintf: Sailfish's QtBuild routes qWarning
 * to the system journal (libQt5Core links sd_journal_send), which an
 * unprivileged app process cannot read back — stderr is what the debug
 * launch recipe captures. Every line is timestamped because the interesting
 * question is always "how long did the capture take".
 */

#include "decoder.h"
#include "formatgroups.h"
#include "settings.h"
#include "staticdecoder.h"

#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusError>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusPendingCall>
#include <QtDBus/QDBusPendingCallWatcher>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusUnixFileDescriptor>

#include <QtMultimedia/QCamera>
#include <QtMultimedia/QCameraExposure>
#include <QtMultimedia/QCameraViewfinderSettings>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFile>
#include <QImageReader>
#include <QSize>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QDebug>

#include <sys/syscall.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace {

const char kService[] = "org.amberapi.zxing";
const char kObjectPath[] = "/org/amberapi/zxing";
const char kInterface[] = "org.amberapi.zxing";

// The pixel format the daemon decodes: QVideoFrame::Format_ARGB32 works
// while RGB32/Y8/RGB24/RGB565 return nothing (probe-verified on device).
const int kPixelFormat = 1;     // QVideoFrame::Format_ARGB32

// How long to leave the camera alone after handing over an image.
const int kSampleIntervalMs = 150;

// Longest edge sent to the daemon. With the capture negotiated down to
// kCaptureMaxEdge this never triggers — it is the safety net should the
// camera ever hand over a larger still (native is 8192x6144, ~200 MB as
// ARGB32, which no D-Bus message would carry).
const int kDecodeMaxEdge = 2560;

// Largest capture edge requested from the camera. Native stills are
// 8192x6144 and take ~3 s each; at 1920x1440 the capture cycle drops to
// ~1.5 s and the payload fits D-Bus comfortably. 2560-wide capture sizes
// make camerabin fail with "failed to negotiate caps" and drop the
// pipeline to Unloaded — so 1920x1440 is the ceiling.
const int kCaptureMaxEdge = 1920;

int createFrameFd()
{
#ifdef __NR_memfd_create
    return int(syscall(__NR_memfd_create, "zendecoder-frame", 0u));
#else
    return -1;
#endif
}

const char *timestamp()
{
    static char buffer[16];
    const QByteArray now = QTime::currentTime().toString(QLatin1String("hh:mm:ss.zzz")).toLatin1();
    std::memcpy(buffer, now.constData(), size_t(now.size()));
    buffer[now.size()] = '\0';
    return buffer;
}

QString localPath(const QString &fileName)
{
    const QUrl url(fileName);
    if (url.isLocalFile() || fileName.startsWith(QLatin1String("file:")))
        return url.toLocalFile().isEmpty() ? fileName : url.toLocalFile();
    return fileName;
}

} // namespace

Decoder::Decoder(QObject *parent)
    : QObject(parent)
    , m_resolutionDone(false)
    , m_flashLogged(false)
    , m_busy(0)
    , m_oneShot(false)
    , m_frames(0)
    , m_width(0)
    , m_height(0)
    , m_fd(-1)
    , m_static(Q_NULLPTR)
    , m_settings(Q_NULLPTR)
    , m_formatMask(FormatGroup::All)
    , m_appActive(true)
{
    // No parent: a parented object cannot be moveToThread()'d, so
    // StaticDecoder is owned outright and deleted here first.
    m_static = new StaticDecoder;
    connect(m_static, &StaticDecoder::resultReady,
            this, &Decoder::staticDecodeFinished);
}

void Decoder::setSettings(Settings *settings)
{
    m_settings = settings;
}

void Decoder::setApplicationActive(bool active)
{
    if (m_appActive == active)
        return;
    m_appActive = active;
    qInfo("application %s", active ? "active" : "inactive");
    // Emit first: QML stops the capture loop and calls camera.stop(), and
    // everything we release below must happen AFTER that — QCamera::stop()
    // moves the camera back to the loaded state, undoing an unload().
    emit applicationActiveChanged(active);
    if (!active) {
        releaseCamera("focus lost");
        // A capture already in flight completes ~1 s later and makes
        // camerabin restart the preview on top of us; re-check then.
        QTimer::singleShot(1500, this, [this]() {
            if (!m_appActive)
                releaseCamera("still inactive (re-check)");
        });
    }
}

void Decoder::releaseCamera(const char *why)
{
    QObject *media = m_camera ? m_camera->property("mediaObject").value<QObject *>()
                              : nullptr;
    QCamera *qcam = qobject_cast<QCamera *>(media);
    if (!qcam) {
        static int logged = 0;
        if (logged++ < 3)
            std::fprintf(stderr, "[%s] release camera: no QCamera (%s)\n",
                         timestamp(), why);
        return;
    }
    const int before = int(qcam->status());
    qcam->unload();
    qInfo("camera released (%s): status %d -> %d", why, before,
          int(qcam->status()));
}

Decoder::~Decoder()
{
    // Order matters: stopping the worker first guarantees no further
    // resultReady emission, then dropping this object's queued events
    // removes the result that emission may just have posted — so nothing
    // runs on a half-destroyed Decoder. The memfd goes last.
    delete m_static;
    m_static = Q_NULLPTR;
    QCoreApplication::removePostedEvents(this, 0);
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
}

void Decoder::setTorch(bool on)
{
    // One "TYPE CT PART TORCH_STATUS" tuple per write() syscall — the
    // MTK flashlight-core store() parses a single tuple, so two writes
    // cover both LED channels (ct 0 and 1); each QFile::write() is one
    // syscall, i.e. one store() call. Single-value writes ("1") are
    // rejected by the driver ("Error argument number").
    static const char *kTorchNode =
        "/sys/class/flashlight_core/flashlight/flashlight_torch";
    static bool missingLogged = false;
    const char *t0 = on ? "0 0 0 1" : "0 0 0 0";
    const char *t1 = on ? "0 1 0 1" : "0 1 0 0";
    QFile f(QString::fromLatin1(kTorchNode));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (!missingLogged) {
            missingLogged = true;
            qInfo("torch sysfs: no node at %s (%s) — Qt flash.mode only",
                  kTorchNode, qPrintable(f.errorString()));
        }
        return;
    }
    const qint64 w0 = f.write(t0);
    const qint64 w1 = f.write(t1);
    f.close();
    qInfo("torch sysfs: on=%d wrote %lld/%lld bytes", on ? 1 : 0,
          (long long)w0, (long long)w1);
}

void Decoder::logMessage(const QString &message)
{
    qInfo("decode qml: %s", qPrintable(message));
}

void Decoder::attachCamera(QObject *qmlCamera)
{
    if (!qmlCamera) {
        std::fprintf(stderr, "[%s] decode attach: no camera object\n", timestamp());
        std::fflush(stderr);
        return;
    }

    QObject *capture = qmlCamera->property("imageCapture").value<QObject *>();
    if (!capture) {
        std::fprintf(stderr, "[%s] decode attach: camera has no imageCapture group\n", timestamp());
        std::fflush(stderr);
        return;
    }

    m_camera = qmlCamera;
    m_captureGroup = capture;
    m_resolutionDone = false;           // this camera still wants configuring
    m_flashLogged = false;              // re-probe flash capability per camera

    // Captures go to a private cache file, never to the gallery: the path is
    // handed to every capture via captureToLocation(), read back through
    // imageSaved and deleted by submitImageFile(). Sweeping the old file
    // here also cleans up after a crash mid-scan.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath() + QLatin1String("/harbour-zendecoder");
    QDir().mkpath(dir);
    m_scanFilePath = dir + QLatin1String("/scan.jpg");
    QFile::remove(m_scanFilePath);

    std::fprintf(stderr,
                 "[%s] decode attach: capture=%s scan file=%s\n", timestamp(),
                 capture->metaObject()->className(), qPrintable(m_scanFilePath));
    std::fflush(stderr);
    qInfo("decode attach: capture=%s scan file=%s",
          capture->metaObject()->className(), qPrintable(m_scanFilePath));

    // Log every readiness flip: the capture cadence is measured from these
    // lines (a ready=false stretch with no imageSaved means a stalled
    // capture, ready=true without requests means requestCapture refused).
    connect(capture, SIGNAL(readyForCaptureChanged(bool)),
            this, SLOT(logReady(bool)), Qt::UniqueConnection);

    attemptResolution(0);
}

void Decoder::logReady(bool ready)
{
    std::fprintf(stderr, "[%s] decode ready=%d\n", timestamp(), ready ? 1 : 0);
    std::fflush(stderr);
    if (!ready || m_flashLogged)
        return;
    // Torch debugging: does the gstcamerabin backend advertise FlashTorch
    // at all? Logged once when the camera first comes up (exposure queries
    // are meaningless before that).
    QObject *media = m_camera ? m_camera->property("mediaObject").value<QObject *>()
                              : nullptr;
    QCamera *qcam = qobject_cast<QCamera *>(media);
    if (!qcam)
        return;
    QCameraExposure *e = qcam->exposure();
    qInfo("flash capability: FlashTorch=%d FlashOff=%d FlashAuto=%d "
          "current=0x%x ready=%d",
          e->isFlashModeSupported(QCameraExposure::FlashTorch) ? 1 : 0,
          e->isFlashModeSupported(QCameraExposure::FlashOff) ? 1 : 0,
          e->isFlashModeSupported(QCameraExposure::FlashAuto) ? 1 : 0,
          int(e->flashMode()), e->isFlashReady() ? 1 : 0);
    m_flashLogged = true;
}

bool Decoder::requestCapture()
{
    if (!m_appActive)
        return false;               // backgrounded: never pull new frames
    QObject *capture = m_captureGroup.data();
    if (!capture || m_scanFilePath.isEmpty())
        return false;
    if (!m_resolutionDone)
        return false;                   // a resolution change may be reloading
                                        // the pipeline — capturing now would
                                        // be swallowed by the reload
    if (!capture->property("ready").toBool())
        return false;                   // a capture is already in flight

    const bool invoked = QMetaObject::invokeMethod(capture, "captureToLocation",
                                                   Q_ARG(QString, m_scanFilePath));
    std::fprintf(stderr, "[%s] decode capture requested (invoke=%d)\n",
                 timestamp(), invoked ? 1 : 0);
    std::fflush(stderr);
    if (!invoked)
        qWarning("decode capture request failed");
    return invoked;
}

void Decoder::attemptResolution(int attempt)
{
    QObject *capture = m_captureGroup.data();
    if (!capture)
        return;
    if (configureResolution(capture)) {
        m_resolutionDone = true;
        return;
    }
    if (attempt < 5) {
        // The camera reports no resolutions while it is still starting up
        // (the first attempt right after Camera.onCompleted sees an empty
        // list), so keep asking until it settles.
        QTimer::singleShot(1500, this, [this, attempt]() {
            attemptResolution(attempt + 1);
        });
    } else {
        m_resolutionDone = true;        // give up — the default size works
        std::fprintf(stderr, "[%s] decode attach: no resolutions ever reported — "
                     "keeping the camera's default capture size\n", timestamp());
        std::fflush(stderr);
    }
}

bool Decoder::configureResolution(QObject *capture)
{
    // Default stills are the sensor's full 8192x6144 — a ~3 s capture cycle
    // and a 200 MB ARGB32 buffer. Ask for the largest supported size that
    // fits kCaptureMaxEdge; if the camera reports nothing (it may still be
    // starting up), leave the default alone rather than risk a broken size.
    const QCamera *qcam = qobject_cast<QCamera *>(
            m_camera ? m_camera->property("mediaObject").value<QObject *>()
                     : nullptr);
    if (!qcam) {
        std::fprintf(stderr, "[%s] decode configure: no QCamera yet\n", timestamp());
        std::fflush(stderr);
        return false;                   // camera not constructed yet — retry
    }

    QSize chosen;
    QString supported;
    const QList<QSize> sizes = qcam->supportedViewfinderResolutions();
    for (const QSize &s : sizes) {
        if (!supported.isEmpty())
            supported += QLatin1Char(' ');
        supported += QString::fromLatin1("%1x%2").arg(s.width()).arg(s.height());
        if (s.isValid() && s.width() <= kCaptureMaxEdge
                && s.height() <= kCaptureMaxEdge
                && s.width() * s.height() > chosen.width() * chosen.height()) {
            chosen = s;
        }
    }

    if (supported.isEmpty())
        return false;                   // not answering yet — retry

    int set = 0;
    QSize readBack;
    if (chosen.isValid()) {
        set = capture->setProperty("resolution", QSize(chosen)) ? 1 : 0;
        readBack = capture->property("resolution").toSize();
    }

    std::fprintf(stderr,
                 "[%s] decode attach: capture=%s supported=[%s] chosen=%dx%d set=%d readBack=%dx%d\n",
                 timestamp(), capture->metaObject()->className(),
                 qPrintable(supported), chosen.width(), chosen.height(), set,
                 readBack.isValid() ? readBack.width() : -1,
                 readBack.isValid() ? readBack.height() : -1);
    std::fflush(stderr);
    qInfo("decode capture size: supported=[%s] chosen=%dx%d readBack=%dx%d",
          qPrintable(supported), chosen.width(), chosen.height(),
          readBack.isValid() ? readBack.width() : -1,
          readBack.isValid() ? readBack.height() : -1);
    return true;
}

void Decoder::submitImageFile(const QString &fileName)
{
    // A capture that was already on its way when focus was lost arrives
    // here after camerabin has resumed the preview — let go again before
    // decoding (the decode itself only needs the file).
    if (!m_appActive)
        releaseCamera("capture completed while inactive");
    loadFile(fileName, true, false);     // live loop: empties stay silent
}

void Decoder::decodeFile(const QString &fileName)
{
    loadFile(fileName, false, true);     // gallery import: report empties
}

void Decoder::loadFile(const QString &fileName, bool deleteAfter, bool oneShot)
{
    // Every capture is logged: scans produce one file per second at most,
    // and the log shows how long each cycle takes.
    ++m_frames;
    const QString path = localPath(fileName);
    std::fprintf(stderr, "[%s] decode file #%d %s (delete=%d, oneShot=%d)\n",
                 timestamp(), m_frames, qPrintable(path), deleteAfter ? 1 : 0,
                 oneShot ? 1 : 0);
    std::fflush(stderr);

    QImageReader reader(path);
    reader.setAutoTransform(true);      // honour EXIF orientation

    // Scale during decoding: only the small buffer is allocated, and Qt's
    // JPEG reader can downscale cheaply.
    const QSize size = reader.size();
    if (size.isValid()
            && qMax(size.width(), size.height()) > kDecodeMaxEdge) {
        reader.setScaledSize(size.scaled(kDecodeMaxEdge, kDecodeMaxEdge,
                                         Qt::KeepAspectRatio));
    }

    const QImage image = reader.read();
    if (deleteAfter)
        QFile::remove(path);            // scans must not litter the gallery

    if (image.isNull()) {
        std::fprintf(stderr, "[%s] decode read failed: %s\n", timestamp(),
                     qPrintable(reader.errorString()));
        std::fflush(stderr);
        qWarning("decode read failed: %s", qPrintable(reader.errorString()));
        // Busy was never acquired here (loadFile runs before submitFrame),
        // so this reports the failure directly instead of going through
        // abortDecode() — which would release a frame's busy flag.
        if (oneShot) {
            qInfo("decode one-shot: read failed");
            emit notFound();
        }
        return;
    }

    std::fprintf(stderr, "[%s] decode pixels %dx%d -> %dx%d\n", timestamp(),
                 size.width(), size.height(), image.width(), image.height());
    std::fflush(stderr);
    qInfo("decode image %dx%d (delete=%d)", image.width(), image.height(),
          deleteAfter ? 1 : 0);

    submitFrame(image, oneShot);
}

void Decoder::submitFrame(const QImage &image, bool oneShot)
{
    if (image.isNull()) {
        if (oneShot)
            emit notFound();
        return;
    }

    if (!m_busy.testAndSetOrdered(0, 1)) {
        // A decode is still in flight. The live loop just drops the frame
        // (the next capture retries); a gallery import gets its answer now
        // rather than hanging a spinner — dropping it silently would leave
        // the gallery UI waiting forever.
        if (oneShot) {
            std::fprintf(stderr, "[%s] decode dropped: busy (one-shot)\n", timestamp());
            std::fflush(stderr);
            qInfo("decode one-shot: busy");
            emit notFound();
        }
        return;
    }

    // Sample the live loop a few times per second. One-shot imports are a
    // user action, not a capture — they always run.
    if (!oneShot && m_lastSubmit.isValid()
            && m_lastSubmit.elapsed() < kSampleIntervalMs) {
        m_busy.storeRelease(0);
        return;
    }

    m_oneShot = oneShot;   // only now: busy held, so this is the live chain
    m_formatMask = m_settings ? m_settings->formatMask() : FormatGroup::All;
    m_pendingImage = image;
    m_lastSubmit.start();

    // qInfo, not fprintf: in a sandboxed run stderr is the booster's
    // socket and never reaches zendecoder.log. Logged only when the mask
    // CHANGES, so the live loop's per-frame submits stay quiet.
    static quint32 s_loggedMask = FormatGroup::All;
    if (m_formatMask != s_loggedMask) {
        s_loggedMask = m_formatMask;
        qInfo("decode formats mask=0x%x", m_formatMask);
    }
    m_static->submit(image, m_formatMask);
}

void Decoder::staticDecodeFinished(bool found, const QString &text,
                                   const QString &format, int elapsedMs)
{
    if (found) {
        m_busy.storeRelease(0);
        m_oneShot = false;      // consumed: the import delivered its result
        m_pendingImage = QImage();
        std::fprintf(stderr, "[%s] static decoded (%s, %d ms): %s\n", timestamp(),
                     qPrintable(format), elapsedMs, qPrintable(text));
        std::fflush(stderr);
        qInfo("static decoded: [%s] %s", qPrintable(format), qPrintable(text));
        emit decoded(text, format);
        return;
    }

    // Static miss → the proven daemon path answers for QR, and the
    // one-shot chain reports notFound() through abortDecode() if it fails.
    // Gated on the matrix group: the daemon is QR-only and ignores the
    // settings, so letting it answer when the user turned QR off would
    // decode exactly what they asked not to.
    if (!(m_formatMask & FormatGroup::Matrix)) {
        abortDecode(m_formatMask == 0
                            ? "all format groups disabled"
                            : "static miss, QR group disabled");
        return;
    }

    std::fprintf(stderr, "[%s] static miss (%d ms) -> service fallback\n",
                 timestamp(), elapsedMs);
    std::fflush(stderr);

    if (!packPixels()) {
        abortDecode("static miss, no pixel payload for the service");
        return;
    }
    QMetaObject::invokeMethod(this, "submitPending", Qt::QueuedConnection);
}

bool Decoder::packPixels()
{
    if (m_pendingImage.isNull())
        return false;

    const QImage argb = m_pendingImage.format() == QImage::Format_ARGB32
            ? m_pendingImage
            : m_pendingImage.convertToFormat(QImage::Format_ARGB32);
    m_pendingImage = QImage();          // cleared either way (see header)
    if (argb.isNull())
        return false;

    // The daemon reads width*height*4 bytes with no stride, so rows are
    // repacked if the capture's lines happen to be padded.
    const int stride = argb.width() * 4;
    m_pixels.resize(stride * argb.height());
    if (argb.bytesPerLine() == stride) {
        std::memcpy(m_pixels.data(), argb.constBits(), size_t(m_pixels.size()));
    } else {
        for (int row = 0; row < argb.height(); ++row)
            std::memcpy(m_pixels.data() + row * stride, argb.constScanLine(row),
                        size_t(stride));
    }

    m_width = argb.width();
    m_height = argb.height();
    return true;
}

void Decoder::abortDecode(const char *why)
{
    const bool oneShot = m_oneShot;
    m_oneShot = false;
    m_pendingImage = QImage();
    m_busy.storeRelease(0);

    std::fprintf(stderr, "[%s] decode abort: %s\n", timestamp(), why);
    std::fflush(stderr);
    if (oneShot) {
        qInfo("decode one-shot: %s", why);
        emit notFound();
    }
}

void Decoder::submitPending()
{
    const int fd = createFrameFd();
    if (fd < 0) {
        abortDecode("memfd_create failed");
        return;
    }

    if (write(fd, m_pixels.constData(), size_t(m_pixels.size())) != ssize_t(m_pixels.size())) {
        close(fd);
        abortDecode("frame write failed");
        return;
    }
    lseek(fd, 0, SEEK_SET);

    QDBusInterface iface(QLatin1String(kService), QLatin1String(kObjectPath),
                         QLatin1String(kInterface), QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        static int logged = 0;
        if (logged++ < 3)
            std::fprintf(stderr, "[%s] decode service unavailable: %s (%s)\n", timestamp(),
                         qPrintable(iface.lastError().message()),
                         qPrintable(iface.lastError().name()));
        close(fd);
        abortDecode("service unavailable");
        return;
    }

    QList<QVariant> arguments;
    arguments << QVariant::fromValue(QDBusUnixFileDescriptor(fd))
              << QVariant::fromValue(quint32(m_pixels.size()))
              << QVariant::fromValue(m_width)
              << QVariant::fromValue(m_height)
              << QVariant::fromValue(kPixelFormat);

    m_fd = fd;      // closed once the service has read the buffer
    QDBusPendingCallWatcher *watcher =
            new QDBusPendingCallWatcher(iface.asyncCallWithArgumentList(QLatin1String("decodeFromDescriptor"),
                                                                        arguments),
                                        this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Decoder::callFinished);
}

void Decoder::callFinished(QDBusPendingCallWatcher *watcher)
{
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
    m_pendingImage = QImage();  // the fallback payload has been read
    m_busy.storeRelease(0);

    if (watcher->isError()) {
        // Most often the service still starting up on the first frame; the
        // next sample retries.
        static int logged = 0;
        if (logged++ < 3)
            std::fprintf(stderr, "[%s] decode failed: %s\n", timestamp(),
                         qPrintable(watcher->error().message()));
        if (logged <= 3)
            qWarning("decode service call failed: %s",
                     qPrintable(watcher->error().message()));
        if (m_oneShot) {
            m_oneShot = false;
            qInfo("decode one-shot: service call failed");
            emit notFound();
        }
    } else {
        const QString text = QDBusReply<QString>(watcher->reply()).value();
        const bool oneShot = m_oneShot;
        m_oneShot = false;
        if (text.isEmpty()) {
            static int empty = 0;
            if (empty++ < 3)
                std::fprintf(stderr, "[%s] decode returned no code\n", timestamp());
            if (oneShot) {
                qInfo("decode one-shot: no code found");
                emit notFound();
            }
        } else {
            std::fprintf(stderr, "[%s] service decoded: %s\n", timestamp(),
                         qPrintable(text));
            qInfo("service decoded: %s", qPrintable(text));
            // The daemon answers QR only and reports no symbology, so
            // format stays empty on this path (see decoder.h).
            emit decoded(text, QString());
        }
    }
    std::fflush(stderr);

    watcher->deleteLater();
}
