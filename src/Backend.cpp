#include "Backend.h"

#include <QCoreApplication>

#include <QDateTime>
#include <QFileInfo>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kMaxResults = 600;

QString lc(const QString &s) { return s.toLower(); }

qint64 parseSize(const QString &s) {
    static const QRegularExpression re(QStringLiteral("^([\\d.,]+)\\s*(B|KiB|MiB|GiB|TiB)$"));
    const auto m = re.match(s.trimmed());
    if (!m.hasMatch())
        return 0;
    QString num = m.captured(1);
    num.replace(QLatin1Char(','), QLatin1Char('.'));
    const double v = num.toDouble();
    const QString u = m.captured(2);
    double mult = 1;
    if (u == QLatin1String("KiB")) mult = 1024.0;
    else if (u == QLatin1String("MiB")) mult = 1024.0 * 1024;
    else if (u == QLatin1String("GiB")) mult = 1024.0 * 1024 * 1024;
    else if (u == QLatin1String("TiB")) mult = 1024.0 * 1024 * 1024 * 1024;
    return qint64(v * mult);
}

QString escapeRegex(const QString &s) {
    static const QRegularExpression special(QStringLiteral("([.^$*+?()\\[\\]{}|\\\\])"));
    QString out = s;
    out.replace(special, QStringLiteral("\\\\1"));
    return out;
}

QString lastSegment(const QString &id) {
    const int i = id.lastIndexOf(QLatin1Char('.'));
    return i >= 0 ? id.mid(i + 1) : id;
}

} // namespace

QProcessEnvironment Backend::cleanEnv() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Stable, untranslated output so the parsers below never depend on the user's locale.
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    env.insert(QStringLiteral("LANG"), QStringLiteral("C.UTF-8"));
    env.insert(QStringLiteral("TERM"), QStringLiteral("dumb"));
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    return env;
}

QString Backend::which(const QString &prog) {
    return QStandardPaths::findExecutable(prog);
}

Backend::Backend(QObject *parent)
    : QObject(parent) {
    m_hasPacman = !which(QStringLiteral("pacman")).isEmpty();
    m_hasYay = !which(QStringLiteral("yay")).isEmpty();
    m_hasFlatpak = !which(QStringLiteral("flatpak")).isEmpty();

    m_net.setTransferTimeout(9000);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(250);
    connect(&m_debounce, &QTimer::timeout, this, [this] { startSearch(); });

    m_counts = {{QStringLiteral("all"), 0}, {QStringLiteral("pacman"), 0},
                {QStringLiteral("aur"), 0}, {QStringLiteral("flatpak"), 0}};

    if (m_hasFlatpak)
        loadFlatpakRemotes();
    loadInstalled();
    loadShellUpdates();
}

Backend::~Backend() {
    cancelSearch();
}

// ───────────────────────── process helper ─────────────────────────

QProcess *Backend::run(const QString &prog, const QStringList &args, Callback cb, int timeoutMs,
                       QList<QPointer<QProcess>> *bucket) {
    auto *p = new QProcess(this);
    p->setProcessEnvironment(cleanEnv());
    p->setStandardInputFile(QProcess::nullDevice());
    auto *timer = new QTimer(p);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, p, [p] { p->kill(); });
    connect(p, &QProcess::finished, this, [p, cb](int code, QProcess::ExitStatus st) {
        const QByteArray out = p->readAllStandardOutput();
        const QByteArray err = p->readAllStandardError();
        p->deleteLater();
        cb(st == QProcess::NormalExit ? code : -1, out, err);
    });
    connect(p, &QProcess::errorOccurred, this, [p, cb](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            cb(-2, {}, p->errorString().toUtf8());
        }
    });
    p->start(prog, args);
    timer->start(timeoutMs);
    if (bucket)
        bucket->append(p);
    return p;
}

// ───────────────────────── properties ─────────────────────────

void Backend::setQuery(const QString &q) {
    if (q == m_query)
        return;
    m_query = q;
    emit queryChanged();
    if (m_mode == QLatin1String("installed")) {
        rebuild();
    } else {
        // Instant feedback: the previous list stays until the debounce fires.
        m_debounce.start();
    }
}

void Backend::setMode(const QString &m) {
    if (m == m_mode || (m != QLatin1String("search") && m != QLatin1String("installed")))
        return;
    m_mode = m;
    emit modeChanged();
    emit searchingChanged();
    if (m_mode == QLatin1String("search")) {
        m_debounce.stop();
        startSearch();
    } else {
        m_debounce.stop();
        rebuild();
    }
}

void Backend::setSourceFilter(const QString &s) {
    if (s == m_sourceFilter)
        return;
    m_sourceFilter = s;
    emit filtersChanged();
    rebuild();
}

void Backend::setStateFilter(const QString &s) {
    if (s == m_stateFilter)
        return;
    m_stateFilter = s;
    emit filtersChanged();
    rebuild();
}

void Backend::setKindFilter(const QString &s) {
    if (s == m_kindFilter)
        return;
    m_kindFilter = s;
    emit filtersChanged();
    rebuild();
}

QString Backend::flatpakInstalledIn(const QString &appId) const {
    return m_flatpakInstalledIn.value(appId);
}

bool Backend::isInstalled(const QString &source, const QString &id) const {
    if (source == QLatin1String("flatpak"))
        return m_instFlatpak.contains(id);
    if (source == QLatin1String("aur"))
        return m_instPacman.contains(id) && m_foreign.contains(id);
    return m_instPacman.contains(id) && !m_foreign.contains(id);
}

QString Backend::formatSize(double bytes) const {
    if (bytes <= 0)
        return {};
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    int u = 0;
    while (bytes >= 1024 && u < 4) {
        bytes /= 1024;
        ++u;
    }
    return QStringLiteral("%1 %2").arg(bytes, 0, 'f', u == 0 ? 0 : (bytes < 10 ? 2 : 1)).arg(QLatin1String(units[u]));
}

QString Backend::iconFor(const QString &source, const QString &id) {
    const QString key = source + QLatin1Char(':') + id;
    auto it = m_iconCache.constFind(key);
    if (it != m_iconCache.constEnd())
        return *it;
    QString found;
    QStringList candidates;
    if (source == QLatin1String("flatpak")) {
        candidates << id << lc(lastSegment(id));
    } else {
        candidates << id;
    }
    for (const QString &c : candidates) {
        if (!c.isEmpty() && QIcon::hasThemeIcon(c)) {
            found = c;
            break;
        }
    }
    m_iconCache.insert(key, found);
    return found;
}

// ───────────────────────── search ─────────────────────────

