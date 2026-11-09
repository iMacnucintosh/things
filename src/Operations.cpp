#include "Operations.h"

#include "Backend.h"
#include "Package.h"

#include <QFile>
#include <QRegularExpression>
#include <csignal>
#include <sys/types.h>

namespace {
constexpr int kMaxLogLines = 3000;
const char *kPacmanLock = "/var/lib/pacman/db.lck";
}

Operations::Operations(Backend *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend) {
    m_logTimer.setSingleShot(true);
    m_logTimer.setInterval(90);
    connect(&m_logTimer, &QTimer::timeout, this, &Operations::flushLog);

    m_dryTimer.setInterval(280);
    connect(&m_priv, &Privilege::authenticated, this, [this](bool ok, const QString &err) {
        m_authError = ok ? QString() : err;
        if (ok) {
            m_awaiting = false;
            emit passwordStateChanged();
            pump();
        } else {
            emit passwordStateChanged();
        }
    });
    connect(&m_priv, &Privilege::expired, this, [this] { emit passwordStateChanged(); });
}

Operations::~Operations() {
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(500);
    }
}

QString Operations::title(const Op &op) {
    const QString verb = op.kind == QLatin1String("install") ? QObject::tr("Instalando") : QObject::tr("Eliminando");
    return verb + QLatin1Char(' ') + (op.name.isEmpty() ? op.pkg : op.name);
}

QString Operations::currentTitle() const {
    for (const Op &o : m_ops)
        if (o.id == m_curId)
            return title(o);
    if (!m_ops.isEmpty())
        return title(m_ops.last());
    return {};
}

Operations::Op *Operations::current() {
    for (Op &o : m_ops)
        if (o.id == m_curId)
            return &o;
    return nullptr;
}

QVariantList Operations::queue() const {
    QVariantList out;
    for (const Op &o : m_ops) {
        QVariantMap m;
        m[QStringLiteral("id")] = o.id;
        m[QStringLiteral("kind")] = o.kind;
        m[QStringLiteral("source")] = o.source;
        m[QStringLiteral("pkg")] = o.pkg;
        m[QStringLiteral("name")] = o.name.isEmpty() ? o.pkg : o.name;
        m[QStringLiteral("status")] = o.status;
        m[QStringLiteral("error")] = o.error;
        m[QStringLiteral("title")] = title(o);
        out.append(m);
    }
    return out;
}

bool Operations::active() const {
    for (const Op &o : m_ops)
        if (o.status == QLatin1String("queued") || o.status == QLatin1String("waiting")
            || o.status == QLatin1String("running"))
            return true;
    return false;
}

void Operations::setStatus(const QString &s) {
    if (m_statusText == s)
        return;
    m_statusText = s;
    emit runningChanged();
}

bool Operations::enqueue(const QString &kind, const QString &source, const QString &id,
                         const QString &name, const QString &repo) {
    if ((kind != QLatin1String("install") && kind != QLatin1String("remove"))
        || !Validate::packageId(source, id))
        return false;
    if (source == QLatin1String("flatpak") && kind == QLatin1String("install")
        && !repo.isEmpty() && !Validate::remoteName(repo))
        return false;
    for (const Op &o : m_ops)
        if (o.pkg == id && o.source == source && o.kind == kind
            && (o.status == QLatin1String("queued") || o.status == QLatin1String("running")
                || o.status == QLatin1String("waiting")))
            return false;
    if (!active()) {
        m_ops.clear();
        m_logLines.clear();
        m_logText.clear();
        emit logChanged();
    }
    Op op;
    op.id = m_nextId++;
    op.kind = kind;
    op.source = source;
    op.pkg = id;
    op.name = name;
    op.repo = repo;
    m_ops.append(op);
    emit queueChanged();
    pump();
    return true;
}

void Operations::cancelQueued(int opId) {
    for (Op &o : m_ops) {
        if (o.id == opId && o.status == QLatin1String("queued")) {
            o.status = QStringLiteral("cancelled");
            emit queueChanged();
            return;
        }
    }
}

void Operations::dismissFinished() {
    if (active())
        return;
    m_ops.clear();
    m_logLines.clear();
    m_logText.clear();
    m_hint.clear();
    m_progress = -1;
    m_statusText.clear();
    emit queueChanged();
    emit logChanged();
    emit runningChanged();
}

