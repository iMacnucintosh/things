#include "Discover.h"

#include "Package.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkReply>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

namespace {

constexpr qint64 kFreshSecs = 3600;      // do not hit the network again within an hour
constexpr int kPerRow = 14;
constexpr int kFetchPerPage = 24;        // we drop runtimes/addons client-side, so ask for extra
const QString kImageHost = QStringLiteral("https://dl.flathub.org/");

bool imageUrl(const QString &u) {
    return u.startsWith(kImageHost) && QUrl(u).isValid();
}

bool validItem(const QVariantMap &m) {
    return Validate::flatpakId(m.value(QStringLiteral("id")).toString())
        && !m.value(QStringLiteral("name")).toString().isEmpty()
        && imageUrl(m.value(QStringLiteral("icon")).toString());
}

const QList<QPair<QString, QString>> &collections() {
    // section id → Flathub collection path
    static const QList<QPair<QString, QString>> c = {
        {QStringLiteral("trending"), QStringLiteral("trending")},
        {QStringLiteral("popular"), QStringLiteral("popular")},
        {QStringLiteral("new"), QStringLiteral("recently-added")},
        {QStringLiteral("updated"), QStringLiteral("recently-updated")},
    };
    return c;
}

} // namespace

Discover::Discover(QObject *parent) : QObject(parent) {
    loadCache();
    // First refresh after the window is up: cached data is already on screen by then.
    QTimer::singleShot(0, this, [this] { refresh(false); });
}

QString Discover::cachePath() {
    return QDir::home().filePath(QStringLiteral(".cache/things/discover.json"));
}

QNetworkRequest Discover::request(const QUrl &url) const {
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::UserAgentHeader,
                QStringLiteral("Things/0.1 (+https://github.com/iMacnucintosh/things)"));
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(15000);
    return r;
}

void Discover::loadCache() {
    QFile f(cachePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const qint64 ts = root.value(QLatin1String("ts")).toInteger();
    const QJsonArray secs = root.value(QLatin1String("sections")).toArray();
    if (ts <= 0 || secs.isEmpty())
        return;

    QVariantList sections;
    for (const QJsonValue &sv : secs) {
        const QJsonObject so = sv.toObject();
        QVariantList items;
        for (const QJsonValue &iv : so.value(QLatin1String("items")).toArray()) {
            const QVariantMap m = iv.toObject().toVariantMap();
            if (validItem(m)) // never trust the cache blindly
                items.append(m);
        }
        const QString id = so.value(QLatin1String("id")).toString();
        if (!id.isEmpty() && !items.isEmpty())
            sections.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("items"), items}});
    }
    if (sections.isEmpty())
        return;
    m_sections = sections;
    m_fetchedAt = QDateTime::fromSecsSinceEpoch(ts);

    QVariantMap hero = root.value(QLatin1String("hero")).toObject().toVariantMap();
    if (validItem(hero)) {
        if (!imageUrl(hero.value(QStringLiteral("screenshot")).toString()))
            hero.remove(QStringLiteral("screenshot"));
        m_hero = hero;
    }
}

void Discover::saveCache() const {
    if (m_sections.isEmpty())
        return;
    QDir().mkpath(QFileInfo(cachePath()).absolutePath());
    QSaveFile f(cachePath());
    if (!f.open(QIODevice::WriteOnly))
        return;
    QJsonObject root;
    root.insert(QStringLiteral("ts"), m_fetchedAt.toSecsSinceEpoch());
    root.insert(QStringLiteral("sections"), QJsonValue::fromVariant(m_sections));
    root.insert(QStringLiteral("hero"), QJsonValue::fromVariant(m_hero));
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
}

QVariantMap Discover::parseHit(const QJsonObject &o) const {
    if (o.value(QLatin1String("type")).toString() != QLatin1String("desktop-application"))
        return {};
    QVariantMap m;
    m[QStringLiteral("id")] = o.value(QLatin1String("app_id")).toString();
    m[QStringLiteral("name")] = o.value(QLatin1String("name")).toString().trimmed();
    m[QStringLiteral("summary")] = o.value(QLatin1String("summary")).toString().trimmed();
    m[QStringLiteral("icon")] = o.value(QLatin1String("icon")).toString();
    m[QStringLiteral("developer")] = o.value(QLatin1String("developer_name")).toString().trimmed();
    m[QStringLiteral("installs")] = o.value(QLatin1String("installs_last_month")).toDouble();
    m[QStringLiteral("added")] = o.value(QLatin1String("added_at")).toInteger();
    m[QStringLiteral("updated")] = o.value(QLatin1String("updated_at")).toInteger();
    const QJsonValue cat = o.value(QLatin1String("main_categories"));
    m[QStringLiteral("category")] = cat.isArray() ? cat.toArray().first().toString() : cat.toString();
    if (!validItem(m) || m.value(QStringLiteral("summary")).toString().isEmpty())
        return {};
    return m;
}

void Discover::refresh(bool force) {
    if (m_pending > 0)
        return;
    if (!force && hasData() && m_fetchedAt.isValid()
        && m_fetchedAt.secsTo(QDateTime::currentDateTime()) < kFreshSecs) {
        if (!m_hero.contains(QStringLiteral("screenshot")))
            fetchHeroScreenshot();
        return;
    }
    m_fresh.clear();
    m_failed = 0;
    m_pending = collections().size();
    emit loadingChanged();
    for (const auto &c : collections())
        fetchCollection({c.first, c.second});
}