void Backend::cancelSearch() {
    for (auto &p : m_searchProcs) {
        if (p) {
            p->disconnect(this);
            p->kill();
            p->deleteLater();
        }
    }
    m_searchProcs.clear();
    for (auto &r : m_searchReplies) {
        if (r) {
            r->disconnect(this);
            r->abort();
            r->deleteLater();
        }
    }
    m_searchReplies.clear();
    const bool was = m_pending > 0;
    m_pending = 0;
    if (was)
        emit searchingChanged();
}

void Backend::retrySearch() {
    if (m_mode == QLatin1String("search"))
        startSearch();
}

void Backend::startSearch() {
    cancelSearch();
    ++m_gen;
    m_bySource.clear();
    m_notice.clear();
    emit noticeChanged();

    const QString q = m_query.trimmed();
    m_tokens = q.toLower().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (q.size() < 2) {
        rebuild();
        emit settled();
        return;
    }

    const int gen = m_gen;
    // Cap the amount of tokens: they go straight to argv of external tools.
    QStringList tokens = m_tokens.mid(0, 6);

    int started = 0;
    if (m_hasPacman) { ++started; ++m_pending; }
    if (m_hasFlatpak) { ++started; ++m_pending; }
    ++started; ++m_pending; // AUR (RPC, or yay as fallback)
    emit searchingChanged();

    if (m_hasPacman)
        searchPacman(gen, tokens);
    searchAur(gen, tokens);
    if (m_hasFlatpak)
        searchFlatpak(gen, q);
    Q_UNUSED(started)
}

void Backend::finishOne(int gen) {
    if (gen != m_gen)
        return;
    if (m_pending > 0)
        --m_pending;
    if (m_pending == 0) {
        emit searchingChanged();
        emit settled();
    }
}

void Backend::ingest(int gen, const QString &source, QVector<Package> pkgs, const QString &err) {
    if (gen != m_gen)
        return;
    if (!err.isEmpty()) {
        m_notice = m_notice.isEmpty() ? err : m_notice + QLatin1Char('\n') + err;
        emit noticeChanged();
    }
    m_bySource[source] = std::move(pkgs);
    rebuild();
    finishOne(gen);
}

void Backend::searchPacman(int gen, const QStringList &tokens) {
    QStringList args = {QStringLiteral("-Ss"), QStringLiteral("--color"), QStringLiteral("never"),
                        QStringLiteral("--")};
    for (const QString &t : tokens)
        args << escapeRegex(t);
    run(QStringLiteral("pacman"), args, [this, gen](int code, const QByteArray &out, const QByteArray &) {
        if (gen != m_gen)
            return;
        // pacman -Ss exits 1 when nothing matches.
        ingest(gen, QStringLiteral("pacman"), parsePacmanSearch(out),
               code > 1 || code < 0 ? tr("pacman could not search.") : QString());
    }, 20000, &m_searchProcs);
}

void Backend::searchAur(int gen, const QStringList &tokens) {
    QString longest;
    for (const QString &t : tokens)
        if (t.size() > longest.size())
            longest = t;
    QUrl url(QStringLiteral("https://aur.archlinux.org/rpc/v5/search/")
             + QString::fromLatin1(QUrl::toPercentEncoding(longest)));
    QUrlQuery qq;
    qq.addQueryItem(QStringLiteral("by"), QStringLiteral("name-desc"));
    url.setQuery(qq);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("things/0.1 (Arch package manager)"));
    QNetworkReply *reply = m_net.get(req);
    m_searchReplies.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, gen, reply, tokens] {
        reply->deleteLater();
        if (gen != m_gen)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            if (m_hasYay) {
                searchAurViaYay(gen, tokens);
            } else {
                ingest(gen, QStringLiteral("aur"), {},
                       tr("AUR is not available (offline)."));
            }
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject root = doc.object();
        if (root.value(QStringLiteral("type")).toString() == QLatin1String("error")) {
            // e.g. "Too many package results" for very short terms: not a failure of AUR.
            ingest(gen, QStringLiteral("aur"), {},
                   root.value(QStringLiteral("error")).toString().contains(QLatin1String("Too many"))
                       ? tr("AUR: too many results, narrow your search.") : QString());
            return;
        }
        QVector<Package> out;
        const QJsonArray arr = root.value(QStringLiteral("results")).toArray();
        out.reserve(arr.size());
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            Package p;
            p.id = p.name = o.value(QStringLiteral("Name")).toString();
            p.version = o.value(QStringLiteral("Version")).toString();
            p.description = o.value(QStringLiteral("Description")).toString();
            p.source = QStringLiteral("aur");
            p.repo = QStringLiteral("aur");
            p.votes = o.value(QStringLiteral("NumVotes")).toInt();
            p.popularity = o.value(QStringLiteral("Popularity")).toDouble();
            p.outOfDate = !o.value(QStringLiteral("OutOfDate")).isNull()
                && o.value(QStringLiteral("OutOfDate")).toInt() > 0;
            if (p.id.isEmpty() || !Validate::pacmanName(p.id))
                continue;
            // Remaining tokens must also match (the RPC only takes one keyword).
            const QString hay = lc(p.id + QLatin1Char(' ') + p.description);
            bool ok = true;
            for (const QString &t : tokens)
                if (!hay.contains(t)) { ok = false; break; }
            if (ok)
                out.append(p);
        }
        ingest(gen, QStringLiteral("aur"), out);
    });
}

void Backend::searchAurViaYay(int gen, const QStringList &tokens) {
    QStringList args = {QStringLiteral("-Ssa"), QStringLiteral("--color"), QStringLiteral("never"),
                        QStringLiteral("--")};
    for (const QString &t : tokens)
        args << escapeRegex(t);
    run(QStringLiteral("yay"), args, [this, gen](int code, const QByteArray &out, const QByteArray &) {
        if (gen != m_gen)
            return;
        ingest(gen, QStringLiteral("aur"), parseYaySearch(out),
               code < 0 ? tr("AUR is not available.") : QString());
    }, 25000, &m_searchProcs);
}

void Backend::searchFlatpak(int gen, const QString &query) {
    const QStringList args = {QStringLiteral("search"),
                              QStringLiteral("--columns=application,name,description,version,remotes"),
                              QStringLiteral("--"), query};
    run(QStringLiteral("flatpak"), args, [this, gen](int code, const QByteArray &out, const QByteArray &) {
        if (gen != m_gen)
            return;
        ingest(gen, QStringLiteral("flatpak"), parseFlatpakTable(out, m_defaultRemote, true),
               code < 0 ? tr("Flatpak could not search.") : QString());
    }, 25000, &m_searchProcs);
}

