#include "Theme.h"

#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {

QString thingsThemePath() {
    return QDir::home().filePath(QStringLiteral(".config/things/theme.json"));
}

QString schemePath() {
    return QDir::home().filePath(QStringLiteral(".local/state/sylphrena/scheme.json"));
}

QColor parseHex(const QString &raw) {
    QString s = raw.trimmed();
    if (s.isEmpty())
        return {};
    if (!s.startsWith(QLatin1Char('#')))
        s.prepend(QLatin1Char('#'));
    return QColor(s);
}

} // namespace

QString Theme::pickFamily(const QStringList &candidates) {
    const QStringList installed = QFontDatabase::families();
    for (const QString &name : candidates) {
        if (installed.contains(name))
            return name;
        // Qt often resolves "GoogleSansFlex" → "Google Sans Flex" without an exact list hit.
        const QFont probe{name};
        const QFontInfo info{probe};
        if (installed.contains(info.family()))
            return info.family();
    }
    return candidates.isEmpty() ? QStringLiteral("sans-serif") : candidates.last();
}

Theme::Theme(QObject *parent)
    : QObject(parent) {
    applyDefaults();
    reload();

    const QString theme = thingsThemePath();
    const QString scheme = schemePath();
    QFileInfo(theme).dir().mkpath(QStringLiteral("."));
    m_watcher.addPath(QFileInfo(theme).absolutePath());
    if (QFile::exists(theme))
        m_watcher.addPath(theme);
    if (QFile::exists(scheme))
        m_watcher.addPath(scheme);

    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        if (!m_watcher.files().contains(path) && QFile::exists(path))
            m_watcher.addPath(path);
        reload();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        const QString theme = thingsThemePath();
        if (QFile::exists(theme) && !m_watcher.files().contains(theme))
            m_watcher.addPath(theme);
        reload();
    });
}

QColor Theme::color(const QString &key, const QString &fallback) const {
    const QVariant v = m_colours.value(key);
    if (v.isValid()) {
        const QColor c = parseHex(v.toString());
        if (c.isValid())
            return c;
    }
    if (!fallback.isEmpty() && fallback != key)
        return color(fallback);
    return m_mode == QLatin1String("light") ? QColor(QStringLiteral("#f4ece8")) : QColor(QStringLiteral("#161310"));
}

QColor Theme::withAlpha(const QColor &c) const {
    if (!m_transparencyEnabled)
        return c;
    QColor out = c;
    out.setAlphaF(qBound(0.0, m_transparencyBase, 1.0));
    return out;
}

void Theme::applyDefaults() {
    m_mode = QStringLiteral("dark");
    m_transparencyEnabled = false;
    m_transparencyBase = 0.85;
    // Sylphrena tokens say "GoogleSansFlex"; the TTF registers "Google Sans Flex".
    m_uiFamily = pickFamily({
        QStringLiteral("Google Sans Flex"),
        QStringLiteral("GoogleSansFlex"),
        QStringLiteral("Rubik"),
        QStringLiteral("Noto Sans"),
        QStringLiteral("sans-serif"),
    });
    m_iconFamily = pickFamily({
        QStringLiteral("Material Symbols Rounded"),
        QStringLiteral("Material Symbols Outlined"),
        QStringLiteral("sans-serif"),
    });
    m_colours = {
        {QStringLiteral("background"), QStringLiteral("161310")},
        {QStringLiteral("surface"), QStringLiteral("161310")},
        {QStringLiteral("surfaceContainer"), QStringLiteral("231f1b")},
        {QStringLiteral("surfaceContainerHigh"), QStringLiteral("2d2925")},
        {QStringLiteral("surfaceContainerHighest"), QStringLiteral("38332f")},
        {QStringLiteral("surfaceContainerLow"), QStringLiteral("1e1b18")},
        {QStringLiteral("surfaceContainerLowest"), QStringLiteral("100e0c")},
        {QStringLiteral("onSurface"), QStringLiteral("e9e1dc")},
        {QStringLiteral("onSurfaceVariant"), QStringLiteral("d0c4b9")},
        {QStringLiteral("outline"), QStringLiteral("9b8f84")},
        {QStringLiteral("outlineVariant"), QStringLiteral("4e453c")},
        {QStringLiteral("primary"), QStringLiteral("e4c09b")},
        {QStringLiteral("onPrimary"), QStringLiteral("432c14")},
        {QStringLiteral("primaryContainer"), QStringLiteral("b99875")},
        {QStringLiteral("onPrimaryContainer"), QStringLiteral("1e1b18")},
        {QStringLiteral("secondaryContainer"), QStringLiteral("564538")},
        {QStringLiteral("onSecondaryContainer"), QStringLiteral("f4ece8")},
        {QStringLiteral("error"), QStringLiteral("ffb4ab")},
        {QStringLiteral("onError"), QStringLiteral("690005")},
    };
}

void Theme::loadFrom(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("mode")))
        m_mode = root.value(QStringLiteral("mode")).toString(m_mode);

    const QJsonObject tr = root.value(QStringLiteral("transparency")).toObject();
    if (!tr.isEmpty()) {
        if (tr.contains(QStringLiteral("enabled")))
            m_transparencyEnabled = tr.value(QStringLiteral("enabled")).toBool(false);
        if (tr.contains(QStringLiteral("base")))
            m_transparencyBase = tr.value(QStringLiteral("base")).toDouble(0.85);
    }

    QJsonObject colours = root.value(QStringLiteral("colours")).toObject();
    if (colours.isEmpty())
        colours = root.value(QStringLiteral("colors")).toObject();
    if (colours.isEmpty() && root.contains(QStringLiteral("primary")))
        colours = root;

    if (colours.isEmpty())
        return;

    QVariantMap map;
    for (auto it = colours.begin(); it != colours.end(); ++it)
        map.insert(it.key(), it.value().toVariant());
    m_colours = map;
}

void Theme::reload() {
    applyDefaults();
    // Prefer dedicated Things theme; fall back to Sylphrena scheme.json.
    if (QFile::exists(thingsThemePath()))
        loadFrom(thingsThemePath());
    else if (QFile::exists(schemePath()))
        loadFrom(schemePath());
    emit changed();
}
