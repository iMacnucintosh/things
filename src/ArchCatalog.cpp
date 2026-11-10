#include "ArchCatalog.h"

#include "Package.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkReply>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QHash>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QXmlStreamReader>

namespace {

constexpr int kRowSize = 14;
constexpr int kPkgstatsLimit = 4000; // firefox sits around #700, blender around #2400
constexpr qint64 kPopularityFreshSecs = 6 * 3600;
// Packages above this are installed almost everywhere as a dependency (avahi, v4l-utils…): nobody
// "chose" them, so they make a poor recommendation.
constexpr double kMaxPopularity = 85.0;

bool discoverCategory(const QString &c) {
    // Toolchains and system plumbing: still searchable, but not a front-page recommendation.
    static const QSet<QString> skip = {QStringLiteral("Development"), QStringLiteral("System"),
                                       QStringLiteral("Settings"), QStringLiteral("Qt"),
                                       QStringLiteral("GTK"), QStringLiteral("Translation")};
    return !skip.contains(c);
}

} // namespace

ArchCatalog::ArchCatalog(QObject *parent) : QObject(parent) {
    // After the window is up: parsing happens off the GUI thread anyway.
    QTimer::singleShot(0, this, &ArchCatalog::reload);
}

QString ArchCatalog::catalogDir() {
    const QByteArray env = qgetenv("THINGS_APPSTREAM_DIR"); // tests / alternative location
    return env.isEmpty() ? QStringLiteral("/usr/share/swcatalog/xml") : QString::fromLocal8Bit(env);
}

QString ArchCatalog::popularityCache() {
    return QDir::home().filePath(QStringLiteral(".cache/things/archpop.json"));
}

QList<ArchCatalog::App> ArchCatalog::parseCatalog(const QString &dir, const QString &lang) {
    QList<App> out;
    for (const QString &repo : {QStringLiteral("core"), QStringLiteral("extra"), QStringLiteral("multilib")}) {
        const QString file = dir + QLatin1Char('/') + repo + QStringLiteral(".xml.gz");
        if (!QFile::exists(file))
            continue;
        QProcess gz;
        gz.start(QStringLiteral("gzip"), {QStringLiteral("-dc"), QStringLiteral("--"), file});
        if (!gz.waitForFinished(20000) || gz.exitCode() != 0)
            continue;
        const QByteArray data = gz.readAllStandardOutput();

        QXmlStreamReader xml(data);
        int depth = 0;
        bool inComp = false, isApp = false;
        QStringList stack;
        App cur;
        QString nameLoc, summaryLoc;
        auto localized = [&](QString &plain, QString &loc) {
            const QString l = xml.attributes().value(QStringLiteral("xml:lang")).toString();
            const QString text = xml.readElementText().trimmed();
            --depth;
            stack.removeLast(); // readElementText consumed the end tag
            if (l.isEmpty())
                plain = text;
            else if (l == lang || l.section(QLatin1Char('_'), 0, 0) == lang)
                loc = text;
        };
        while (!xml.atEnd()) {
            const auto tok = xml.readNext();
            if (tok == QXmlStreamReader::StartElement) {
                ++depth;
                const QString n = xml.name().toString();
                stack.append(n);
                if (n == QLatin1String("component") && depth == 2) {
                    inComp = true;
                    isApp = xml.attributes().value(QStringLiteral("type")) == QLatin1String("desktop-application");
                    cur = App();
                    cur.repo = repo;
                    nameLoc.clear();
                    summaryLoc.clear();
                } else if (inComp && isApp && depth == 3) {
                    if (n == QLatin1String("pkgname")) {
                        const QString t = xml.readElementText().trimmed();
                        --depth;
                        stack.removeLast();
                        if (cur.pkg.isEmpty())
                            cur.pkg = t;
                    } else if (n == QLatin1String("name")) {
                        localized(cur.name, nameLoc);
                    } else if (n == QLatin1String("summary")) {
                        localized(cur.summary, summaryLoc);
                    } else if (n == QLatin1String("developer_name") && cur.developer.isEmpty()) {
                        cur.developer = xml.readElementText().trimmed();
                        --depth;
                        stack.removeLast();
                    } else if (n == QLatin1String("icon")
                               && xml.attributes().value(QStringLiteral("type")) == QLatin1String("stock")) {
                        cur.stock = xml.readElementText().trimmed();
                        --depth;
                        stack.removeLast();
                    }
                } else if (inComp && isApp && depth == 4 && n == QLatin1String("category")
                           && stack.value(2) == QLatin1String("categories") && cur.category.isEmpty()) {
                    cur.category = xml.readElementText().trimmed();
                    --depth;
                    stack.removeLast();
                }
            } else if (tok == QXmlStreamReader::EndElement) {
                if (xml.name() == QLatin1String("component") && depth == 2) {
                    if (inComp && isApp && !cur.pkg.isEmpty()) {
                        if (!nameLoc.isEmpty())
                            cur.name = nameLoc;
                        if (!summaryLoc.isEmpty())
                            cur.summary = summaryLoc;
                        if (!cur.name.isEmpty() && !cur.summary.isEmpty())
                            out.append(cur);
                    }
                    inComp = false;
                }
                --depth;
                if (!stack.isEmpty())
                    stack.removeLast();
            }
        }
    }
    return out;
}