QVector<Package> Backend::parsePacmanSearch(const QByteArray &out) {
    static const QRegularExpression head(QStringLiteral("^(\\S+?)/(\\S+) (\\S+)(.*)$"));
    QVector<Package> res;
    const QStringList lines = QString::fromUtf8(out).split(QLatin1Char('\n'));
    Package cur;
    bool open = false;
    auto flush = [&] {
        if (open && Validate::pacmanName(cur.id))
            res.append(cur);
        open = false;
    };
    for (const QString &line : lines) {
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t'))) {
            if (open) {
                if (!cur.description.isEmpty())
                    cur.description += QLatin1Char(' ');
                cur.description += line.trimmed();
            }
            continue;
        }
        const auto m = head.match(line);
        if (!m.hasMatch())
            continue;
        flush();
        cur = Package();
        cur.repo = m.captured(1);
        cur.id = cur.name = m.captured(2);
        cur.version = m.captured(3);
        cur.source = QStringLiteral("pacman");
        open = true;
    }
    flush();
    return res;
}

QVector<Package> Backend::parseYaySearch(const QByteArray &out) {
    static const QRegularExpression head(
        QStringLiteral("^aur/(\\S+) (\\S+) \\(\\+(\\d+) ([\\d.,]+)\\)(.*)$"));
    QVector<Package> res;
    Package cur;
    bool open = false;
    auto flush = [&] {
        if (open && Validate::pacmanName(cur.id))
            res.append(cur);
        open = false;
    };
    for (const QString &line : QString::fromUtf8(out).split(QLatin1Char('\n'))) {
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char(' '))) {
            if (open) {
                if (!cur.description.isEmpty())
                    cur.description += QLatin1Char(' ');
                cur.description += line.trimmed();
            }
            continue;
        }
        const auto m = head.match(line);
        if (!m.hasMatch())
            continue;
        flush();
        cur = Package();
        cur.id = cur.name = m.captured(1);
        cur.version = m.captured(2);
        cur.votes = m.captured(3).toInt();
        QString pop = m.captured(4);
        pop.replace(QLatin1Char(','), QLatin1Char('.'));
        cur.popularity = pop.toDouble();
        cur.outOfDate = m.captured(5).contains(QLatin1String("Out-of-date"), Qt::CaseInsensitive);
        cur.source = QStringLiteral("aur");
        cur.repo = QStringLiteral("aur");
        open = true;
    }
    flush();
    return res;
}

QVector<Package> Backend::parseFlatpakTable(const QByteArray &out, const QString &defaultRepo,
                                            bool hasDescriptionCol) {
    QVector<Package> res;
    for (const QString &line : QString::fromUtf8(out).split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty())
            continue;
        const QStringList c = line.split(QLatin1Char('\t'));
        if (c.size() < 2 || c.at(0) == QLatin1String("Application ID"))
            continue;
        Package p;
        p.id = c.at(0).trimmed();
        p.name = c.at(1).trimmed();
        if (p.name.isEmpty())
            p.name = p.id;
        if (hasDescriptionCol) {
            p.description = c.value(2).trimmed();
            p.version = c.value(3).trimmed();
            p.repo = c.value(4).trimmed().split(QLatin1Char(',')).value(0);
        }
        if (p.repo.isEmpty())
            p.repo = defaultRepo;
        p.source = QStringLiteral("flatpak");
        if (Validate::flatpakId(p.id))
            res.append(p);
    }
    return res;
}

// ───────────────────────── ranking / list building ─────────────────────────

void Backend::markInstalled(Package &p) const {
    if (p.source == QLatin1String("flatpak")) {
        const auto it = m_instFlatpak.constFind(p.id);
        p.installed = it != m_instFlatpak.constEnd();
        p.installedVersion = p.installed ? it->version : QString();
    } else {
        const auto it = m_instPacman.constFind(p.id);
        const bool foreign = m_foreign.contains(p.id);
        p.installed = it != m_instPacman.constEnd() && ((p.source == QLatin1String("aur")) == foreign);
        p.installedVersion = p.installed ? it->version : QString();
        if (p.installed) {
            p.reason = it->reason;
            p.orphan = it->orphan;
        }
    }
}

double Backend::scorePackage(const Package &p) const {
    const QString q = m_tokens.join(QLatin1Char(' '));
    QStringList names = {lc(p.id), lc(p.name)};
    if (p.source == QLatin1String("flatpak"))
        names << lc(lastSegment(p.id));
    double s = 5;
    bool all = true;
    for (const QString &n : names) {
        if (n == q) { s = std::max(s, 100.0); continue; }
        if (n.startsWith(q)) { s = std::max(s, 70.0); continue; }
        const int pos = n.indexOf(q);
        if (pos >= 0) {
            const bool boundary = pos == 0 || !n.at(pos - 1).isLetterOrNumber();
            s = std::max(s, boundary ? 55.0 : 40.0);
            continue;
        }
        bool every = true;
        for (const QString &t : m_tokens)
            if (!n.contains(t)) { every = false; break; }
        if (every && !m_tokens.isEmpty())
            s = std::max(s, 45.0);
    }
    if (s <= 5) {
        const QString d = lc(p.description);
        all = true;
        for (const QString &t : m_tokens)
            if (!d.contains(t)) { all = false; break; }
        if (all && !m_tokens.isEmpty())
            s = 12;
    }
    if (p.installed)
        s += 20;
    if (p.source == QLatin1String("aur")) {
        s += std::min(10.0, std::log10(p.votes + 1.0) * 3.0);
        if (p.outOfDate)
            s -= 5;
    } else if (p.source == QLatin1String("pacman")) {
        s += (p.repo == QLatin1String("core") || p.repo == QLatin1String("extra")) ? 5 : 3;
    } else {
        s += 2;
    }
    return s;
}

