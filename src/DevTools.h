#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QString>

/// Developer helper (not shown in --help): `--shot-dir <dir>` lets the QML demo script
/// save window screenshots, so the UI can be reviewed headless (QT_QPA_PLATFORM=offscreen).
class DevTools : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled CONSTANT)
    Q_PROPERTY(QString demo READ demo CONSTANT)
    Q_PROPERTY(QString select READ select CONSTANT)
public:
    DevTools(const QString &dir, const QString &demo, const QString &select, QObject *parent = nullptr)
        : QObject(parent), m_dir(dir), m_demo(demo), m_select(select) {}
    bool enabled() const { return !m_dir.isEmpty(); }
    QString demo() const { return m_demo; }
    QString select() const { return m_select; }
    void setWindow(QQuickWindow *w) { m_win = w; }

    Q_INVOKABLE bool take(const QString &name);
    Q_INVOKABLE void quit();

private:
    QString m_dir, m_demo, m_select;
    QPointer<QQuickWindow> m_win;
};
