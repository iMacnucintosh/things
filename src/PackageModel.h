#pragma once

#include "Package.h"

#include <QAbstractListModel>
#include <QVector>

class PackageModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        VersionRole,
        DescriptionRole,
        SourceRole,
        RepoRole,
        InstalledRole,
        InstalledVersionRole,
        ReasonRole,
        OrphanRole,
        VotesRole,
        PopularityRole,
        OutOfDateRole,
        SizeRole,
        KeyRole,
    };

    explicit PackageModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }
    const QVector<Package> &items() const { return m_items; }
    void setItems(QVector<Package> items);
    /// Updates `installed` flags in place (no reset → the list keeps its scroll position).
    void refreshInstalled(const std::function<void(Package &)> &fn);

signals:
    void countChanged();

private:
    QVector<Package> m_items;
};
