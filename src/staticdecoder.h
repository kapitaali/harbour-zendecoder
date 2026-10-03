/*
 * Static barcode decoding on a worker thread.
 *
 * The vendored zxing-cpp (3rdparty/, readers-only static build, all
 * symbologies) runs here instead of on the UI thread: a 2560-px gallery
 * import would otherwise stall the viewfinder, and the live capture loop
 * would drop its cadence. One decode at a time — Decoder holds its busy
 * flag across the whole static-then-daemon chain, so submit() is never
 * called while a previous decode is still running.
 *
 * Threading: this object is moved to m_thread in the constructor and
 * decode() runs in that thread (queued invoke). submit() is called from
 * the main thread: it writes m_image and posts the event; the queue
 * operation is the memory barrier that makes the write visible. decode()
 * takes a local copy of the image up front, so a later submit() replacing
 * m_image cannot pull the pixels out from under it (QImage's implicit
 * sharing makes the copy free — it just bumps a refcount).
 *
 * The result travels back as resultReady(); the connection to Decoder is
 * queued (different threads), carrying only core metatypes (bool, QString,
 * int) so no metatype registration is needed. Emitting is unconditional —
 * every submitted image produces exactly one resultReady, found or not,
 * including on a decode exception, because Decoder's busy flag is only
 * released when this arrives.
 */
#ifndef STATICDECODER_H
#define STATICDECODER_H

#include <QImage>
#include <QObject>
#include <QString>

class QThread;

class StaticDecoder : public QObject
{
    Q_OBJECT
public:
    /** Starts the worker thread. No parent — a parented object cannot be
     *  moved to another thread; Decoder deletes it explicitly. */
    explicit StaticDecoder(QObject *parent = Q_NULLPTR);
    /** Quits the worker thread and waits for it to finish. */
    ~StaticDecoder() override;

    /**
     * Hand over an image for decoding in the worker thread. Called from
     * Decoder with the busy flag already held — never re-entrant.
     */
    void submit(const QImage &image);

signals:
    /**
     * Exactly one per submit(). elapsedMs measures the zxing call alone
     * (grayscale conversion included, thread hop excluded).
     */
    void resultReady(bool found, const QString &text, const QString &format,
                     int elapsedMs);

private slots:
    /** Worker thread: grayscale-convert, run zxing, emit resultReady. */
    void decode();

private:
    QThread *m_thread;      // owned; quit+wait'd in the destructor
    QImage m_image;         // written by submit(), read once by decode()
};

#endif // STATICDECODER_H