void Discover::fetchCollection(const Collection &c) {
    // Flathub translates names/summaries when the app ships translations (falls back to English).
    const QString lang = QLocale::system().name().section(QLatin1Char('_'), 0, 0);
    const QUrl url(QStringLiteral("https://flathub.org/api/v2/collection/%1?page=1&per_page=%2&locale=%3")
                       .arg(c.path)
                       .arg(kFetchPerPage)
                       .arg(lang.isEmpty() || lang == QLatin1String("C") ? QStringLiteral("en") : lang));
    QNetworkReply *reply = m_nam.get(request(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, id = c.id] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            collectionDone(id, {}, false);
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QVariantList items;
        for (const QJsonValue &v : root.value(QLatin1String("hits")).toArray()) {
            const QVariantMap m = parseHit(v.toObject());
            if (!m.isEmpty())
                items.append(m);
            if (items.size() >= kPerRow)
                break;
        }
        collectionDone(id, items, !items.isEmpty());
    });
}

void Discover::collectionDone(const QString &id, const QVariantList &items, bool ok) {
    if (ok)
        m_fresh.insert(id, items);
    else
        ++m_failed;
    if (--m_pending > 0)
        return;

    if (m_failed == collections().size()) {
        m_notice = hasData() ? tr("No connection to Flathub: the last saved data is shown.")
                             : tr("Could not reach Flathub.");
        emit loadingChanged();
        emit changed();
        return;
    }

    // A row that failed this time keeps its previous (cached) content.
    QVariantMap previous;
    for (const QVariant &s : std::as_const(m_sections))
        previous.insert(s.toMap().value(QStringLiteral("id")).toString(),
                        s.toMap().value(QStringLiteral("items")));
    QVariantList sections;
    for (const auto &c : collections()) {
        const QVariant items = m_fresh.contains(c.first) ? m_fresh.value(c.first) : previous.value(c.first);
        if (!items.toList().isEmpty())
            sections.append(QVariantMap{{QStringLiteral("id"), c.first}, {QStringLiteral("items"), items}});
    }
    m_sections = sections;
    m_fetchedAt = QDateTime::currentDateTime();
    m_notice.clear();
    pickHero();
    saveCache();
    emit loadingChanged();
    emit changed();
    fetchHeroScreenshot();
}

void Discover::pickHero() {
    // The most installed apps make the best front page (well known, polished screenshots).
    // Trending is full of tiny new apps, so it is only a fallback.
    auto itemsOf = [this](const QString &id) {
        for (const QVariant &s : std::as_const(m_sections)) {
            const QVariantMap sm = s.toMap();
            if (sm.value(QStringLiteral("id")).toString() == id)
                return sm.value(QStringLiteral("items")).toList();
        }
        return QVariantList();
    };
    QVariantList pool = itemsOf(QStringLiteral("popular")).mid(0, 8);
    if (pool.isEmpty())
        pool = itemsOf(QStringLiteral("trending"));
    if (pool.isEmpty() && !m_sections.isEmpty())
        pool = m_sections.first().toMap().value(QStringLiteral("items")).toList();
    if (pool.isEmpty()) {
        m_hero.clear();
        return;
    }
    // A different pick each day, so the front page does not look frozen.
    const int idx = QDate::currentDate().dayOfYear() % int(pool.size());
    QVariantMap hero = pool.at(idx).toMap();
    if (m_hero.value(QStringLiteral("id")) == hero.value(QStringLiteral("id"))
        && m_hero.contains(QStringLiteral("screenshot")))
        hero.insert(QStringLiteral("screenshot"), m_hero.value(QStringLiteral("screenshot")));
    m_hero = hero;
}

void Discover::fetchHeroScreenshot() {
    const QString id = m_hero.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || !Validate::flatpakId(id) || m_hero.contains(QStringLiteral("screenshot")))
        return;
    QNetworkReply *reply = m_nam.get(request(QUrl(QStringLiteral("https://flathub.org/api/v2/appstream/") + id)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, id] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError || m_hero.value(QStringLiteral("id")).toString() != id)
            return;
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QString best;
        int bestW = 0;
        for (const QJsonValue &sv : root.value(QLatin1String("screenshots")).toArray()) {
            for (const QJsonValue &zv : sv.toObject().value(QLatin1String("sizes")).toArray()) {
                const QJsonObject z = zv.toObject();
                const QString src = z.value(QLatin1String("src")).toString();
                const int w = z.value(QLatin1String("width")).toString().toInt();
                // Biggest one up to ~1100 px wide: sharp enough for the banner, light to download.
                if (imageUrl(src) && w <= 1100 && w > bestW) {
                    best = src;
                    bestW = w;
                }
            }
            if (!best.isEmpty())
                break; // first screenshot that has a usable size
        }
        if (best.isEmpty())
            return;
        m_hero.insert(QStringLiteral("screenshot"), best);
        saveCache();
        emit changed();
    });
}

QString Discover::formatInstalls(double n) const {
    if (n >= 1e6)
        return QStringLiteral("%1 M").arg(QString::number(n / 1e6, 'f', 1).replace(QLatin1Char('.'), QLatin1Char(',')));
    if (n >= 1000)
        return tr("%1k").arg(qRound(n / 1000.0));
    return QString::number(qRound(n));
}
