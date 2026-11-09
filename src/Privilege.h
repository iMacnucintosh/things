#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QTimer>

class QLocalServer;

/// Holds the sudo password in RAM only and serves it to `sudo -A` over a private unix socket.
///
/// Pattern shared with Sylphrena's Updates service, minus the Python helper: the askpass
/// program is this very binary (`things`) started with THINGS_ASKPASS_SOCK set; it connects
/// to the socket, prints the password and exits. Nothing is ever written to disk, the socket
/// lives in $XDG_RUNTIME_DIR (0700) with 0600 permissions and every peer is checked with
/// SO_PEERCRED. The secret is wiped after an idle timeout or on exit.
class Privilege : public QObject {
    Q_OBJECT
public:
    explicit Privilege(QObject *parent = nullptr);
    ~Privilege() override;

    bool ready() const;
    /// Verify with `sudo -S -v`; on success start serving the password.
    void authenticate(const QString &password);
    bool authenticating() const { return m_authBusy; }
    QString socketPath() const { return m_sockPath; }
    QString askpassProgram() const;
    /// Restart the idle countdown (called around each operation).
    void keepAlive();
    void forget();

    /// Askpass client mode. Called from main() before any Qt object exists.
    static int runAskpass();

signals:
    void authenticated(bool ok, const QString &error);
    void expired();

private:
    void startServer();

    QByteArray m_pw;
    QLocalServer *m_server = nullptr;
    QString m_sockPath;
    QTimer m_idle;
    bool m_authBusy = false;
};
