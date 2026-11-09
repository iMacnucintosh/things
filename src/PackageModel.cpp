#include "PackageModel.h"

PackageModel::PackageModel(QObject *parent)
    : QAbstractListModel(parent) {}

int PackageModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_items.size();
}

QVariant PackageModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const Package &p = m_items.at(index.row());
    switch (role) {
    case IdRole: return p.id;
    case NameRole: return p.name;
    case VersionRole: return p.version;
    case DescriptionRole: return p.description;
    case SourceRole: return p.source;
    case RepoRole: return p.repo;
    case InstalledRole: return p.installed;
    case InstalledVersionRole: return p.installedVersion;
    case ReasonRole: return p.reason;
    case OrphanRole: return p.orphan;
    case VotesRole: return p.votes;
    case PopularityRole: return p.popularity;
    case OutOfDateRole: return p.outOfDate;
    case SizeRole: return p.size;
    case KeyRole: return p.key();
    }
    return {};
}

QHash<int, QByteArray> PackageModel::roleNames() const {
    return {
        {IdRole, "pkgId"},
        {NameRole, "name"},
        {VersionRole, "version"},
        {DescriptionRole, "description"},
        {SourceRole, "source"},
        {RepoRole, "repo"},
        {InstalledRole, "installed"},
        {InstalledVersionRole, "installedVersion"},
        {ReasonRole, "reason"},
        {OrphanRole, "orphan"},
        {VotesRole, "votes"},
        {PopularityRole, "popularity"},
        {OutOfDateRole, "outOfDate"},
        {SizeRole, "size"},
        {KeyRole, "key"},
    };
}

void PackageModel::setItems(QVector<Package> items) {
    const int before = m_items.size();
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
    if (before != m_items.size())
        emit countChanged();
}

void PackageModel::refreshInstalled(const std::function<void(Package &)> &fn) {
    for (int i = 0; i < m_items.size(); ++i) {
        Package &p = m_items[i];
        const bool was = p.installed;
        const QString ver = p.installedVersion;
        fn(p);
        if (was != p.installed || ver != p.installedVersion)
            emit dataChanged(index(i), index(i), {InstalledRole, InstalledVersionRole});
    }
}
