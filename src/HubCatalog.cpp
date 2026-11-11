#include "HubCatalog.h"
#include "Package.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocale>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QVersionNumber>
#include <algorithm>

namespace {

constexpr auto kIndexUrl = "https://raw.githubusercontent.com/iMacnucintosh/sylphrena-hub/main/index.json";
constexpr auto kReleases = "https://github.com/iMacnucintosh/sylphrena-hub/releases/download/";
constexpr qint64 kTtlSecs = 600;
constexpr int kMaxIndex = 1024 * 1024;

QString str(const QJsonObject &o, const char *key, int maxLen) {
    const QString s = o.value(QLatin1String(key)).toString().trimmed();
    return s.left(maxLen);
}

bool isHex64(const QString &s) {
    static const QRegularExpression re(QStringLiteral("^[0-9a-f]{64}$"));
    return re.match(s).hasMatch();
}

} // namespace

HubCatalog::HubCatalog(QObject *parent)
    : QObject(parent) {
    findTool();
    loadCache();
    scanInstalled();
    rebuild();
}

QString HubCatalog::cachePath() {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/hub-index.json");
}

QString HubCatalog::installedDir() {
    const QString data = qEnvironmentVariable("XDG_DATA_HOME",
                                              QDir::homePath() + QStringLiteral("/.local/share"));
    return data + QStringLiteral("/sylphrena/extensions");
}

bool HubCatalog::validId(const QString &id) {
    static const QRegularExpression re(QStringLiteral("^[a-z0-9-]{1,64}$"));
    return re.match(id).hasMatch();
}

bool HubCatalog::validZipUrl(const QString &url) {
    // Only the hub's releases. Tests may point SYLPHRENA_HUB_TEST_PREFIX at a local folder, the same
    // variable the shell's tool honours, so both ends agree.
    const QString test = qEnvironmentVariable("SYLPHRENA_HUB_TEST_PREFIX");
    return url.startsWith(QLatin1String(kReleases)) || (!test.isEmpty() && url.startsWith(test));
}

QVector<QVariantMap> HubCatalog::parseIndex(const QByteArray &json, QString *error) {
    QVector<QVariantMap> out;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QObject::tr("The hub index is not valid.");
        return out;
    }
    const QJsonObject root = doc.object();
    if (root.value(QLatin1String("schema")).toInt() != 1) {
        if (error)
            *error = QObject::tr("The hub index is from a version this version of Things does not understand.");
        return out;
    }
    for (const QJsonValue &v : root.value(QLatin1String("items")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString kind = str(o, "kind", 16);
        const QString id = str(o, "id", 64);
        const QString version = str(o, "version", 32);
        const QString zip = str(o, "zip", 512);
        const QString sha = str(o, "sha256", 64).toLower();
        // Anything that fails one check is skipped, not shown half-way
        if ((kind != QLatin1String("extension") && kind != QLatin1String("widget")) || !validId(id)
            || QVersionNumber::fromString(version).isNull() || !validZipUrl(zip) || !isHex64(sha))
            continue;
        static const QRegularExpression iconRe(QStringLiteral("^[a-z0-9_]{1,48}$"));
        static const QRegularExpression reqRe(QStringLiteral("^[0-9.<>=\\s]{0,48}$"));
        QString icon = str(o, "icon", 48);
        if (!iconRe.match(icon).hasMatch())
            icon = QStringLiteral("extension");
        QString requires_ = str(o, "requires", 48);
        if (!reqRe.match(requires_).hasMatch())
            requires_.clear();
        QVariantMap e;
        e[QStringLiteral("id")] = id;
        e[QStringLiteral("kind")] = kind;
        e[QStringLiteral("name")] = str(o, "name", 80).isEmpty() ? id : str(o, "name", 80);
        e[QStringLiteral("description")] = str(o, "description", 400);
        e[QStringLiteral("icon")] = icon;
        e[QStringLiteral("version")] = version;
        e[QStringLiteral("requires")] = requires_;
        e[QStringLiteral("system")] = o.value(QLatin1String("system")).toBool();
        // Programs it needs / can use: short texts shown before installing (never blocks anything)
        for (const char *key : {"needs", "optional"}) {
            QStringList list;
            const QJsonArray arr = o.value(QLatin1String(key)).toArray();
            for (const QJsonValue &v : arr) {
                const QString t = v.toString().simplified().left(160);
                if (!t.isEmpty() && list.size() < 5)
                    list << t;
            }
            e[QString::fromLatin1(key)] = list;
        }
        // Texts in the user's language, when the hub carries them (index "i18n": {lang: {name, description, needs, optional}})
        const QJsonObject tr = o.value(QLatin1String("i18n")).toObject();
        const QString lang = QLocale().name().section(QLatin1Char('_'), 0, 0);
        const QJsonObject t = tr.value(lang).toObject();
        if (!t.isEmpty()) {
            if (!t.value(QLatin1String("name")).toString().isEmpty())
                e[QStringLiteral("name")] = t.value(QLatin1String("name")).toString().left(80);
            if (!t.value(QLatin1String("description")).toString().isEmpty())
                e[QStringLiteral("description")] = t.value(QLatin1String("description")).toString().left(400);
            for (const char *key : {"needs", "optional"}) {
                QStringList list;
                for (const QJsonValue &v : t.value(QLatin1String(key)).toArray()) {
                    const QString x = v.toString().simplified().left(160);
                    if (!x.isEmpty() && list.size() < 5)
                        list << x;
                }
                if (!list.isEmpty())
                    e[QString::fromLatin1(key)] = list;
            }
        }
        const QString store = str(o, "store", 512);
        e[QStringLiteral("store")] = validStoreUrl(store) ? store : QString();
        QStringList data;
        for (const QJsonValue &v : o.value(QLatin1String("data")).toArray()) {
            const QString d = v.toString().simplified().left(160);
            if (!d.isEmpty() && data.size() < 8)
                data << d;
        }
        e[QStringLiteral("data")] = data;
        e[QStringLiteral("zip")] = zip;
        e[QStringLiteral("sha256")] = sha;
        e[QStringLiteral("size")] = double(qBound<qint64>(0, qint64(o.value(QLatin1String("size")).toDouble()), qint64(64) * 1024 * 1024));
        out.append(e);
    }
    return out;
}