void Backend::rebuild() {
    const bool installedMode = m_mode == QLatin1String("installed");
    QVector<Package> all;

    if (installedMode) {
        all.reserve(m_instPacman.size() + m_instFlatpak.size());
        for (auto it = m_instPacman.cbegin(); it != m_instPacman.cend(); ++it)
            all.append(it.value());
        for (auto it = m_instFlatpak.cbegin(); it != m_instFlatpak.cend(); ++it)
            all.append(it.value());
        const QStringList toks = m_query.trimmed().toLower().split(QRegularExpression(QStringLiteral("\\s+")),
                                                                    Qt::SkipEmptyParts);
        if (!toks.isEmpty()) {
            QVector<Package> kept;
            for (Package &p : all) {
                const QString hay = lc(p.id + QLatin1Char(' ') + p.name + QLatin1Char(' ') + p.description);
                bool ok = true;
                for (const QString &t : toks)
                    if (!hay.contains(t)) { ok = false; break; }
                if (ok) {
                    // Name hits first, description hits after.
                    p.score = lc(p.id).contains(toks.first()) || lc(p.name).contains(toks.first()) ? 1 : 0;
                    kept.append(p);
                }
            }
            all = kept;
        }
    } else {
        for (auto it = m_bySource.cbegin(); it != m_bySource.cend(); ++it)
            all += it.value();
        for (Package &p : all) {
            markInstalled(p);
            p.score = scorePackage(p);
        }
    }

    // State / kind filter first so the source chips show meaningful counts.
    QVector<Package> stage;
    stage.reserve(all.size());
    for (Package &p : all)
        p.update = p.installed ? m_pendingUpdates.value(p.key()).toMap().value(QStringLiteral("to")).toString() : QString();
    for (const Package &p : std::as_const(all)) {
        if (installedMode) {
            if (m_kindFilter == QLatin1String("updates") && p.update.isEmpty())
                continue;
            if (m_kindFilter == QLatin1String("explicit") && (p.reason == QLatin1String("dep") || p.orphan))
                continue;
            if (m_kindFilter == QLatin1String("dep") && p.reason != QLatin1String("dep"))
                continue;
            if (m_kindFilter == QLatin1String("orphan") && !p.orphan)
                continue;
        } else {
            if (m_stateFilter == QLatin1String("installed") && !p.installed)
                continue;
            if (m_stateFilter == QLatin1String("available") && p.installed)
                continue;
        }
        stage.append(p);
    }

    QVariantMap counts = {{QStringLiteral("all"), int(stage.size())}, {QStringLiteral("pacman"), 0},
                          {QStringLiteral("aur"), 0}, {QStringLiteral("flatpak"), 0}};
    for (const Package &p : std::as_const(stage))
        counts[p.source] = counts.value(p.source).toInt() + 1;
    if (counts != m_counts) {
        m_counts = counts;
        emit countsChanged();
    }

    QVector<Package> out;
    out.reserve(stage.size());
    for (const Package &p : std::as_const(stage))
        if (m_sourceFilter == QLatin1String("all") || p.source == m_sourceFilter)
            out.append(p);

    if (installedMode) {
        std::sort(out.begin(), out.end(), [](const Package &a, const Package &b) {
            if (a.score != b.score)
                return a.score > b.score;
            return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
        });
    } else {
        auto srcRank = [](const QString &s) { return s == QLatin1String("pacman") ? 0 : s == QLatin1String("flatpak") ? 1 : 2; };
        std::stable_sort(out.begin(), out.end(), [&](const Package &a, const Package &b) {
            if (a.score != b.score)
                return a.score > b.score;
            if (srcRank(a.source) != srcRank(b.source))
                return srcRank(a.source) < srcRank(b.source);
            return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
        });
        if (out.size() > kMaxResults)
            out.resize(kMaxResults);
    }
    m_model.setItems(out);
}

// ───────────────────────── installed ─────────────────────────

void Backend::loadFlatpakRemotes() {
    run(QStringLiteral("flatpak"),
        {QStringLiteral("remotes"), QStringLiteral("--columns=name,options")},
        [this](int, const QByteArray &out, const QByteArray &) {
            bool anySystem = false, any = false;
            QString first, flathubSystem;
            for (const QString &line : QString::fromUtf8(out).split(QLatin1Char('\n'))) {
                const QStringList c = line.split(QLatin1Char('\t'));
                if (c.at(0).trimmed().isEmpty() || !Validate::remoteName(c.at(0).trimmed()))
                    continue;
                any = true;
                const QString name = c.at(0).trimmed();
                if (first.isEmpty())
                    first = name;
                if (c.value(1).contains(QLatin1String("system"))) {
                    anySystem = true;
                    if (name == QLatin1String("flathub"))
                        flathubSystem = name;
                }
            }
            if (any) {
                // Decision: install system-wide when a system remote exists (what flathub's
                // setup guide configures), per-user only when the machine has user remotes only.
                m_flatpakInstallation = anySystem ? QStringLiteral("system") : QStringLiteral("user");
                m_defaultRemote = !flathubSystem.isEmpty() ? flathubSystem : first;
            }
        });
}

void Backend::refreshInstalled() {
    loadInstalled();
}