void Operations::pump() {
    if (running() || m_awaiting)
        return;
    for (Op &o : m_ops) {
        if (o.status == QLatin1String("queued") || o.status == QLatin1String("waiting")) {
            begin(o);
            return;
        }
    }
    // Queue drained.
    m_priv.keepAlive();
    emit runningChanged();
}

Operations::Cmd Operations::buildCommand(const Op &op) const {
    Cmd c;
    const QString sudo = QStringLiteral("sudo");
    const bool install = op.kind == QLatin1String("install");
    if (op.source == QLatin1String("pacman") || (op.source == QLatin1String("aur") && !install)) {
        if (!m_backend->hasPacman()) {
            c.error = tr("pacman no está disponible.");
            return c;
        }
        c.program = sudo;
        c.needsRoot = true;
        c.args = {QStringLiteral("-A"), QStringLiteral("pacman")};
        if (install)
            c.args << QStringLiteral("-S") << QStringLiteral("--needed");
        else
            c.args << QStringLiteral("-Rns");
        c.args << QStringLiteral("--noconfirm") << QStringLiteral("--color") << QStringLiteral("never")
               << QStringLiteral("--") << op.pkg;
    } else if (op.source == QLatin1String("aur")) {
        if (!m_backend->hasYay()) {
            c.error = tr("yay no está instalado: no se puede instalar desde AUR.");
            return c;
        }
        // --sudoflags=-A routes every sudo call through askpass. Never --sudoloop (faillock).
        c.program = QStringLiteral("yay");
        c.needsRoot = true;
        c.args = {QStringLiteral("-S"), QStringLiteral("--needed"), QStringLiteral("--noconfirm"),
                  QStringLiteral("--sudoflags=-A"), QStringLiteral("--color"), QStringLiteral("never"),
                  QStringLiteral("--answerclean"), QStringLiteral("None"),
                  QStringLiteral("--answerdiff"), QStringLiteral("None"),
                  QStringLiteral("--answeredit"), QStringLiteral("None"),
                  QStringLiteral("--answerupgrade"), QStringLiteral("None"),
                  QStringLiteral("--"), QStringLiteral("aur/") + op.pkg};
    } else {
        if (!m_backend->hasFlatpak()) {
            c.error = tr("flatpak no está instalado.");
            return c;
        }
        QString where = install ? m_backend->flatpakInstallation() : m_backend->flatpakInstalledIn(op.pkg);
        if (where.isEmpty())
            where = QStringLiteral("system");
        const bool user = where.contains(QLatin1String("user"));
        QStringList fargs;
        if (install) {
            const QString remote = op.repo.isEmpty() ? m_backend->defaultRemote() : op.repo;
            fargs = {QStringLiteral("install"), user ? QStringLiteral("--user") : QStringLiteral("--system"),
                     QStringLiteral("-y"), QStringLiteral("--noninteractive"), remote, op.pkg};
        } else {
            fargs = {QStringLiteral("uninstall"), user ? QStringLiteral("--user") : QStringLiteral("--system"),
                     QStringLiteral("-y"), QStringLiteral("--noninteractive"), op.pkg};
        }
        if (user) {
            c.program = QStringLiteral("flatpak");
            c.args = fargs;
        } else {
            c.program = sudo;
            c.needsRoot = true;
            c.args = QStringList{QStringLiteral("-A"), QStringLiteral("flatpak")} + fargs;
        }
    }
    c.display = c.program + QLatin1Char(' ') + c.args.join(QLatin1Char(' '));
    return c;
}

QString Operations::diagnose(const QString &log) const {
    const QString l = log.toLower();
    if (l.contains(QLatin1String("db.lck")) || l.contains(QLatin1String("unable to lock database")))
        return tr("Otra instancia de pacman está en ejecución o quedó un bloqueo (/var/lib/pacman/db.lck).");
    if (l.contains(QLatin1String("failed retrieving file")) || l.contains(QLatin1String("error 404"))
        || l.contains(QLatin1String("404")))
        return tr("Las bases de datos de pacman están desactualizadas. Actualiza el sistema (módulo Updates de Nexus) y reintenta.");
    if (l.contains(QLatin1String("could not satisfy dependencies")) || l.contains(QLatin1String("breaks dependency")))
        return tr("Hay dependencias sin resolver: otro paquete instalado depende de éste.");
    if (l.contains(QLatin1String("conflicting files")) || l.contains(QLatin1String("exists in filesystem")))
        return tr("Conflicto de archivos con otro paquete instalado.");
    if (l.contains(QLatin1String("target not found")) || l.contains(QLatin1String("no package found")))
        return tr("El paquete no se encontró. Puede que las bases de datos estén desactualizadas.");
    if (l.contains(QLatin1String("signature")) || l.contains(QLatin1String("pgp")) || l.contains(QLatin1String("keyring")))
        return tr("Problema de firmas o claves PGP. Revisa el llavero de pacman.");
    if (l.contains(QLatin1String("no space left")))
        return tr("No queda espacio en disco.");
    if (l.contains(QLatin1String("a password is required")) || l.contains(QLatin1String("incorrect password"))
        || l.contains(QLatin1String("sorry, try again")))
        return tr("sudo rechazó la contraseña. Vuelve a intentarlo.");
    if (l.contains(QLatin1String("error: failed to build")) || l.contains(QLatin1String("makepkg")))
        return tr("Falló la compilación del paquete de AUR. Revisa la salida.");
    return {};
}

