#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantList>

/// Arch applications for "Descubrir": the AppStream catalog shipped by `archlinux-appstream-data`
/// (names, summaries, theme icon names) ranked by how many Arch users have each package
/// installed (pkgstats.archlinux.de).
///
/// Parsing the catalog (~8 MB gzip) runs on a worker thread. Icons are the theme icon names
/// (the catalog's own icons are JPEG XL, which Qt cannot load here); screenshots are not used
/// because they live on third-party hosts.
class ArchCatalog : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    /// [{ id (package name), name, summary, icon, developer, category, popularity (0-100), repo }]
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)

public:
    explicit ArchCatalog(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool loading() const { return m_loading; }
    QVariantList items() const { return m_items; }

    /// Re-read the catalog (e.g. after archlinux-appstream-data was installed).
    Q_INVOKABLE void reload();

signals:
    void changed();

private:
    struct App {
        QString pkg, name, summary, developer, stock, category, repo;
    };

    static QString catalogDir();
    static QString popularityCache();
    static QList<App> parseCatalog(const QString &dir, const QString &lang);
    void catalogParsed(const QList<App> &apps);
    void fetchPopularity(const QList<App> &apps);
    void finish(const QList<App> &apps, const QHash<QString, double> &popularity);

    QNetworkAccessManager m_nam;
    QVariantList m_items;
    bool m_available = false;
    bool m_loading = false;
};
