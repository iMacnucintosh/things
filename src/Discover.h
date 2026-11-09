#pragma once

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class QNetworkReply;

/// "Descubrir": trending / popular / new / recently updated apps from the Flathub API, with
/// icons and one screenshot for the featured app. Cached on disk (instant start, offline fallback).
///
/// Everything that comes from the network is validated before it reaches QML: app ids go through
/// Validate::flatpakId, image URLs must be on dl.flathub.org.
class Discover : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sections READ sections NOTIFY changed)
    Q_PROPERTY(QVariantMap hero READ hero NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)

public:
    explicit Discover(QObject *parent = nullptr);

    /// [{ id: "trending", items: [{ id, name, summary, icon, developer, installs, category,
    ///    added, updated }] }, ...]
    QVariantList sections() const { return m_sections; }
    /// Featured app (same fields as an item, plus `screenshot`); empty when there is no data.
    QVariantMap hero() const { return m_hero; }
    bool loading() const { return m_pending > 0; }
    bool hasData() const { return !m_sections.isEmpty(); }
    QString notice() const { return m_notice; }

    /// Fetch fresh data. Without `force`, does nothing while the cache is younger than an hour.
    Q_INVOKABLE void refresh(bool force = false);
    /// 240647 → "240 mil", 1.2 million → "1,2 M".
    Q_INVOKABLE QString formatInstalls(double n) const;

signals:
    void changed();
    void loadingChanged();

private:
    struct Collection {
        QString id;
        QString path;
    };

    static QString cachePath();
    void loadCache();
    void saveCache() const;
    QVariantMap parseHit(const QJsonObject &o) const;
    void fetchCollection(const Collection &c);
    void collectionDone(const QString &id, const QVariantList &items, bool ok);
    void pickHero();
    void fetchHeroScreenshot();
    QNetworkRequest request(const QUrl &url) const;

    QNetworkAccessManager m_nam;
    QVariantList m_sections;
    QVariantMap m_hero;
    QString m_notice;
    QDateTime m_fetchedAt;
    int m_pending = 0;
    int m_failed = 0;
    QVariantMap m_fresh; // id → items collected during the current refresh
};