void Backend::loadInstalled() {
    if (m_instPending > 0) {
        m_instDirty = true; // an operation finished mid-load: reload once this round ends
        return;
    }
    m_tmpPacman.clear();
    m_tmpFlatpak.clear();
    m_repoByName.clear();
    m_foreign.clear();
    m_orphans.clear();
    m_flatpakInstalledIn.clear();
    m_instPending = 0;
    auto done = [this] {
        if (--m_instPending == 0)
            finishInstalled();
    };

    if (m_hasPacman) {
        m_instPending += 4;
        run(QStringLiteral("pacman"), {QStringLiteral("-Qi")},
            [this, done](int, const QByteArray &out, const QByteArray &) {
                const QStringList blocks = QString::fromUtf8(out).split(QStringLiteral("\n\n"), Qt::SkipEmptyParts);
                for (const QString &b : blocks) {
                    Package p;
                    p.source = QStringLiteral("pacman");
                    p.installed = true;
                    QString curKey;
                    for (const QString &line : b.split(QLatin1Char('\n'))) {
                        if (line.isEmpty())
                            continue;
                        if (line.at(0).isSpace())
                            continue;
                        const int colon = line.indexOf(QLatin1Char(':'));
                        if (colon < 0)
                            continue;
                        const QString k = line.left(colon).trimmed();
                        const QString v = line.mid(colon + 1).trimmed();
                        if (k == QLatin1String("Name")) p.id = p.name = v;
                        else if (k == QLatin1String("Version")) p.version = p.installedVersion = v;
                        else if (k == QLatin1String("Description")) p.description = v;
                        else if (k == QLatin1String("Installed Size")) p.size = parseSize(v);
                        else if (k == QLatin1String("Install Reason"))
                            p.reason = v.startsWith(QLatin1String("Explicitly")) ? QStringLiteral("explicit")
                                                                                 : QStringLiteral("dep");
                    }
                    if (!p.id.isEmpty())
                        m_tmpPacman.append(p);
                }
                done();
            }, 40000, &m_instProcs);
        run(QStringLiteral("pacman"), {QStringLiteral("-Sl")},
            [this, done](int, const QByteArray &out, const QByteArray &) {
                for (const QString &line : QString::fromUtf8(out).split(QLatin1Char('\n'))) {
                    const QStringList c = line.split(QLatin1Char(' '));
                    if (c.size() >= 2)
                        m_repoByName.insert(c.at(1), c.at(0));
                }
                done();
            }, 40000, &m_instProcs);
        run(QStringLiteral("pacman"), {QStringLiteral("-Qmq")},
            [this, done](int, const QByteArray &out, const QByteArray &) {
                for (const QString &n : QString::fromUtf8(out).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                    m_foreign.insert(n.trimmed());
                done();
            }, 40000, &m_instProcs);
        run(QStringLiteral("pacman"), {QStringLiteral("-Qdtq")},
            [this, done](int, const QByteArray &out, const QByteArray &) {
                for (const QString &n : QString::fromUtf8(out).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                    m_orphans.insert(n.trimmed());
                done();
            }, 40000, &m_instProcs);
    }
    if (m_hasFlatpak) {
        ++m_instPending;
        run(QStringLiteral("flatpak"),
            {QStringLiteral("list"), QStringLiteral("--app"),
             QStringLiteral("--columns=application,name,version,origin,installation,description,size")},
            [this, done](int, const QByteArray &out, const QByteArray &) {
                for (const QString &line : QString::fromUtf8(out).split(QLatin1Char('\n'))) {
                    const QStringList c = line.split(QLatin1Char('\t'));
                    if (c.size() < 5 || c.at(0).trimmed().isEmpty())
                        continue;
                    Package p;
                    p.id = c.at(0).trimmed();
                    if (!Validate::flatpakId(p.id))
                        continue;
                    p.name = c.at(1).trimmed().isEmpty() ? p.id : c.at(1).trimmed();
                    p.version = p.installedVersion = c.at(2).trimmed();
                    p.repo = c.at(3).trimmed();
                    p.description = c.value(5).trimmed();
                    p.size = parseSize(c.value(6).trimmed());
                    p.source = QStringLiteral("flatpak");
                    p.installed = true;
                    p.reason = QStringLiteral("explicit");
                    m_flatpakInstalledIn.insert(p.id, c.at(4).trimmed());
                    m_tmpFlatpak.append(p);
                }
                done();
            }, 40000, &m_instProcs);
    }
    if (m_instPending == 0) {
        finishInstalled();
        return;
    }
    emit loadingInstalledChanged();
}

void Backend::finishInstalled() {
    m_instPacman.clear();
    m_instFlatpak.clear();
    for (Package p : std::as_const(m_tmpPacman)) {
        if (m_foreign.contains(p.id)) {
            p.source = QStringLiteral("aur");
            p.repo = QStringLiteral("aur");
        } else {
            p.repo = m_repoByName.value(p.id, QStringLiteral("local"));
        }
        p.orphan = m_orphans.contains(p.id);
        m_instPacman.insert(p.id, p);
    }
    for (const Package &p : std::as_const(m_tmpFlatpak))
        m_instFlatpak.insert(p.id, p);
    m_tmpPacman.clear();
    m_tmpFlatpak.clear();
    emit loadingInstalledChanged();
    matchShellUpdates();

    if (m_mode == QLatin1String("installed")) {
        rebuild();
    } else {
        // Keep the list (and the scroll position) and just flip the installed badges.
        m_model.refreshInstalled([this](Package &p) { markInstalled(p); });
    }
    emit installedChanged();
    if (!m_detail.isEmpty()) {
        const QString src = m_detail.value(QStringLiteral("source")).toString();
        const QString id = m_detail.value(QStringLiteral("id")).toString();
        const bool inst = isInstalled(src, id);
        if (inst != m_detail.value(QStringLiteral("installed")).toBool()) {
            // Re-read the detail so the sections (files, required by…) match the new state.
            loadDetail(src, id, m_detail.value(QStringLiteral("repo")).toString());
        }
    }
    emit settled();
    if (m_instDirty) {
        m_instDirty = false;
        loadInstalled();
    }
}

// ───────────────────────── detail ─────────────────────────

QVariantMap Backend::field(const QString &label, const QString &value, const QString &kind) {
    QVariantMap m;
    m[QStringLiteral("label")] = label;
    m[QStringLiteral("value")] = value;
    m[QStringLiteral("kind")] = kind.isEmpty() ? QStringLiteral("text") : kind;
    return m;
}

QList<QPair<QString, QStringList>> Backend::parseKeyBlocks(const QByteArray &out) {
    // First block only (pacman -Qi/-Si of one package). "Key   : value" + indented continuations.
    QList<QPair<QString, QStringList>> res;
    for (const QString &line : QString::fromUtf8(out).split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty()) {
            if (!res.isEmpty())
                break;
            continue;
        }
        if (line.at(0).isSpace()) {
            if (!res.isEmpty())
                res.last().second << line.trimmed();
            continue;
        }
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        res.append({line.left(colon).trimmed(), {line.mid(colon + 1).trimmed()}});
    }
    return res;
}

static QVariantList pacmanFields(const QList<QPair<QString, QStringList>> &kv, bool aurExtras) {
    struct Def { const char *key; const char *label; const char *kind; };
    static const Def defs[] = {
        {"URL", "Web", "link"},
        {"Licenses", "Licences", "text"},
        {"Groups", "Groups", "text"},
        {"Architecture", "Architecture", "text"},
        {"Download Size", "Download size", "text"},
        {"Installed Size", "Installed size", "text"},
        {"Install Reason", "Reason for installing", "text"},
        {"Install Date", "Installed on", "text"},
        {"Depends On", "Dependencies", "list"},
        {"Optional Deps", "Optional dependencies", "list"},
        {"Required By", "Required by", "list"},
        {"Optional For", "Optional for", "list"},
        {"Provides", "Provides", "list"},
        {"Conflicts With", "Conflicts with", "list"},
        {"Replaces", "Replaces", "list"},
        {"Packager", "Packaged by", "text"},
        {"Build Date", "Build date", "text"},
    };
    Q_UNUSED(aurExtras)
    QVariantList fields;
    for (const Def &d : defs) {
        for (const auto &pair : kv) {
            if (pair.first != QLatin1String(d.key))
                continue;
            QString value;
            QStringList items;
            const QString kind = QLatin1String(d.kind);
            if (kind == QLatin1String("list")) {
                if (QLatin1String(d.key) == QLatin1String("Optional Deps")) {
                    items = pair.second;
                } else {
                    for (const QString &l : pair.second)
                        items += l.split(QStringLiteral("  "), Qt::SkipEmptyParts);
                }
                for (QString &s : items)
                    s = s.trimmed();
                items.removeAll(QString());
                if (items.isEmpty() || (items.size() == 1 && items.first() == QLatin1String("None")))
                    break;
                value = items.join(QLatin1String(", "));
            } else {
                value = pair.second.join(QLatin1Char(' ')).trimmed();
                if (value.isEmpty() || value == QLatin1String("None") || value == QLatin1String("Unknown"))
                    break;
                if (QLatin1String(d.key) == QLatin1String("Install Reason"))
                    value = value.startsWith(QLatin1String("Explicitly")) ? QCoreApplication::translate("Backend", "Explicitly installed")
                                                                         : QCoreApplication::translate("Backend", "As a dependency");
            }
            QVariantMap f;
            f[QStringLiteral("label")] = QCoreApplication::translate("Backend", d.label);
            f[QStringLiteral("value")] = value;
            f[QStringLiteral("kind")] = kind;
            f[QStringLiteral("items")] = items;
            fields.append(f);
            break;
        }
    }
    return fields;
}

void Backend::setDetail(int gen, const QVariantMap &d, bool loading) {
    if (gen != m_detailGen)
        return;
    m_detail = d;
    m_detailLoading = loading;
    emit detailChanged();
}

void Backend::closeDetail() {
    ++m_detailGen;
    ++m_filesGen;
    for (auto &p : m_detailProcs)
        if (p) { p->disconnect(this); p->kill(); p->deleteLater(); }
    m_detailProcs.clear();
    if (m_detailReply) { m_detailReply->disconnect(this); m_detailReply->abort(); m_detailReply->deleteLater(); }
    m_detail.clear();
    m_detailLoading = false;
    m_detailFiles.clear();
    m_filesLoading = false;
    emit detailChanged();
    emit detailFilesChanged();
}

void Backend::loadDetail(const QString &source, const QString &id, const QString &repo) {
    if (!Validate::packageId(source, id))
        return;
    closeDetail();
    const int gen = m_detailGen;

    // Header from whichever list already knows the package (instant paint).
    QVariantMap base;
    base[QStringLiteral("source")] = source;
    base[QStringLiteral("id")] = id;
    base[QStringLiteral("name")] = id;
    base[QStringLiteral("repo")] = repo;
    base[QStringLiteral("installed")] = isInstalled(source, id);
    base[QStringLiteral("fields")] = QVariantList();
    Package known;
    bool have = false;
    for (const Package &p : m_model.items())
        if (p.source == source && p.id == id) { known = p; have = true; break; }
    if (!have) {
        if (source == QLatin1String("flatpak") && m_instFlatpak.contains(id)) { known = m_instFlatpak.value(id); have = true; }
        else if (source != QLatin1String("flatpak") && m_instPacman.contains(id)) { known = m_instPacman.value(id); have = true; }
    }
    if (have) {
        base[QStringLiteral("name")] = known.name;
        base[QStringLiteral("version")] = known.version;
        base[QStringLiteral("description")] = known.description;
        if (repo.isEmpty())
            base[QStringLiteral("repo")] = known.repo;
        base[QStringLiteral("votes")] = known.votes;
        base[QStringLiteral("popularity")] = known.popularity;
        base[QStringLiteral("outOfDate")] = known.outOfDate;
    }
    const bool installed = base.value(QStringLiteral("installed")).toBool();
    if (installed)
        base[QStringLiteral("installedVersion")] = source == QLatin1String("flatpak")
            ? m_instFlatpak.value(id).version : m_instPacman.value(id).version;
    setDetail(gen, base, true);

    if (source == QLatin1String("pacman"))
        loadPacmanDetail(gen, id, base.value(QStringLiteral("repo")).toString(), installed, base);
    else if (source == QLatin1String("aur"))
        loadAurDetail(gen, id, base);
    else
        loadFlatpakDetail(gen, id, base.value(QStringLiteral("repo")).toString(), installed, base);
}

void Backend::loadPacmanDetail(int gen, const QString &id, const QString &repo, bool installed,
                               QVariantMap base) {
    QStringList args;
    if (installed)
        args = {QStringLiteral("-Qi"), QStringLiteral("--"), id};
    else if (Validate::remoteName(repo) && !repo.isEmpty())
        args = {QStringLiteral("-Si"), QStringLiteral("--"), repo + QLatin1Char('/') + id};
    else
        args = {QStringLiteral("-Si"), QStringLiteral("--"), id};
    run(QStringLiteral("pacman"), args, [this, gen, base](int code, const QByteArray &out, const QByteArray &err) mutable {
        if (gen != m_detailGen)
            return;
        if (code != 0) {
            base[QStringLiteral("error")] = QString::fromUtf8(err).trimmed();
            setDetail(gen, base, false);
            return;
        }
        const auto kv = parseKeyBlocks(out);
        for (const auto &p : kv) {
            if (p.first == QLatin1String("Version")) base[QStringLiteral("version")] = p.second.join(QLatin1Char(' '));
            else if (p.first == QLatin1String("Description")) base[QStringLiteral("description")] = p.second.join(QLatin1Char(' '));
            else if (p.first == QLatin1String("Repository")) base[QStringLiteral("repo")] = p.second.join(QLatin1Char(' '));
        }
        base[QStringLiteral("fields")] = pacmanFields(kv, false);
        base[QStringLiteral("canFiles")] = base.value(QStringLiteral("installed")).toBool();
        setDetail(gen, base, false);
    }, 20000, &m_detailProcs);
}

void Backend::loadAurDetail(int gen, const QString &id, QVariantMap base) {
    QUrl url(QStringLiteral("https://aur.archlinux.org/rpc/v5/info"));
    QUrlQuery qq;
    qq.addQueryItem(QStringLiteral("arg[]"), id);
    url.setQuery(qq);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("things/0.1 (Arch package manager)"));
    QNetworkReply *reply = m_net.get(req);
    m_detailReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, gen, reply, id, base]() mutable {
        reply->deleteLater();
        if (gen != m_detailGen)
            return;
        const bool installed = base.value(QStringLiteral("installed")).toBool();
        QVariantList fields;
        auto add = [&](const QString &label, const QString &v, const QString &kind = QStringLiteral("text"),
                       const QStringList &items = {}) {
            if (v.isEmpty())
                return;
            QVariantMap f = field(label, v, kind);
            if (!items.isEmpty())
                f[QStringLiteral("items")] = items;
            fields.append(f);
        };
        auto strs = [](const QJsonValue &v) {
            QStringList l;
            for (const QJsonValue &x : v.toArray())
                l << x.toString();
            return l;
        };
        auto fmtDate = [](qint64 t) {
            return t > 0 ? QDateTime::fromSecsSinceEpoch(t).toString(QStringLiteral("dd/MM/yyyy")) : QString();
        };

        bool ok = reply->error() == QNetworkReply::NoError;
        QJsonObject o;
        if (ok) {
            const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("results")).toArray();
            ok = !arr.isEmpty();
            if (ok)
                o = arr.first().toObject();
        }
        if (!ok) {
            // Offline: fall back to yay's own metadata (-Si prints the same layout as pacman).
            if (m_hasYay) {
                run(QStringLiteral("yay"), {QStringLiteral("-Si"), QStringLiteral("--color"), QStringLiteral("never"),
                                            QStringLiteral("--"), QStringLiteral("aur/") + id},
                    [this, gen, base](int code, const QByteArray &out, const QByteArray &err) mutable {
                        if (gen != m_detailGen)
                            return;
                        if (code != 0) {
                            base[QStringLiteral("error")] = tr("Could not get the AUR information.") + QLatin1Char(' ')
                                + QString::fromUtf8(err).trimmed().left(200);
                            setDetail(gen, base, false);
                            return;
                        }
                        const auto kv = parseKeyBlocks(out);
                        for (const auto &p : kv) {
                            if (p.first == QLatin1String("Version")) base[QStringLiteral("version")] = p.second.join(QLatin1Char(' '));
                            else if (p.first == QLatin1String("Description")) base[QStringLiteral("description")] = p.second.join(QLatin1Char(' '));
                        }
                        base[QStringLiteral("fields")] = pacmanFields(kv, true);
                        setDetail(gen, base, false);
                    }, 25000, &m_detailProcs);
                return;
            }
            base[QStringLiteral("error")] = tr("Could not get the AUR information (offline).");
            setDetail(gen, base, false);
            return;
        }

        base[QStringLiteral("version")] = o.value(QStringLiteral("Version")).toString();
        base[QStringLiteral("description")] = o.value(QStringLiteral("Description")).toString();
        base[QStringLiteral("votes")] = o.value(QStringLiteral("NumVotes")).toInt();
        base[QStringLiteral("popularity")] = o.value(QStringLiteral("Popularity")).toDouble();
        const bool ood = !o.value(QStringLiteral("OutOfDate")).isNull() && o.value(QStringLiteral("OutOfDate")).toInt() > 0;
        base[QStringLiteral("outOfDate")] = ood;

        add(tr("Web"), o.value(QStringLiteral("URL")).toString(), QStringLiteral("link"));
        add(tr("AUR page"), QStringLiteral("https://aur.archlinux.org/packages/") + id, QStringLiteral("link"));
        add(tr("Licences"), strs(o.value(QStringLiteral("License"))).join(QLatin1String(", ")));
        add(tr("Maintainer"), o.value(QStringLiteral("Maintainer")).toString());
        add(tr("Votes"), QStringLiteral("%1 · popularidad %2").arg(o.value(QStringLiteral("NumVotes")).toInt())
                .arg(o.value(QStringLiteral("Popularity")).toDouble(), 0, 'f', 2));
        add(tr("First release"), fmtDate(o.value(QStringLiteral("FirstSubmitted")).toVariant().toLongLong()));
        add(tr("Last modified"), fmtDate(o.value(QStringLiteral("LastModified")).toVariant().toLongLong()));
        if (ood)
            add(tr("Status"), tr("Marked out of date on %1")
                    .arg(fmtDate(o.value(QStringLiteral("OutOfDate")).toVariant().toLongLong())));
        const QStringList dep = strs(o.value(QStringLiteral("Depends")));
        const QStringList mk = strs(o.value(QStringLiteral("MakeDepends")));
        const QStringList opt = strs(o.value(QStringLiteral("OptDepends")));
        if (!dep.isEmpty()) add(tr("Dependencies"), dep.join(QLatin1String(", ")), QStringLiteral("list"), dep);
        if (!mk.isEmpty()) add(tr("Build dependencies"), mk.join(QLatin1String(", ")), QStringLiteral("list"), mk);
        if (!opt.isEmpty()) add(tr("Optional dependencies"), opt.join(QLatin1String(", ")), QStringLiteral("list"), opt);
        const QString pbase = o.value(QStringLiteral("PackageBase")).toString();
        if (!pbase.isEmpty() && pbase != id)
            add(tr("Base package"), pbase);
        base[QStringLiteral("fields")] = fields;
        base[QStringLiteral("canFiles")] = installed;

        if (installed) {
            // Local facts (size, required by…) come from the installed package.
            run(QStringLiteral("pacman"), {QStringLiteral("-Qi"), QStringLiteral("--"), id},
                [this, gen, base](int code, const QByteArray &out, const QByteArray &) mutable {
                    if (gen != m_detailGen)
                        return;
                    if (code == 0) {
                        QVariantList f = base.value(QStringLiteral("fields")).toList();
                        const QVariantList local = pacmanFields(parseKeyBlocks(out), true);
                        for (const QVariant &v : local) {
                            const QString l = v.toMap().value(QStringLiteral("label")).toString();
                            if (l == QCoreApplication::translate("Backend", "Installed size") || l == QCoreApplication::translate("Backend", "Required by")
                                || l == QCoreApplication::translate("Backend", "Installed on")
                                || l == QCoreApplication::translate("Backend", "Reason for installing"))
                                f.append(v);
                        }
                        base[QStringLiteral("fields")] = f;
                    }
                    setDetail(gen, base, false);
                }, 20000, &m_detailProcs);
            return;
        }
        setDetail(gen, base, false);
    });
}

