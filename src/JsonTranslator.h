#pragma once

#include <QHash>
#include <QString>
#include <QTranslator>

/// Translations from a JSON file: {"English source": "translation", "context\u0004English source": "…"}.
/// The code is written in English; a language without a file simply shows the source. A context (the second
/// argument of qsTr / tr) only matters when the same English text needs two different translations.
class JsonTranslator : public QTranslator {
public:
    using QTranslator::QTranslator;
    /// Load `path` (a file or a Qt resource); false if it is missing or not a JSON object.
    bool loadFile(const QString &path);
    QString translate(const char *context, const char *sourceText, const char *disambiguation = nullptr,
                      int n = -1) const override;
    bool isEmpty() const override { return m_map.isEmpty(); }

private:
    QHash<QString, QString> m_map;
};
