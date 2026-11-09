#include "Privilege.h"

#include "Backend.h"

#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

constexpr int kIdleMs = 5 * 60 * 1000;

void wipe(QByteArray &b) {
    if (!b.isEmpty())
        std::memset(b.data(), 0, size_t(b.size()));
    b.clear();
}

} // namespace

Privilege::Privilege(QObject *parent)
    : QObject(parent) {
    m_idle.setSingleShot(true);
    m_idle.setInterval(kIdleMs);
    connect(&m_idle, &QTimer::timeout, this, [this] {
        forget();
        emit expired();
    });
}

Privilege::~Privilege() {
    forget();
}

bool Privilege::ready() const {
    return m_server && m_server->isListening() && !m_pw.isEmpty();
}

QString Privilege::askpassProgram() const {
    return QCoreApplication::applicationFilePath();
}

void Privilege::keepAlive() {
    if (ready())
        m_idle.start();
}

void Privilege::forget() {
    m_idle.stop();
    if (m_server) {
        m_server->close();
        QLocalServer::removeServer(m_sockPath);
        m_server->deleteLater();
        m_server = nullptr;
    }
    wipe(m_pw);
}

void Privilege::authenticate(const QString &password) {
    if (m_authBusy)
        return;
    if (password.isEmpty()) {
        emit authenticated(false, tr("Introduce tu contraseña."));
        return;
    }
    const QString sudo = Backend::which(QStringLiteral("sudo"));
    if (sudo.isEmpty()) {
        emit authenticated(false, tr("sudo no está instalado."));
        return;
    }
    m_authBusy = true;
    QByteArray pw = password.toUtf8();

    // 1) forget any cached timestamp so a wrong password cannot slip through,
    // 2) validate with `sudo -S -v` (password on stdin, no prompt text).
    auto *reset = new QProcess(this);
    reset->setProcessEnvironment(Backend::cleanEnv());
    reset->setStandardInputFile(QProcess::nullDevice());
    connect(reset, &QProcess::finished, this, [this, sudo, pw, reset]() mutable {
        reset->deleteLater();
        auto *check = new QProcess(this);
        check->setProcessEnvironment(Backend::cleanEnv());
        auto *timeout = new QTimer(check);
        timeout->setSingleShot(true);
        connect(timeout, &QTimer::timeout, check, [check] { check->kill(); });
        connect(check, &QProcess::finished, this, [this, check, pw](int code, QProcess::ExitStatus) mutable {
            const QString err = QString::fromUtf8(check->readAllStandardError()).toLower();
            check->deleteLater();
            m_authBusy = false;
            if (code == 0) {
                wipe(m_pw);
                m_pw = pw;
                startServer();
                if (!ready()) {
                    forget();
                    emit authenticated(false, tr("No se pudo preparar el canal seguro de contraseña."));
                    return;
                }
                m_idle.start();
                emit authenticated(true, {});
                return;
            }
            wipe(pw);
            QString msg = tr("Contraseña incorrecta");
            if (err.contains(QLatin1String("locked")) || err.contains(QLatin1String("too many")))
                msg = tr("Demasiados intentos fallidos. Espera un minuto e inténtalo de nuevo.");
            else if (err.contains(QLatin1String("not in the sudoers")) || err.contains(QLatin1String("may not run sudo")))
                msg = tr("Este usuario no tiene permisos de sudo.");
            emit authenticated(false, msg);
        });
        check->start(sudo, {QStringLiteral("-S"), QStringLiteral("-p"), QString(), QStringLiteral("-v")});
        timeout->start(20000);
        // Write and close stdin so sudo can never wait for a second attempt.
        check->write(pw + '\n');
        check->closeWriteChannel();
    });
    reset->start(sudo, {QStringLiteral("-k")});
}

void Privilege::startServer() {
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    QString dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_sockPath = QStringLiteral("%1/things-askpass-%2-%3.sock")
                     .arg(dir).arg(QCoreApplication::applicationPid())
                     .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    QLocalServer::removeServer(m_sockPath);
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_sockPath)) {
        m_server->deleteLater();
        m_server = nullptr;
        return;
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (m_server && m_server->hasPendingConnections()) {
            QLocalSocket *c = m_server->nextPendingConnection();
            bool allowed = false;
#ifdef SO_PEERCRED
            struct ucred cred;
            socklen_t len = sizeof(cred);
            if (::getsockopt(int(c->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0)
                allowed = cred.uid == ::getuid();
#endif
            if (allowed && !m_pw.isEmpty()) {
                c->write(m_pw + '\n');
                c->flush();
            }
            c->disconnectFromServer();
            connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
            QTimer::singleShot(2000, c, &QObject::deleteLater);
        }
    });
}

// ───────────────────────── askpass client ─────────────────────────

int Privilege::runAskpass() {
    const char *path = std::getenv("THINGS_ASKPASS_SOCK");
    if (!path || !*path)
        return 1;
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return 1;
    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (std::strlen(path) >= sizeof(addr.sun_path)) {
        ::close(fd);
        return 1;
    }
    std::strcpy(addr.sun_path, path);
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return 1;
    }
    char buf[1024];
    ssize_t total = 0;
    for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0)
            break;
        if (std::fwrite(buf, 1, size_t(n), stdout) != size_t(n))
            break;
        total += n;
    }
    std::fflush(stdout);
    ::close(fd);
    std::memset(buf, 0, sizeof(buf));
    return total > 0 ? 0 : 1;
}