QVariantList HubCatalog::merge(const QVector<QVariantMap> &catalog, const QHash<QString, QVariantMap> &installed) {
    QVariantList rows;
    QSet<QString> seen;
    for (const QVariantMap &c : catalog) {
        const QString id = c.value(QStringLiteral("id")).toString();
        seen.insert(id);
        QVariantMap r = c;
        r.remove(QStringLiteral("zip"));
        r.remove(QStringLiteral("sha256"));
        r[QStringLiteral("local")] = false;
        r[QStringLiteral("installedVersion")] = QString();
        r[QStringLiteral("state")] = QStringLiteral("available");
        const auto it = installed.constFind(id);
        if (it != installed.constEnd()) {
            const QString have = it->value(QStringLiteral("version")).toString();
            r[QStringLiteral("installedVersion")] = have;
            if (!it->value(QStringLiteral("fromHub")).toBool()) {
                // Put there by hand or from a zip: Things shows it and never touches it on its own
                r[QStringLiteral("state")] = QStringLiteral("local");
                r[QStringLiteral("local")] = true;
            } else if (QVersionNumber::compare(QVersionNumber::fromString(have),
                                                QVersionNumber::fromString(c.value(QStringLiteral("version")).toString())) < 0) {
                r[QStringLiteral("state")] = QStringLiteral("update");
            } else {
                r[QStringLiteral("state")] = QStringLiteral("installed");
            }
        }
        rows.append(r);
    }
    // Installed ones the hub does not list (local, or retired from the catalog)
    QStringList extra = installed.keys();
    std::sort(extra.begin(), extra.end());
    for (const QString &id : extra) {
        if (seen.contains(id))
            continue;
        const QVariantMap &i = installed[id];
        QVariantMap r;
        r[QStringLiteral("id")] = id;
        r[QStringLiteral("kind")] = i.value(QStringLiteral("kind"), QStringLiteral("extension"));
        r[QStringLiteral("name")] = i.value(QStringLiteral("name"), id);
        r[QStringLiteral("description")] = i.value(QStringLiteral("description"));
        r[QStringLiteral("icon")] = i.value(QStringLiteral("icon"), QStringLiteral("extension"));
        r[QStringLiteral("version")] = i.value(QStringLiteral("version"));
        r[QStringLiteral("installedVersion")] = i.value(QStringLiteral("version"));
        r[QStringLiteral("requires")] = QString();
        r[QStringLiteral("system")] = i.value(QStringLiteral("system"));
        r[QStringLiteral("size")] = 0.0;
        const bool local = !i.value(QStringLiteral("fromHub")).toBool();
        r[QStringLiteral("local")] = local;
        r[QStringLiteral("state")] = local ? QStringLiteral("local") : QStringLiteral("installed");
        rows.append(r);
    }
    return rows;
}