void ArchCatalog::reload() {
    if (m_loading)
        return;
    const QString dir = catalogDir();
    m_available = QFile::exists(dir + QStringLiteral("/extra.xml.gz"));
    if (!m_available) {
        m_items.clear();
        emit changed();
        return;
    }
    m_loading = true;
    emit changed();

    const QString lang = QLocale::system().name().section(QLatin1Char('_'), 0, 0);
    auto *thread = QThread::create([this, dir, lang] {
        const QList<App> apps = parseCatalog(dir, lang);
        QMetaObject::invokeMethod(this, [this, apps] { catalogParsed(apps); }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ArchCatalog::catalogParsed(const QList<App> &apps) {
    if (apps.isEmpty()) {
        m_loading = false;
        m_items.clear();
        emit changed();
        return;
    }
    fetchPopularity(apps);
}

void ArchCatalog::fetchPopularity(const QList<App> &apps) {
    // Cached popularity (for the app names only) is good for a few hours.
    QHash<QString, double> cached;
    qint64 cachedTs = 0;
    QFile f(popularityCache());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        cachedTs = root.value(QLatin1String("ts")).toInteger();
        const QJsonObject pop = root.value(QLatin1String("pop")).toObject();
        for (auto it = pop.begin(); it != pop.end(); ++it)
            cached.insert(it.key(), it.value().toDouble());
    }
    if (!cached.isEmpty() && QDateTime::currentSecsSinceEpoch() - cachedTs < kPopularityFreshSecs) {
        finish(apps, cached);
        return;
    }

    QNetworkRequest req(QUrl(QStringLiteral("https://pkgstats.archlinux.de/api/packages?limit=%1&query=").arg(kPkgstatsLimit)));
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Things/0.1 (+https://github.com/iMacnucintosh/things)"));
    req.setTransferTimeout(20000);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, apps, cached] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            finish(apps, cached); // stale data beats none; empty cache → no row
            return;
        }
        const QJsonArray rows = QJsonDocument::fromJson(reply->readAll()).object()
                                    .value(QLatin1String("packagePopularities")).toArray();
        QSet<QString> wanted;
        for (const App &a : apps)
            wanted.insert(a.pkg);
        QHash<QString, double> pop;
        QJsonObject store;
        for (const QJsonValue &v : rows) {
            const QJsonObject o = v.toObject();
            const QString name = o.value(QLatin1String("name")).toString();
            if (wanted.contains(name)) {
                const double p = o.value(QLatin1String("popularity")).toDouble();
                pop.insert(name, p);
                store.insert(name, p);
            }
        }
        if (!pop.isEmpty()) {
            QDir().mkpath(QFileInfo(popularityCache()).absolutePath());
            QSaveFile out(popularityCache());
            if (out.open(QIODevice::WriteOnly)) {
                out.write(QJsonDocument(QJsonObject{{QStringLiteral("ts"), QDateTime::currentSecsSinceEpoch()},
                                                    {QStringLiteral("pop"), store}})
                              .toJson(QJsonDocument::Compact));
                out.commit();
            }
        }
        finish(apps, pop.isEmpty() ? cached : pop);
    });
}

void ArchCatalog::finish(const QList<App> &apps, const QHash<QString, double> &popularity) {
    struct Ranked {
        App app;
        double pop;
    };
    QList<Ranked> ranked;
    QSet<QString> seen; // one entry per package (LibreOffice, Qt tools… ship several components)
    for (const App &a : apps) {
        if (seen.contains(a.pkg))
            continue;
        seen.insert(a.pkg);
        const auto it = popularity.constFind(a.pkg);
        if (it != popularity.constEnd() && (it.value() > kMaxPopularity || !discoverCategory(a.category)))
            continue;
        // Without a theme icon the card would be a bare letter: keep the row for polished entries.
        if (it == popularity.constEnd() || a.stock.isEmpty() || !QIcon::hasThemeIcon(a.stock)
            || !Validate::pacmanName(a.pkg))
            continue;
        ranked.append({a, it.value()});
    }
    std::sort(ranked.begin(), ranked.end(), [](const Ranked &x, const Ranked &y) { return x.pop > y.pop; });

    QVariantList items;
    for (const Ranked &r : std::as_const(ranked)) {
        items.append(QVariantMap{
            {QStringLiteral("id"), r.app.pkg},
            {QStringLiteral("name"), r.app.name},
            {QStringLiteral("summary"), r.app.summary},
            {QStringLiteral("icon"), QStringLiteral("image://theme/") + r.app.stock},
            {QStringLiteral("developer"), r.app.developer},
            {QStringLiteral("category"), r.app.category},
            {QStringLiteral("popularity"), r.pop},
            {QStringLiteral("repo"), r.app.repo},
        });
        if (items.size() >= kRowSize)
            break;
    }
    m_items = items;
    m_loading = false;
    emit changed();
}