void Backend::loadFlatpakDetail(int gen, const QString &id, const QString &repo, bool installed,
                                QVariantMap base) {
    QStringList args;
    if (installed)
        args = {QStringLiteral("info"), id};
    else
        args = {QStringLiteral("remote-info"), Validate::remoteName(repo) && !repo.isEmpty() ? repo : m_defaultRemote, id};
    run(QStringLiteral("flatpak"), args, [this, gen, base, id](int code, const QByteArray &out, const QByteArray &err) mutable {
        if (gen != m_detailGen)
            return;
        if (code != 0) {
            base[QStringLiteral("error")] = QString::fromUtf8(err).trimmed();
            setDetail(gen, base, false);
            return;
        }
        QHash<QString, QString> kv;
        QString title;
        static const QRegularExpression kvre(QStringLiteral("^\\s*([A-Za-z][A-Za-z ]*):\\s*(.*)$"));
        for (const QString &line : QString::fromUtf8(out).split(QLatin1Char('\n'))) {
            const auto m = kvre.match(line);
            if (m.hasMatch())
                kv.insert(m.captured(1), m.captured(2).trimmed());
            else if (title.isEmpty() && !line.trimmed().isEmpty())
                title = line.trimmed();
        }
        // First line is "Name - Summary".
        const int dash = title.indexOf(QLatin1String(" - "));
        if (dash > 0) {
            base[QStringLiteral("name")] = title.left(dash);
            base[QStringLiteral("description")] = title.mid(dash + 3);
        }
        if (kv.contains(QStringLiteral("Version")))
            base[QStringLiteral("version")] = kv.value(QStringLiteral("Version"));
        QVariantList f;
        auto add = [&](const QString &key, const QString &label, const QString &kind = QStringLiteral("text")) {
            if (!kv.value(key).isEmpty())
                f.append(field(label, kv.value(key), kind));
        };
        f.append(field(tr("Application ID"), id));
        f.append(field(tr("Flathub page"), QStringLiteral("https://flathub.org/apps/") + id, QStringLiteral("link")));
        add(QStringLiteral("License"), tr("Licence"));
        add(QStringLiteral("Origin"), tr("Remote"));
        add(QStringLiteral("Branch"), tr("Branch"));
        add(QStringLiteral("Arch"), tr("Architecture"));
        add(QStringLiteral("Installation"), tr("Installation"));
        add(QStringLiteral("Download Size"), tr("Download size"));
        add(QStringLiteral("Installed Size"), tr("Installed size"));
        add(QStringLiteral("Runtime"), tr("Runtime"));
        add(QStringLiteral("Date"), tr("Date"));
        base[QStringLiteral("fields")] = f;
        setDetail(gen, base, false);
    }, 25000, &m_detailProcs);
}

