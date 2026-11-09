#include "DevTools.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>

bool DevTools::take(const QString &name) {
    if (!enabled() || !m_win)
        return false;
    QDir().mkpath(m_dir);
    const QImage img = m_win->grabWindow();
    return img.save(QDir(m_dir).filePath(name + QStringLiteral(".png")));
}

void DevTools::quit() {
    QCoreApplication::quit();
}