QVariantMap HubCatalog::entry(const QString &id) const {
    for (const QVariantMap &c : m_catalog)
        if (c.value(QStringLiteral("id")).toString() == id)
            return c;
    return {};
}

QVariantMap HubCatalog::installedInfo(const QString &id) const {
    return m_installed.value(id);
}

QNetworkRequest HubCatalog::request(const QUrl &url) const {
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Things"));
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(15000);
    return r;
}

void HubCatalog::loadCache() {
    // THINGS_HUB_INDEX (a path or file:// URL) replaces the network and the cache: for tests
    const QString override_ = qEnvironmentVariable("THINGS_HUB_INDEX");
    if (!override_.isEmpty()) {
        QFile f(QUrl(override_).isLocalFile() ? QUrl(override_).toLocalFile() : override_);
        if (f.open(QIODevice::ReadOnly))
            ingest(f.read(kMaxIndex), false);
        return;
    }
    QFile f(cachePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    // The cache only paints the first frame: it is never "fresh", so the first refresh of a run always asks
    // the hub (a new release would otherwise stay hidden for up to the TTL)
    ingest(f.read(kMaxIndex), false);
}

void HubCatalog::saveCache(const QByteArray &json) {
    QDir().mkpath(QFileInfo(cachePath()).absolutePath());
    QSaveFile f(cachePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(json);
        f.commit();
    }
}

void HubCatalog::ingest(const QByteArray &json, bool fromNet) {
    QString err;
    const QVector<QVariantMap> parsed = parseIndex(json, &err);
    if (!err.isEmpty()) {
        // A bad document from the network must not wipe a good cache
        if (fromNet && m_catalog.isEmpty())
            m_notice = err;
        return;
    }
    m_catalog = parsed;
    if (fromNet) {
        m_fetchedAt = QDateTime::currentDateTime();
        m_notice.clear();
        saveCache(json);
    }
    rebuild();
}

void HubCatalog::scanInstalled() {
    m_installed.clear();
    const QDir dir(installedDir());
    for (const QFileInfo &fi : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QFile mf(fi.absoluteFilePath() + QStringLiteral("/manifest.json"));
        if (!mf.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject m = QJsonDocument::fromJson(mf.read(kMaxIndex)).object();
        const QString id = m.value(QLatin1String("id")).toString();
        if (!validId(id) || id != fi.fileName())
            continue;
        QVariantMap i;
        i[QStringLiteral("id")] = id;
        i[QStringLiteral("name")] = str(m, "name", 80);
        i[QStringLiteral("description")] = str(m, "description", 400);
        i[QStringLiteral("icon")] = str(m, "icon", 48);
        i[QStringLiteral("kind")] = m.value(QLatin1String("type")).toString() == QLatin1String("widget")
            ? QStringLiteral("widget") : QStringLiteral("extension");
        i[QStringLiteral("version")] = str(m, "version", 32);
        i[QStringLiteral("system")] = !m.value(QLatin1String("system")).toObject().value(QLatin1String("install")).toString().isEmpty();
        QFile sf(fi.absoluteFilePath() + QStringLiteral("/.source.json"));
        bool fromHub = false;
        if (sf.open(QIODevice::ReadOnly))
            fromHub = QJsonDocument::fromJson(sf.read(4096)).object().value(QLatin1String("source")).toString() == QLatin1String("hub");
        i[QStringLiteral("fromHub")] = fromHub;
        m_installed.insert(id, i);
    }
}

void HubCatalog::findTool() {
    const QString before = m_tool;
    const QString home = QDir::homePath();
    QStringList candidates;
    const QString env = qEnvironmentVariable("SYLPHRENA_EXT_TOOL");
    if (!env.isEmpty())
        candidates << env;
    candidates << home + QStringLiteral("/.config/quickshell/sylphrena/scripts/ext.py")
               << QStringLiteral("/etc/xdg/quickshell/sylphrena/scripts/ext.py")
               << QStringLiteral("/usr/share/sylphrena/scripts/ext.py");
    for (const QString &c : candidates) {
        if (QFileInfo(c).isFile()) {
            m_tool = c;
            break;
        }
    }
    // The running shell knows where it is installed: ask it (the answer wins over the guesses)
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p] {
        const QString dir = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
        p->deleteLater();
        const QString path = dir + QStringLiteral("/ext.py");
        if (!dir.isEmpty() && !dir.contains(QLatin1String("not found")) && QFileInfo(path).isFile() && path != m_tool) {
            m_tool = path;
            emit toolChanged();
        }
    });
    connect(p, &QProcess::errorOccurred, p, &QObject::deleteLater);
    p->start(QStringLiteral("qs"), {QStringLiteral("-c"), QStringLiteral("sylphrena"), QStringLiteral("ipc"),
                                    QStringLiteral("call"), QStringLiteral("extensions"), QStringLiteral("tools")});
    QTimer::singleShot(5000, p, [p] { if (p->state() != QProcess::NotRunning) p->kill(); });
    if (m_tool != before)
        emit toolChanged();
}