void Operations::begin(Op &op) {
    m_curId = op.id;
    m_cancelRequested = false;
    m_hint.clear();
    m_progress = -1;
    m_logLines.clear();
    m_logText.clear();
    emit logChanged();
    appendLine(QStringLiteral("→ ") + title(op));

    const Cmd cmd = buildCommand(op);
    if (!cmd.error.isEmpty()) {
        appendLine(cmd.error);
        op.status = QStringLiteral("failed");
        op.error = cmd.error;
        m_hint = cmd.error;
        emit queueChanged();
        emit runningChanged();
        emit operationFinished(false, op.kind, op.source, op.pkg);
        QTimer::singleShot(0, this, &Operations::pump);
        return;
    }

    if (m_dryRun) {
        op.status = QStringLiteral("running");
        emit queueChanged();
        runDry(op, cmd);
        return;
    }

    if (cmd.needsRoot && !m_priv.ready()) {
        op.status = QStringLiteral("waiting");
        m_awaiting = true;
        m_authError.clear();
        setStatus(tr("Esperando autorización…"));
        emit queueChanged();
        emit passwordStateChanged();
        return;
    }
    launch(op);
}

void Operations::launch(Op &op) {
    const Cmd cmd = buildCommand(op);

    // pacman database lock: report it clearly instead of letting the tool fail late.
    if (cmd.program != QLatin1String("flatpak") && QFile::exists(QString::fromLatin1(kPacmanLock))) {
        QProcess pg2;
        pg2.start(QStringLiteral("pgrep"), {QStringLiteral("-x"), QStringLiteral("pacman")});
        pg2.waitForFinished(1500);
        QProcess pg3;
        pg3.start(QStringLiteral("pgrep"), {QStringLiteral("-x"), QStringLiteral("yay")});
        pg3.waitForFinished(1500);
        const bool running = pg2.exitCode() == 0 || pg3.exitCode() == 0;
        const QString msg = running
            ? tr("Otra instancia de pacman o yay está en ejecución. Espera a que termine e inténtalo de nuevo.")
            : tr("Existe un bloqueo huérfano en %1. Si no hay otro gestor de paquetes en marcha, elimínalo con: sudo rm %1")
                  .arg(QString::fromLatin1(kPacmanLock));
        appendLine(msg);
        op.status = QStringLiteral("failed");
        op.error = msg;
        m_hint = msg;
        emit queueChanged();
        emit runningChanged();
        emit operationFinished(false, op.kind, op.source, op.pkg);
        QTimer::singleShot(0, this, &Operations::pump);
        return;
    }

    op.status = QStringLiteral("running");
    setStatus(tr("Iniciando…"));
    emit queueChanged();

    appendLine(QStringLiteral("$ ") + cmd.display);
    m_proc = new QProcess(this);
    QProcessEnvironment env = Backend::cleanEnv();
    if (cmd.needsRoot) {
        env.insert(QStringLiteral("SUDO_ASKPASS"), m_priv.askpassProgram());
        env.insert(QStringLiteral("THINGS_ASKPASS_SOCK"), m_priv.socketPath());
        m_priv.keepAlive();
    }
    m_proc->setProcessEnvironment(env);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    // A child that inherits our stdin can hang waiting on it: always /dev/null.
    m_proc->setStandardInputFile(QProcess::nullDevice());
    connect(m_proc, &QProcess::readyRead, this, [this] { appendOutput(m_proc->readAll()); });
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        appendOutput(m_proc->readAll());
        finishCurrent(st == QProcess::NormalExit ? code : -1, m_cancelRequested);
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            appendLine(tr("No se pudo iniciar: %1").arg(m_proc->errorString()));
            finishCurrent(-2, false);
        }
    });
    m_proc->start(cmd.program, cmd.args);
    emit runningChanged();
}

