#include "FlathubInfo.h"

#include "Package.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTextDocument>
#include <QUrl>

namespace {

const QString kImageHost = QStringLiteral("https://dl.flathub.org/");

bool imageUrl(const QString &u) {
    return u.startsWith(kImageHost) && QUrl(u).isValid();
}

bool webUrl(const QString &u) {
    const QUrl url(u);
    return url.isValid() && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"))
           && !url.host().isEmpty();
}

/// AppStream descriptions are a small HTML subset (<p>, <ul>/<li>, <em>, <code>): flatten to text.
QString htmlToText(const QString &html) {
    if (html.trimmed().isEmpty())
        return {};
    QString h = html;
    // Keep list items readable: QTextDocument would drop the bullets in plain text.
    h.replace(QRegularExpression(QStringLiteral("<li[^>]*>")), QStringLiteral("<p>• "));
    h.replace(QStringLiteral("</li>"), QStringLiteral("</p>"));
    QTextDocument doc;
    doc.setHtml(h);
    QString t = doc.toPlainText();
    t.replace(QRegularExpression(QStringLiteral("[ \\t]+\\n")), QStringLiteral("\n"));
    t.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    // Soft-wrapped source lines inside a paragraph come out with spaces already; just tidy.
    return t.trimmed();
}

} // namespace

FlathubInfo::FlathubInfo(QObject *parent) : QObject(parent) {}

void FlathubInfo::clear() {
    m_loadingId.clear();
    if (!m_info.isEmpty()) {
        m_info.clear();
        emit changed();
    }
}

void FlathubInfo::load(const QString &appId) {
    if (!Validate::flatpakId(appId)) {
        clear();
        return;
    }
    if (m_loadingId == appId
        || (m_info.value(QStringLiteral("id")).toString() == appId && m_loadingId.isEmpty()))
        return;
    const auto cached = m_cache.constFind(appId);
    if (cached != m_cache.constEnd()) {
        m_loadingId.clear();
        m_info = *cached;
        emit changed();
        return;
    }

    m_info.clear();
    m_loadingId = appId;
    emit changed();

    const QString lang = QLocale::system().name().section(QLatin1Char('_'), 0, 0);
    QUrl url(QStringLiteral("https://flathub.org/api/v2/appstream/") + appId);
    if (!lang.isEmpty() && lang != QLatin1String("C"))
        url.setQuery(QStringLiteral("locale=") + lang);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Things/0.1 (+https://github.com/iMacnucintosh/things)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(15000);

    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, appId] {
        reply->deleteLater();
        if (m_loadingId != appId)
            return; // the user moved on to another app meanwhile
        m_loadingId.clear();
        if (reply->error() != QNetworkReply::NoError) {
            m_info = QVariantMap{{QStringLiteral("id"), appId}}; // nothing extra, but "done"
            emit changed();
            return;
        }
        m_info = parse(appId, reply->readAll());
        m_cache.insert(appId, m_info);
        emit changed();
    });
}

QVariantMap FlathubInfo::parse(const QString &appId, const QByteArray &json) const {
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    QVariantMap out{{QStringLiteral("id"), appId}};

    // Screenshots: a ~750 px version for the drawer, the biggest reasonable one for full view.
    QVariantList shots;
    for (const QJsonValue &sv : o.value(QLatin1String("screenshots")).toArray()) {
        const QJsonObject so = sv.toObject();
        QString thumb, full;
        int thumbW = 0, fullW = 0, thumbBest = 1 << 30;
        for (const QJsonValue &zv : so.value(QLatin1String("sizes")).toArray()) {
            const QJsonObject z = zv.toObject();
            const QString src = z.value(QLatin1String("src")).toString();
            const int w = z.value(QLatin1String("width")).toString().toInt();
            if (!imageUrl(src) || w <= 0)
                continue;
            if (w <= 2400 && w > fullW) {
                full = src;
                fullW = w;
            }
            const int dist = qAbs(w - 752); // closest to 752
            if (dist < thumbBest) {
                thumb = src;
                thumbW = w;
                thumbBest = dist;
            }
        }
        Q_UNUSED(thumbW)
        if (!thumb.isEmpty())
            shots.append(QVariantMap{{QStringLiteral("url"), thumb},
                                     {QStringLiteral("full"), full.isEmpty() ? thumb : full},
                                     {QStringLiteral("caption"), so.value(QLatin1String("caption")).toString().trimmed()}});
        if (shots.size() >= 8)
            break;
    }
    out[QStringLiteral("screenshots")] = shots;

    out[QStringLiteral("description")] = htmlToText(o.value(QLatin1String("description")).toString());
    out[QStringLiteral("developer")] = o.value(QLatin1String("developer_name")).toString().trimmed();

    QStringList cats;
    for (const QJsonValue &c : o.value(QLatin1String("categories")).toArray())
        if (cats.size() < 3 && !c.toString().isEmpty())
            cats.append(c.toString());
    out[QStringLiteral("categories")] = cats;

    // Latest release: first stable entry.
    for (const QJsonValue &rv : o.value(QLatin1String("releases")).toArray()) {
        const QJsonObject r = rv.toObject();
        const QString version = r.value(QLatin1String("version")).toString();
        if (version.isEmpty())
            continue;
        const qint64 ts = r.value(QLatin1String("timestamp")).toString().toLongLong();
        out[QStringLiteral("release")] = QVariantMap{
            {QStringLiteral("version"), version},
            {QStringLiteral("date"), ts > 0 ? QDateTime::fromSecsSinceEpoch(ts).date().toString(Qt::ISODate) : QString()},
            {QStringLiteral("notes"), htmlToText(r.value(QLatin1String("description")).toString())},
        };
        break;
    }

    // Links (http/https only), in a fixed order.
    QVariantList links;
    const QJsonObject urls = o.value(QLatin1String("urls")).toObject();
    for (const QString &kind : {QStringLiteral("homepage"), QStringLiteral("help"), QStringLiteral("faq"),
                                QStringLiteral("bugtracker"), QStringLiteral("donation")}) {
        const QString u = urls.value(kind).toString();
        if (webUrl(u))
            links.append(QVariantMap{{QStringLiteral("kind"), kind}, {QStringLiteral("url"), u}});
    }
    out[QStringLiteral("links")] = links;
    return out;
}
