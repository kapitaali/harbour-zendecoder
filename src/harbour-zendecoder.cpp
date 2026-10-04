/*
 * Application entry point.
 *
 * Uses the explicit SailfishApp pattern (not SailfishApp::main()) so the C++
 * back ends — decoder, history, settings, trial, product lookup — can be
 * handed to QML as context properties before the view is shown.
 */

#ifdef QT_QML_DEBUG
#include <QtQuick>
#endif

#include <sailfishapp.h>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickView>
#include <QScopedPointer>
#include <QTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>

#include "decoder.h"
#include "history.h"
#include "settings.h"
#include "trialmanager.h"
#include "productlookup.h"

/*
 * Sailfish's Qt build routes qWarning/qDebug to the system journal, which an
 * unprivileged process cannot read back — and QML runtime errors (bad
 * handler names, type mismatches...) land there too. Mirror everything to
 * stderr with a timestamp instead: the debug launch recipe captures stderr,
 * so failures stay diagnosable.
 *
 * A launched (Sailjail-sandboxed) app has no capturable stderr either —
 * its fd 2 is a socket owned by the booster — so also append to a log file
 * in the app's whitelisted data directory. That directory
 * (~/.local/share/<OrganizationName>/<ApplicationName>/) is created and
 * made writable by the launch profile on every start, which makes
 * sandboxed runs diagnosable after the fact.
 */
namespace {

QFile g_logFile;

void openLogFile()
{
    const QString dir = QDir::homePath()
            + QStringLiteral("/.local/share/harbour.zendecoder/harbour-zendecoder");
    QDir().mkpath(dir);

    const QString path = dir + QStringLiteral("/zendecoder.log");
    if (QFileInfo(path).size() > 512 * 1024) {
        QFile::remove(path + QStringLiteral(".1"));
        QFile::rename(path, path + QStringLiteral(".1"));
    }

    g_logFile.setFileName(path);
    g_logFile.open(QIODevice::Append);
}

} // namespace

static void mirrorMessages(QtMsgType type, const QMessageLogContext &context,
                           const QString &message)
{
    const char *kind = type == QtFatalMsg    ? "FATAL"
            : type == QtCriticalMsg         ? "CRIT"
            : type == QtWarningMsg          ? "WARN"
            : type == QtInfoMsg             ? "INFO"
                                            : "DBG";
    const QByteArray line = QByteArray("[")
            + QTime::currentTime().toString(QLatin1String("hh:mm:ss.zzz")).toLocal8Bit()
            + ' ' + kind + "] " + (context.file ? context.file : "-")
            + ':' + QByteArray::number(context.line) + ' '
            + message.toLocal8Bit() + '\n';

    std::fwrite(line.constData(), 1, line.size(), stderr);
    std::fflush(stderr);
    if (g_logFile.isOpen()) {
        g_logFile.write(line);
        g_logFile.flush();
    }
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(mirrorMessages);
    openLogFile();

    // Startup marker: attributes everything that follows in a captured log
    // to this launch (and proves log capture is working at all). The build
    // id lets testers match a log to the exact binary that wrote it.
    qInfo("zendecoder starting build %s", BUILD_ID);

    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    QScopedPointer<QQuickView> view(SailfishApp::createView());

    History history;
    if (!history.initialize()) {
        qWarning() << "Failed to initialize history database";
    }

    Decoder decoder;
    Settings settings;
    TrialManager trial;
    ProductLookup productLookup;

    // Format toggles: the decoder reads the group mask from settings on
    // every submission (before this it can't decode anything anyway —
    // nothing is attached until QML asks for a capture).
    decoder.setSettings(&settings);

    // Focus -> scanner: the instant the window loses focus, ScannerPage
    // stops the camera and the capture loop (see decoder.h). Without this
    // the loop would keep taking 1920x1440 stills behind the cover, and
    // the system's camerahalserver would stay awake on battery.
    QObject::connect(app.data(), &QGuiApplication::applicationStateChanged,
                     &decoder, [&decoder](Qt::ApplicationState state) {
                         decoder.setApplicationActive(state == Qt::ApplicationActive);
                     });

    // The kernel torch node keeps its state after we die: never leave the
    // LED on behind a closed app (backgrounding is already covered by the
    // setTorch(false) in the QML focus handlers).
    QObject::connect(app.data(), &QCoreApplication::aboutToQuit,
                     &decoder, [&decoder]() { decoder.setTorch(false); });

    QQmlContext *context = view->rootContext();
    context->setContextProperty("decoder", &decoder);
    context->setContextProperty("history", &history);
    context->setContextProperty("settings", &settings);
    context->setContextProperty("trial", &trial);
    context->setContextProperty("productLookup", &productLookup);
    // Build version for the About page (APP_VERSION comes from the .pro,
    // which gets it from the RPM build environment; BUILD_ID is the git
    // hash or a UTC timestamp, so testers can tell builds apart).
    context->setContextProperty("appVersion", QStringLiteral(APP_VERSION));
    context->setContextProperty("buildId", QStringLiteral(BUILD_ID));

    view->setSource(SailfishApp::pathTo("qml/harbour-zendecoder.qml"));

    // QQuickView shows an empty (white) window if the root component fails to
    // load and, unlike QQmlApplicationEngine, does not report the errors
    // itself — so print them here or the failure is invisible in the logs.
    if (view->status() == QQuickView::Error) {
        const QList<QQmlError> errors = view->errors();
        for (int i = 0; i < errors.count(); ++i)
            qWarning() << errors.at(i).toString();
        qWarning() << "Failed to load" << view->source();
    }

    view->showFullScreen();

    return app->exec();
}