void Operations::runDry(Op &op, const Cmd &cmd) {
    appendLine(QStringLiteral("[dry-run] ") + cmd.display);
    m_drySteps = 0;
    m_dryTimer.disconnect(this);
    connect(&m_dryTimer, &QTimer::timeout, this, [this, id = op.id] {
        ++m_drySteps;
        appendLine(QStringLiteral("[dry-run] paso %1/5").arg(m_drySteps));
        m_progress = m_drySteps / 5.0;
        setStatus(QStringLiteral("[dry-run] paso %1/5").arg(m_drySteps));
        emit runningChanged();
        if (m_drySteps >= 5) {
            m_dryTimer.stop();
            Op *o = current();
            if (!o || o->id != id)
                return;
            const bool fail = qEnvironmentVariableIsSet("THINGS_DRY_FAIL");
            if (fail) {
                appendLine(QStringLiteral("error: simulated failure (THINGS_DRY_FAIL)"));
                o->status = QStringLiteral("failed");
                o->error = tr("Fallo simulado.");
                m_hint = tr("Fallo simulado (THINGS_DRY_FAIL).");
            } else {
                appendLine(QStringLiteral("[dry-run] listo, nada se ha modificado."));
                o->status = QStringLiteral("done");
            }
            m_progress = fail ? m_progress : 1.0;
            emit queueChanged();
            emit runningChanged();
            emit operationFinished(!fail, o->kind, o->source, o->pkg);
            QTimer::singleShot(150, this, &Operations::pump);
        }
    });
    m_dryTimer.start();
    emit runningChanged();
}

void Operations::cancelCurrent() {
    Op *op = current();
    if (!op)
        return;
    if (m_awaiting) {
        cancelPassword();
        return;
    }
    if (m_dryTimer.isActive()) {
        m_dryTimer.stop();
        appendLine(tr("Cancelado."));
        op->status = QStringLiteral("cancelled");
        emit queueChanged();
        emit runningChanged();
        QTimer::singleShot(0, this, &Operations::pump);
        return;
    }
    if (!m_proc)
        return;
    m_cancelRequested = true;
    appendLine(tr("Cancelando…"));
    // SIGINT first: pacman/yay abort a transaction cleanly. sudo relays the signal.
    ::kill(pid_t(m_proc->processId()), SIGINT);
    QPointer<QProcess> p = m_proc;
    QTimer::singleShot(4000, this, [p] { if (p && p->state() != QProcess::NotRunning) p->terminate(); });
    QTimer::singleShot(9000, this, [p] { if (p && p->state() != QProcess::NotRunning) p->kill(); });
}

void Operations::finishCurrent(int code, bool cancelled) {
    Op *op = current();
    QProcess *p = m_proc;
    m_proc = nullptr;
    if (p)
        p->deleteLater();
    if (!op) {
        QTimer::singleShot(0, this, &Operations::pump);
        return;
    }
    flushLog();
    const bool ok = code == 0 && !cancelled;
    if (cancelled) {
        op->status = QStringLiteral("cancelled");
        appendLine(tr("Operación cancelada."));
    } else if (ok) {
        op->status = QStringLiteral("done");
        appendLine(tr("Operación completada."));
        m_progress = 1.0;
    } else {
        op->status = QStringLiteral("failed");
        appendLine(tr("La operación falló (código %1).").arg(code));
        m_hint = diagnose(m_logLines.join(QLatin1Char('\n')));
        op->error = m_hint.isEmpty() ? tr("La operación falló. Revisa la salida.") : m_hint;
    }
    flushLog();
    m_priv.keepAlive();
    emit queueChanged();
    emit runningChanged();
    emit operationFinished(ok, op->kind, op->source, op->pkg);
    QTimer::singleShot(250, this, &Operations::pump);
}

void Operations::submitPassword(const QString &password) {
    if (!m_awaiting)
        return;
    m_authError.clear();
    emit passwordStateChanged();
    m_priv.authenticate(password);
    emit passwordStateChanged();
}

void Operations::cancelPassword() {
    if (!m_awaiting)
        return;
    m_awaiting = false;
    m_authError.clear();
    // Everything that was waiting on this password is cancelled with it.
    for (Op &o : m_ops) {
        if (o.status == QLatin1String("waiting") || o.status == QLatin1String("queued")) {
            o.status = QStringLiteral("cancelled");
        }
    }
    appendLine(tr("Autorización cancelada."));
    setStatus(QString());
    emit queueChanged();
    emit passwordStateChanged();
    emit runningChanged();
}

