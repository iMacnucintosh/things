#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantMap>

/// Extra detail for a Flatpak from the Flathub AppStream API: screenshots (with captions), the
/// full description, the latest release notes and project links. Used by the detail drawer.
///
/// Everything is validated before reaching QML: image URLs must be on dl.flathub.org (so the app
/// never contacts third-party hosts), links must be http(s), text is reduced to plain text.
class FlathubInfo : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap info READ info NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)

public:
    explicit FlathubInfo(QObject *parent = nullptr);

    /// { id, screenshots: [{ url, full, caption }], description, developer, categories: [str],
    ///   release: { version, date, notes }, links: [{ kind, url }] }; empty until loaded.
    QVariantMap info() const { return m_info; }
    bool loading() const { return !m_loadingId.isEmpty(); }

    Q_INVOKABLE void load(const QString &appId);
    Q_INVOKABLE void clear();

signals:
    void changed();

private:
    QVariantMap parse(const QString &appId, const QByteArray &json) const;

    QNetworkAccessManager m_nam;
    QVariantMap m_info;
    QString m_loadingId;
    QHash<QString, QVariantMap> m_cache; // session cache: reopening an app is instant
};
