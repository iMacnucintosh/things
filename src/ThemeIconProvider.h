#pragma once

#include <QQuickImageProvider>

/// Freedesktop theme icons (Papirus via qt6ct) exposed as image://theme/<name>.
class ThemeIconProvider : public QQuickImageProvider {
public:
    ThemeIconProvider();
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
};
