#pragma once

#include "Privilege.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QQueue>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class Backend;

/// Serial install/remove queue with live output, privilege handling and previews.
class Operations : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList queue READ queue NOTIFY queueChanged)
    Q_PROPERTY(bool active READ active NOTIFY queueChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool awaitingPassword READ awaitingPassword NOTIFY passwordStateChanged)
    Q_PROPERTY(bool authBusy READ authBusy NOTIFY passwordStateChanged)
    Q_PROPERTY(QString authError READ authError NOTIFY passwordStateChanged)
    Q_PROPERTY(QString log READ log NOTIFY logChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY runningChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY runningChanged)
    Q_PROPERTY(QString hint READ hint NOTIFY runningChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY runningChanged)
    Q_PROPERTY(bool dryRun READ dryRun CONSTANT)
    Q_PROPERTY(bool preparing READ preparing NOTIFY preparingChanged)

public:
    explicit Operations(Backend *backend, QObject *parent = nullptr);
    ~Operations() override;

    QVariantList queue() const;
    bool active() const;
    bool running() const { return m_proc != nullptr || m_dryTimer.isActive(); }
    bool awaitingPassword() const { return m_awaiting; }
    bool authBusy() const { return m_priv.authenticating(); }
    QString authError() const { return m_authError; }
    QString log() const { return m_logText; }
    QString statusText() const { return m_statusText; }
    qreal progress() const { return m_progress; }
    QString hint() const { return m_hint; }
    QString currentTitle() const;
    bool dryRun() const { return m_dryRun; }
    bool preparing() const { return m_preparing > 0; }
    void setDryRun(bool on) { m_dryRun = on; }
    Privilege *privilege() { return &m_priv; }

    /// kind: "install" | "remove"
    Q_INVOKABLE bool enqueue(const QString &kind, const QString &source, const QString &id,
                             const QString &name, const QString &repo);
    Q_INVOKABLE void cancelCurrent();
    Q_INVOKABLE void cancelQueued(int opId);
    Q_INVOKABLE void dismissFinished();
    Q_INVOKABLE void submitPassword(const QString &password);
    Q_INVOKABLE void cancelPassword();
    /// Async dry-run preview (what would be installed/removed). Result arrives in prepared().
    Q_INVOKABLE void prepare(const QString &kind, const QString &source, const QString &id,
                             const QString &repo);

signals:
    void queueChanged();
    void runningChanged();
    void logChanged();
    void passwordStateChanged();
    void preparingChanged();
    void prepared(const QVariantMap &result);
    /// Emitted after each operation settles.
    void operationFinished(bool ok, const QString &kind, const QString &source, const QString &id);

private:
    struct Op {
        int id = 0;
        QString kind, source, pkg, name, repo;
        QString status = QStringLiteral("queued"); // queued|waiting|running|done|failed|cancelled
        QString error;
    };
    struct Cmd {
        QString program;
        QStringList args;
        bool needsRoot = false;
        QString display;
        QString error; // non-empty: refuse to run
    };

    Op *current();
    void pump();
    void begin(Op &op);
    Cmd buildCommand(const Op &op) const;
    void launch(Op &op);
    void finishCurrent(int code, bool cancelled);
    void appendOutput(const QByteArray &chunk);
    void appendLine(const QString &line, bool replaceLast = false);
    void flushLog();
    QString diagnose(const QString &log) const;
    void runDry(Op &op, const Cmd &cmd);
    void setStatus(const QString &s);
    static QString title(const Op &op);

    Backend *m_backend = nullptr;
    Privilege m_priv;
    QList<Op> m_ops;
    int m_nextId = 1;
    int m_curId = 0;
    QProcess *m_proc = nullptr;
    bool m_cancelRequested = false;
    bool m_awaiting = false;
    QString m_authError;
    QStringList m_logLines;
    QString m_logText;
    bool m_lastProgress = false;
    QString m_partial;
    QTimer m_logTimer;
    QString m_statusText;
    qreal m_progress = -1;
    QString m_hint;
    bool m_dryRun = false;
    QTimer m_dryTimer;
    int m_drySteps = 0;
    int m_preparing = 0;
};
