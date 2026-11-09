#pragma once

#include <QRegularExpression>
#include <QString>

/// One row of the unified list (search result or installed package).
struct Package {
    QString id;              // pacman/AUR package name, or Flatpak application id
    QString name;            // display name (Flatpak: human name; else == id)
    QString version;
    QString description;
    QString source;          // "pacman" | "aur" | "flatpak"
    QString repo;            // core/extra/multilib/…; "aur"; Flatpak remote
    bool installed = false;
    QString installedVersion;
    QString reason;          // "explicit" | "dep" | "" (unknown / not installed)
    bool orphan = false;
    int votes = 0;           // AUR
    double popularity = 0;   // AUR
    bool outOfDate = false;  // AUR
    qint64 size = 0;         // installed size in bytes when known
    double score = 0;        // search ranking

    QString key() const { return source + QLatin1Char(':') + id; }
};

namespace Validate {

inline bool pacmanName(const QString &s) {
    static const QRegularExpression re(QStringLiteral("^[a-z0-9@_+][a-z0-9@._+-]{0,127}$"));
    return re.match(s).hasMatch();
}

inline bool flatpakId(const QString &s) {
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,254}$"));
    return re.match(s).hasMatch();
}

inline bool remoteName(const QString &s) {
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}$"));
    return re.match(s).hasMatch();
}

inline bool packageId(const QString &source, const QString &id) {
    if (source == QLatin1String("flatpak"))
        return flatpakId(id);
    if (source == QLatin1String("pacman") || source == QLatin1String("aur"))
        return pacmanName(id);
    return false;
}

} // namespace Validate