void Backend::loadFiles(const QString &source, const QString &id) {
    if (source == QLatin1String("flatpak") || !Validate::pacmanName(id))
        return;
    const int gen = ++m_filesGen;
    m_detailFiles.clear();
    m_filesLoading = true;
    emit detailFilesChanged();
    run(QStringLiteral("pacman"), {QStringLiteral("-Ql"), QStringLiteral("--"), id},
        [this, gen, id](int, const QByteArray &out, const QByteArray &) {
            if (gen != m_filesGen)
                return;
            QStringList files;
            const QString prefix = id + QLatin1Char(' ');
            for (const QString &line : QString::fromUtf8(out).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                if (!line.startsWith(prefix))
                    continue;
                const QString path = line.mid(prefix.size());
                if (!path.endsWith(QLatin1Char('/')))
                    files << path;
                if (files.size() >= 3000)
                    break;
            }
            m_detailFiles = files;
            m_filesLoading = false;
            emit detailFilesChanged();
        }, 20000, &m_detailProcs);
}

// ───────────────────────── updates (from the shell) ─────────────────────────

void Backend::loadShellUpdates() {
    if (which(QStringLiteral("qs")).isEmpty())
        return;
    run(QStringLiteral("qs"), {QStringLiteral("-c"), QStringLiteral("sylphrena"), QStringLiteral("ipc"), QStringLiteral("call"),
                               QStringLiteral("updates"), QStringLiteral("list")},
        [this](int code, const QByteArray &out, const QByteArray &) {
            const QJsonObject o = QJsonDocument::fromJson(out).object();
            if (code != 0 || !o.contains(QLatin1String("entries")))
                return; // the shell is not running (or is older): leave things as they are
            QHash<QString, QPair<QString, QString>> next;
            for (const QJsonValue &v : o.value(QLatin1String("entries")).toArray()) {
                const QJsonObject e = v.toObject();
                const QString src = e.value(QLatin1String("source")).toString();
                const QString from = e.value(QLatin1String("from")).toString();
                const QString to = e.value(QLatin1String("to")).toString();
                if (src == QLatin1String("EXTENSION"))
                    continue; // the Sylphrena section marks those itself, from the hub
                if (src.startsWith(QLatin1String("FLATPAK"))) {
                    // `from` is the application id; Flatpak gives no old version here
                    if (Validate::flatpakId(from))
                        next.insert(QStringLiteral("flatpak:") + from, {QString(), to});
                    continue;
                }
                // Sylphrena's own: its package name, built locally (listed as foreign, so AUR, or pacman)
                const QString name = src == QLatin1String("SYLPHRENA") ? e.value(QLatin1String("pkg")).toString()
                                                                        : e.value(QLatin1String("name")).toString();
                if (!Validate::pacmanName(name))
                    continue;
                if (src != QLatin1String("AUR"))
                    next.insert(QStringLiteral("pacman:") + name, {from, to});
                if (src == QLatin1String("AUR") || src == QLatin1String("SYLPHRENA"))
                    next.insert(QStringLiteral("aur:") + name, {from, to});
            }
            m_shellUpdates = next;
            matchShellUpdates();
            rebuild();
        },
        5000);
}