void HubCatalog::rebuild() {
    m_items = merge(m_catalog, m_installed);
    m_updates = 0;
    for (const QVariant &v : std::as_const(m_items))
        if (v.toMap().value(QStringLiteral("state")).toString() == QLatin1String("update"))
            ++m_updates;
    emit changed();
}

void HubCatalog::rescan() {
    scanInstalled();
    rebuild();
}

void HubCatalog::pickZip() {
    if (m_picking)
        return;
    const QString home = QDir::homePath();
    QString seeker = home + QStringLiteral("/.local/bin/seeker");
    if (!QFileInfo(seeker).isExecutable())
        seeker = QStandardPaths::findExecutable(QStringLiteral("seeker"));
    if (seeker.isEmpty()) {
        m_notice = tr("Choosing the zip needs Seeker, the file manager of Sylphrena.");
        emit changed();
        return;
    }
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR", QStringLiteral("/tmp"));
    const QString result = runtime + QStringLiteral("/things-pick-%1.json").arg(QDateTime::currentMSecsSinceEpoch());
    m_picking = true;
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p, result] {
        p->deleteLater();
        m_picking = false;
        QFile f(result);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(f.read(65536)).object();
            f.close();
            const QJsonArray uris = o.value(QLatin1String("uris")).toArray();
            if (o.value(QLatin1String("response")).toInt(1) == 0 && !uris.isEmpty()) {
                QString path = uris.first().toString();
                if (path.startsWith(QLatin1String("file://")))
                    path = QUrl::fromPercentEncoding(path.mid(7).toUtf8());
                if (Validate::zipPath(path))
                    emit zipPicked(path);
            }
        }
        QFile::remove(result);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p] {
        p->deleteLater();
        m_picking = false;
    });
    p->start(seeker, {QStringLiteral("--pick"), QStringLiteral("--pick-mode=file"),
                      QStringLiteral("--pick-result=") + result,
                      QStringLiteral("--pick-title=") + tr("Choose an extension's zip"),
                      QStringLiteral("--pick-accept=") + tr("Choose"),
                      QStringLiteral("--pick-extensions=zip"),
                      QStringLiteral("--pick-current=") + home});
}

static const QString kStoreBase = QStringLiteral("https://raw.githubusercontent.com/iMacnucintosh/sylphrena-hub/main/");

bool HubCatalog::validStoreUrl(const QString &url) {
    const QString test = qEnvironmentVariable("SYLPHRENA_HUB_TEST_PREFIX");
    static const QRegularExpression re(QStringLiteral("^store/[a-z0-9-]{1,64}\\.json$"));
    if (!test.isEmpty() && url.startsWith(test))
        return true;
    return url.startsWith(kStoreBase) && re.match(url.mid(kStoreBase.size())).hasMatch();
}

bool HubCatalog::validShotUrl(const QString &url) {
    const QString test = qEnvironmentVariable("SYLPHRENA_HUB_TEST_PREFIX");
    static const QRegularExpression re(QStringLiteral("^extensions/[a-z0-9-]{1,64}/store/screenshots/[A-Za-z0-9._-]{1,100}$"));
    if (!test.isEmpty() && url.startsWith(test))
        return true;
    return url.startsWith(kStoreBase) && re.match(url.mid(kStoreBase.size())).hasMatch();
}