// ───────────────────────── output ─────────────────────────

void Operations::appendOutput(const QByteArray &chunk) {
    m_partial += QString::fromUtf8(chunk);
    // Split on \n; a \r inside a line means an in-place progress update.
    int nl;
    while ((nl = m_partial.indexOf(QLatin1Char('\n'))) >= 0) {
        QString line = m_partial.left(nl);
        m_partial.remove(0, nl + 1);
        const QStringList segs = line.split(QLatin1Char('\r'));
        for (int i = 0; i < segs.size(); ++i) {
            const QString s = segs.at(i);
            if (s.trimmed().isEmpty() && i < segs.size() - 1)
                continue;
            appendLine(s, i > 0);
        }
    }
    // Pending text ending with \r (no newline yet): show the latest progress segment.
    if (!m_partial.isEmpty() && m_partial.contains(QLatin1Char('\r'))) {
        const QStringList segs = m_partial.split(QLatin1Char('\r'), Qt::SkipEmptyParts);
        if (!segs.isEmpty()) {
            appendLine(segs.last(), true);
            m_partial.clear();
        }
    }
}

void Operations::appendLine(const QString &raw, bool replaceLast) {
    QString line = raw;
    line.remove(QRegularExpression(QStringLiteral("\x1b\\[[0-9;?]*[A-Za-z]")));
    if (line.trimmed().isEmpty() && !replaceLast)
        return;

    static const QRegularExpression pacProg(QStringLiteral("\\(\\s*(\\d+)/(\\d+)\\)"));
    static const QRegularExpression pct(QStringLiteral("(\\d{1,3})%"));
    const auto pm = pacProg.match(line);
    if (pm.hasMatch() && pm.captured(2).toInt() > 0) {
        m_progress = qBound<qreal>(0, pm.captured(1).toDouble() / pm.captured(2).toDouble(), 1);
    } else {
        const auto fm = pct.match(line);
        if (fm.hasMatch() && m_backend && current() && current()->source == QLatin1String("flatpak"))
            m_progress = qBound<qreal>(0, fm.captured(1).toDouble() / 100.0, 1);
    }

    if (replaceLast && m_lastProgress && !m_logLines.isEmpty())
        m_logLines.last() = line;
    else
        m_logLines.append(line);
    m_lastProgress = replaceLast;
    if (m_logLines.size() > kMaxLogLines)
        m_logLines.remove(0, m_logLines.size() - kMaxLogLines);
    const QString t = line.trimmed();
    if (!t.isEmpty())
        m_statusText = t.left(140);
    if (!m_logTimer.isActive())
        m_logTimer.start();
}

void Operations::flushLog() {
    m_logText = m_logLines.join(QLatin1Char('\n'));
    emit logChanged();
    emit runningChanged();
}

// ───────────────────────── previews ─────────────────────────

