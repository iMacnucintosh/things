#include "ThemeIconProvider.h"

#include <QIcon>

ThemeIconProvider::ThemeIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

QPixmap ThemeIconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) {
    const int w = requestedSize.width() > 0 ? requestedSize.width() : 48;
    const int h = requestedSize.height() > 0 ? requestedSize.height() : 48;
    QPixmap pix = QIcon::fromTheme(id).pixmap(w, h);
    if (size)
        *size = pix.size();
    return pix;
}