QVariantMap HubCatalog::parseDetails(const QByteArray &json, const QString &id) {
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    QVariantMap d;
    if (o.value(QLatin1String("id")).toString() != id)
        return d;
    // Texts in the user's language when the document carries them (i18n: {lang: {english text: translation}})
    const QString lang = QLocale().name().section(QLatin1Char('_'), 0, 0);
    const QJsonObject tr = o.value(QLatin1String("i18n")).toObject().value(lang).toObject();
    auto text = [&](const QJsonValue &v, int limit) {
        const QString en = v.toString().trimmed();
        const QString t = tr.value(en).toString();
        return (t.isEmpty() ? en : t).left(limit);
    };
    d[QStringLiteral("id")] = id;
    d[QStringLiteral("about")] = text(o.value(QLatin1String("about")), 2000);
    QStringList highlights;
    for (const QJsonValue &v : o.value(QLatin1String("highlights")).toArray())
        if (highlights.size() < 6 && !v.toString().isEmpty())
            highlights << text(v, 160);
    d[QStringLiteral("highlights")] = highlights;
    d[QStringLiteral("author")] = o.value(QLatin1String("author")).toString().left(80);
    d[QStringLiteral("license")] = o.value(QLatin1String("license")).toString().left(40);
    QVariantMap links;
    const QJsonObject lo = o.value(QLatin1String("links")).toObject();
    for (const QString &k : {QStringLiteral("source"), QStringLiteral("issues"), QStringLiteral("web")}) {
        const QString u = lo.value(k).toString();
        if (u.startsWith(QLatin1String("https://")) && u.size() < 300)
            links[k] = u;
    }
    d[QStringLiteral("links")] = links;
    QVariantList log;
    for (const QJsonValue &v : o.value(QLatin1String("changelog")).toArray()) {
        if (log.size() >= 12)
            break;
        const QJsonObject e = v.toObject();
        QStringList changes;
        for (const QJsonValue &c : e.value(QLatin1String("changes")).toArray())
            if (changes.size() < 12 && !c.toString().isEmpty())
                changes << text(c, 240);
        QVariantMap row;
        row[QStringLiteral("version")] = e.value(QLatin1String("version")).toString().left(32);
        row[QStringLiteral("date")] = e.value(QLatin1String("date")).toString().left(10);
        row[QStringLiteral("changes")] = changes;
        log.append(row);
    }
    d[QStringLiteral("changelog")] = log;
    QVariantList shots;
    for (const QJsonValue &v : o.value(QLatin1String("screenshots")).toArray()) {
        const QJsonObject e = v.toObject();
        const QString url = e.value(QLatin1String("url")).toString();
        if (shots.size() >= 6 || !validShotUrl(url))
            continue;
        QVariantMap s;
        s[QStringLiteral("url")] = url;
        s[QStringLiteral("full")] = url;
        s[QStringLiteral("caption")] = text(e.value(QLatin1String("caption")), 160);
        shots.append(s);
    }
    d[QStringLiteral("screenshots")] = shots;
    d[QStringLiteral("ok")] = true;
    return d;
}

void HubCatalog::clearDetails() {
    m_details.clear();
    m_detailsId.clear();
    m_detailsPending = false;
    emit detailsChanged();
}

void HubCatalog::loadDetails(const QString &id) {
    if (m_detailsId == id && !m_details.isEmpty())
        return;
    m_details.clear();
    m_detailsId = id;
    const QString url = entry(id).value(QStringLiteral("store")).toString();
    if (url.isEmpty() || !validStoreUrl(url)) {
        m_detailsPending = false;
        emit detailsChanged();
        return;
    }
    m_detailsPending = true;
    emit detailsChanged();
    QNetworkReply *r = m_net.get(request(QUrl(url)));
    connect(r, &QNetworkReply::finished, this, [this, r, id] {
        r->deleteLater();
        if (id != m_detailsId)
            return;
        m_detailsPending = false;
        if (r->error() == QNetworkReply::NoError)
            m_details = parseDetails(r->read(kMaxIndex + 1).left(kMaxIndex), id);
        emit detailsChanged();
    });
}

void HubCatalog::finishOne() {
    if (m_pending > 0)
        --m_pending;
    emit loadingChanged();
}

void HubCatalog::refresh(bool force) {
    if (!qEnvironmentVariable("THINGS_HUB_INDEX").isEmpty())
        return;
    if (m_pending > 0)
        return;
    if (!force && m_fetchedAt.isValid() && m_fetchedAt.secsTo(QDateTime::currentDateTime()) < kTtlSecs && !m_catalog.isEmpty())
        return;
    ++m_pending;
    emit loadingChanged();
    QNetworkReply *r = m_net.get(request(QUrl(QLatin1String(kIndexUrl))));
    m_reply = r;
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (r->error() == QNetworkReply::NoError) {
            ingest(r->read(kMaxIndex + 1).left(kMaxIndex), true);
        } else if (m_catalog.isEmpty()) {
            m_notice = tr("Could not read the Sylphrena catalogue. Check your connection.");
            emit changed();
        } else {
            m_notice = tr("No connection: the last saved catalogue is shown.");
            emit changed();
        }
        finishOne();
    });
}