void Backend::matchShellUpdates() {
    // Only while what is installed is still the version the shell saw as old: once updated (from here, a terminal
    // or the shell) the mark goes, without waiting for the shell's next check
    QVariantMap pending;
    for (auto it = m_shellUpdates.cbegin(); it != m_shellUpdates.cend(); ++it) {
        const QString key = it.key();
        const QString source = key.section(QLatin1Char(':'), 0, 0);
        const QString id = key.section(QLatin1Char(':'), 1);
        const QString from = it.value().first;
        QString have;
        if (source == QLatin1String("flatpak")) {
            if (!m_instFlatpak.contains(id))
                continue;
            have = m_instFlatpak.value(id).installedVersion;
        } else {
            const auto inst = m_instPacman.constFind(id);
            if (inst == m_instPacman.constEnd() || inst->source != source)
                continue;
            have = inst->installedVersion.isEmpty() ? inst->version : inst->installedVersion;
            // Sylphrena's say "0.5.5" for 0.5.5.r0.g…-1
            if (!from.isEmpty() && have != from && !have.startsWith(from + QLatin1Char('.')) && !have.startsWith(from + QLatin1Char('-')))
                continue;
        }
        pending.insert(key, QVariantMap{{QStringLiteral("from"), have}, {QStringLiteral("to"), it.value().second}});
    }
    if (pending != m_pendingUpdates) {
        m_pendingUpdates = pending;
        emit pendingUpdatesChanged();
    }
}

void Backend::openShellUpdates() {
    QProcess::startDetached(QStringLiteral("qs"), {QStringLiteral("-c"), QStringLiteral("sylphrena"), QStringLiteral("ipc"),
                                                   QStringLiteral("call"), QStringLiteral("nexus"), QStringLiteral("openPage"),
                                                   QStringLiteral("updates")});
}