void Operations::prepare(const QString &kind, const QString &source, const QString &id, const QString &repo) {
    QVariantMap res;
    res[QStringLiteral("kind")] = kind;
    res[QStringLiteral("source")] = source;
    res[QStringLiteral("id")] = id;
    res[QStringLiteral("ok")] = true;
    res[QStringLiteral("packages")] = QVariantList();
    res[QStringLiteral("total")] = 0.0;
    res[QStringLiteral("note")] = QString();
    res[QStringLiteral("error")] = QString();

    if (!Validate::packageId(source, id) || (kind != QLatin1String("install") && kind != QLatin1String("remove"))) {
        res[QStringLiteral("ok")] = false;
        res[QStringLiteral("error")] = tr("Paquete no válido.");
        emit prepared(res);
        return;
    }

    auto finish = [this](QVariantMap r) {
        --m_preparing;
        emit preparingChanged();
        emit prepared(r);
    };
    ++m_preparing;
    emit preparingChanged();

    const bool install = kind == QLatin1String("install");

    if (source == QLatin1String("aur") && install) {
        QVariantMap p;
        p[QStringLiteral("name")] = id;
        p[QStringLiteral("size")] = 0.0;
        res[QStringLiteral("packages")] = QVariantList{p};
        res[QStringLiteral("note")] = tr("Se descargará el PKGBUILD de AUR y se compilará con yay. Los paquetes de AUR los mantiene la comunidad: revisa el PKGBUILD si no confías en el origen.");
        QTimer::singleShot(0, this, [=, this]() { finish(res); });
        return;
    }
    if (source == QLatin1String("flatpak")) {
        if (!install) {
            QVariantMap p;
            p[QStringLiteral("name")] = id;
            p[QStringLiteral("size")] = 0.0;
            res[QStringLiteral("packages")] = QVariantList{p};
            QTimer::singleShot(0, this, [=, this]() { finish(res); });
            return;
        }
        const QString remote = Validate::remoteName(repo) && !repo.isEmpty() ? repo : m_backend->defaultRemote();
        auto *p = new QProcess(this);
        p->setProcessEnvironment(Backend::cleanEnv());
        p->setStandardInputFile(QProcess::nullDevice());
        connect(p, &QProcess::finished, this, [=, this]() mutable {
            const QString out = QString::fromUtf8(p->readAllStandardOutput());
            p->deleteLater();
            QVariantMap pkg;
            pkg[QStringLiteral("name")] = id;
            pkg[QStringLiteral("size")] = 0.0;
            static const QRegularExpression dl(QStringLiteral("^\\s*Download Size:\\s*(.+)$"), QRegularExpression::MultilineOption);
            static const QRegularExpression ins(QStringLiteral("^\\s*Installed Size:\\s*(.+)$"), QRegularExpression::MultilineOption);
            const auto a = dl.match(out);
            const auto b = ins.match(out);
            QString note = m_backend->flatpakInstallation() == QLatin1String("system")
                ? tr("Se instalará para todo el sistema desde «%1».").arg(remote)
                : tr("Se instalará solo para tu usuario desde «%1».").arg(remote);
            if (a.hasMatch())
                note += QLatin1Char(' ') + tr("Descarga: %1.").arg(a.captured(1).trimmed());
            if (b.hasMatch())
                note += QLatin1Char(' ') + tr("Ocupará: %1.").arg(b.captured(1).trimmed());
            note += QLatin1Char(' ') + tr("Flatpak puede descargar además los runtimes que necesite.");
            res[QStringLiteral("packages")] = QVariantList{pkg};
            res[QStringLiteral("note")] = note;
            finish(res);
        });
        p->start(QStringLiteral("flatpak"), {QStringLiteral("remote-info"), remote, id});
        QTimer::singleShot(20000, p, [p] { if (p->state() != QProcess::NotRunning) p->kill(); });
        return;
    }

    // pacman / AUR-remove: ask pacman itself what would happen (needs no root).
    auto *p = new QProcess(this);
    p->setProcessEnvironment(Backend::cleanEnv());
    p->setStandardInputFile(QProcess::nullDevice());
    QStringList args = {install ? QStringLiteral("-S") : QStringLiteral("-Rns"), QStringLiteral("--print"),
                        QStringLiteral("--print-format"), install ? QStringLiteral("%n %v %s") : QStringLiteral("%n %v"),
                        QStringLiteral("--color"), QStringLiteral("never")};
    if (install)
        args << QStringLiteral("--needed");
    args << QStringLiteral("--") << id;
    connect(p, &QProcess::finished, this, [=, this](int code, QProcess::ExitStatus) mutable {
        const QString out = QString::fromUtf8(p->readAllStandardOutput());
        const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
        p->deleteLater();
        if (code != 0) {
            res[QStringLiteral("ok")] = false;
            QString e = err.isEmpty() ? tr("pacman no ha podido calcular la operación.") : err;
            res[QStringLiteral("error")] = e;
            finish(res);
            return;
        }
        QVariantList list;
        double total = 0;
        for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QStringList c = line.trimmed().split(QLatin1Char(' '));
            if (c.size() < 2)
                continue;
            QVariantMap pk;
            pk[QStringLiteral("name")] = c.at(0);
            pk[QStringLiteral("version")] = c.at(1);
            const double sz = c.value(2).toDouble();
            pk[QStringLiteral("size")] = sz;
            total += sz;
            list.append(pk);
        }
        res[QStringLiteral("packages")] = list;
        res[QStringLiteral("total")] = total;
        if (list.isEmpty() && install)
            res[QStringLiteral("note")] = tr("Ya está instalado y actualizado.");
        finish(res);
    });
    p->start(QStringLiteral("pacman"), args);
    QTimer::singleShot(20000, p, [p] { if (p->state() != QProcess::NotRunning) p->kill(); });
}
