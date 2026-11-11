#pragma once

#include <QDateTime>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QNetworkReply;

/// The Sylphrena Hub: extensions (and later widgets) published for the Sylphrena shell, read from the
/// hub's index.json, crossed with what is installed in ~/.local/share/sylphrena/extensions.
///
/// Installing, updating and removing are NOT done here: Operations runs the shell's own tool
/// (scripts/ext.py), which owns the extension contract (download, sha256, validation, system pieces,
/// receipt, telling the shell). This class only lists and decides what each row can do.
///
/// Everything that comes from the network is validated before it reaches QML: ids, versions, icon names,
/// download URLs (only the hub's releases) and sha256.
class HubCatalog : public QObject {
    Q_OBJECT
    /// [{ id, name, description, icon, kind, version, installedVersion, state, requires, system, size,
    ///    local }] — state: "available" | "installed" | "update" | "local"
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    /// Path of the shell's tool; empty when Sylphrena is not found (then nothing can be installed).
    Q_PROPERTY(QString tool READ tool NOTIFY toolChanged)
    Q_PROPERTY(int updates READ updates NOTIFY changed)

public:
    explicit HubCatalog(QObject *parent = nullptr);

    QVariantList items() const { return m_items; }
    bool loading() const { return m_pending > 0; }
    bool hasData() const { return !m_catalog.isEmpty() || !m_installed.isEmpty(); }
    QString notice() const { return m_notice; }
    QString tool() const { return m_tool; }
    int updates() const { return m_updates; }

    /// Fetch the index. Without `force`, does nothing while the cache is younger than an hour.
    Q_INVOKABLE void refresh(bool force = false);
    /// Look at the installed extensions again (after an operation).
    Q_INVOKABLE void rescan();
    /// Let the user choose the zip of an extension (Seeker's picker); zipPicked(path) when they do.
    Q_INVOKABLE void pickZip();

    /// Catalog entry of `id` (empty when it is not in the hub): { version, zip, sha256, system, size, name }.
    QVariantMap entry(const QString &id) const;
    /// Installed copy of `id` (empty when it is not installed): { version, system, local, name }.
    QVariantMap installedInfo(const QString &id) const;

    /// Pure helpers (also used by the headless self-test).
    static bool validId(const QString &id);
    static bool validZipUrl(const QString &url);
    /// Entries of an index.json that pass validation; `error` gets why the document was refused.
    static QVector<QVariantMap> parseIndex(const QByteArray &json, QString *error = nullptr);
    /// `installed` and `catalog` → rows for the QML list.
    static QVariantList merge(const QVector<QVariantMap> &catalog, const QHash<QString, QVariantMap> &installed);

signals:
    void changed();
    void loadingChanged();
    void toolChanged();
    void zipPicked(const QString &path);

private:
    static QString cachePath();
    static QString installedDir();
    QNetworkRequest request(const QUrl &url) const;
    void loadCache();
    void saveCache(const QByteArray &json);
    void ingest(const QByteArray &json, bool fromNet);
    void scanInstalled();
    void findTool();
    void rebuild();
    void finishOne();

    QNetworkAccessManager m_net;
    QVector<QVariantMap> m_catalog;
    QHash<QString, QVariantMap> m_installed;
    QVariantList m_items;
    QString m_notice;
    QString m_tool;
    bool m_picking = false;
    QDateTime m_fetchedAt;
    int m_pending = 0;
    int m_updates = 0;
    QPointer<QNetworkReply> m_reply;
};
