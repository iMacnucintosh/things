#include "JsonTranslator.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

bool JsonTranslator::loadFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return false;
    const QJsonObject o = doc.object();
    for (auto it = o.begin(); it != o.end(); ++it)
        if (it.value().isString())
            m_map.insert(it.key(), it.value().toString());
    return !m_map.isEmpty();
}

QString JsonTranslator::translate(const char *, const char *sourceText, const char *disambiguation, int) const {
    if (!sourceText)
        return {};
    const QString src = QString::fromUtf8(sourceText);
    if (disambiguation && *disambiguation) {
        const auto it = m_map.constFind(QString::fromUtf8(disambiguation) + QChar(4) + src);
        if (it != m_map.constEnd())
            return it.value();
    }
    const auto it = m_map.constFind(src);
    return it != m_map.constEnd() ? it.value() : QString();
}
