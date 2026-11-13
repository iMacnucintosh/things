#pragma once

#include "Package.h"
#include "PackageModel.h"

#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <functional>

/// Search / installed lists / package details for pacman, AUR (RPC + yay) and Flatpak.
/// Everything is asynchronous (QProcess + QNetworkAccessManager); stale results are
/// discarded through a generation counter and old child processes are killed.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(PackageModel *model READ model CONSTANT)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QString sourceFilter READ sourceFilter WRITE setSourceFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString stateFilter READ stateFilter WRITE setStateFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString kindFilter READ kindFilter WRITE setKindFilter NOTIFY filtersChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(bool loadingInstalled READ loadingInstalled NOTIFY loadingInstalledChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QVariantMap counts READ counts NOTIFY countsChanged)
    Q_PROPERTY(int installedTotal READ installedTotal NOTIFY installedChanged)
    Q_PROPERTY(bool hasPacman READ hasPacman CONSTANT)
    Q_PROPERTY(bool hasYay READ hasYay CONSTANT)
    Q_PROPERTY(bool hasFlatpak READ hasFlatpak CONSTANT)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
    Q_PROPERTY(bool detailLoading READ detailLoading NOTIFY detailChanged)
    Q_PROPERTY(QStringList detailFiles READ detailFiles NOTIFY detailFilesChanged)
    Q_PROPERTY(bool detailFilesLoading READ detailFilesLoading NOTIFY detailFilesChanged)
    /// Installed packages with a newer version, as the shell found them: "source:id" → {from, to}
    Q_PROPERTY(QVariantMap pendingUpdates READ pendingUpdates NOTIFY pendingUpdatesChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    PackageModel *model() { return &m_model; }
    QString query() const { return m_query; }
    void setQuery(const QString &q);
    QString mode() const { return m_mode; }
    void setMode(const QString &m);
    QString sourceFilter() const { return m_sourceFilter; }
    void setSourceFilter(const QString &s);
    QString stateFilter() const { return m_stateFilter; }
    void setStateFilter(const QString &s);
    QString kindFilter() const { return m_kindFilter; }
    void setKindFilter(const QString &s);
    bool searching() const { return m_pending > 0; }
    bool loadingInstalled() const { return m_instPending > 0; }
    QString notice() const { return m_notice; }
    QVariantMap counts() const { return m_counts; }
    int installedTotal() const { return m_instPacman.size() + m_instFlatpak.size(); }
    bool hasPacman() const { return m_hasPacman; }
    bool hasYay() const { return m_hasYay; }
    bool hasFlatpak() const { return m_hasFlatpak; }
    QVariantMap detail() const { return m_detail; }
    bool detailLoading() const { return m_detailLoading; }
    QStringList detailFiles() const { return m_detailFiles; }
    bool detailFilesLoading() const { return m_filesLoading; }
    QVariantMap pendingUpdates() const { return m_pendingUpdates; }

    /// Flatpak installation to target: "system" when a system remote exists, else "user".
    QString flatpakInstallation() const { return m_flatpakInstallation; }
    /// Installation ("system"/"user") an installed Flatpak lives in; empty if unknown.
    QString flatpakInstalledIn(const QString &appId) const;
    /// First Flatpak remote name (flathub preferred).
    QString defaultRemote() const { return m_defaultRemote; }
    Q_INVOKABLE bool isInstalled(const QString &source, const QString &id) const;

    Q_INVOKABLE void refreshInstalled();
    Q_INVOKABLE void retrySearch();
    Q_INVOKABLE void loadDetail(const QString &source, const QString &id, const QString &repo);
    Q_INVOKABLE void closeDetail();
    Q_INVOKABLE void loadFiles(const QString &source, const QString &id);
    /// Theme icon name for the package, or "" when the theme has none.
    Q_INVOKABLE QString iconFor(const QString &source, const QString &id);
    Q_INVOKABLE QString formatSize(double bytes) const;
    /// Ask the running shell which packages have a newer version (its updates list); nothing if it is not running.
    Q_INVOKABLE void loadShellUpdates();
    /// Updating is the shell's job (one password, in the background): open Nexus on its updates page.
    Q_INVOKABLE void openShellUpdates();

    /// Used by headless self-tests (--dump-*).
    QVector<Package> currentItems() const { return m_model.items(); }

    static QProcessEnvironment cleanEnv();
    static QString which(const QString &prog);

signals:
    void queryChanged();
    void modeChanged();
    void filtersChanged();
    void searchingChanged();
    void loadingInstalledChanged();
    void noticeChanged();
    void countsChanged();
    void installedChanged();
    void detailChanged();
    void detailFilesChanged();
    void pendingUpdatesChanged();
    /// Emitted when a search round or installed load settles (used by self-tests).
    void settled();

private:
    using Callback = std::function<void(int, const QByteArray &, const QByteArray &)>;
    QProcess *run(const QString &prog, const QStringList &args, Callback cb, int timeoutMs = 25000,
                  QList<QPointer<QProcess>> *bucket = nullptr);

    void startSearch();
    void cancelSearch();
    void searchPacman(int gen, const QStringList &tokens);
    void searchAur(int gen, const QStringList &tokens);
    void searchAurViaYay(int gen, const QStringList &tokens);
    void searchFlatpak(int gen, const QString &query);
    void ingest(int gen, const QString &source, QVector<Package> pkgs, const QString &err = {});
    void finishOne(int gen);
    void rebuild();
    void markInstalled(Package &p) const;
    double scorePackage(const Package &p) const;

    void loadInstalled();
    void finishInstalled();
    void loadFlatpakRemotes();
    void matchShellUpdates();

    void loadPacmanDetail(int gen, const QString &id, const QString &repo, bool installed,
                          QVariantMap base);
    void loadAurDetail(int gen, const QString &id, QVariantMap base);
    void loadFlatpakDetail(int gen, const QString &id, const QString &repo, bool installed,
                           QVariantMap base);
    void setDetail(int gen, const QVariantMap &d, bool loading);

    static QVector<Package> parsePacmanSearch(const QByteArray &out);
    static QVector<Package> parseYaySearch(const QByteArray &out);
    static QVector<Package> parseFlatpakTable(const QByteArray &out, const QString &defaultRepo,
                                              bool hasDescriptionCol);
    static QList<QPair<QString, QStringList>> parseKeyBlocks(const QByteArray &out);
    static QVariantMap field(const QString &label, const QString &value, const QString &kind = {});

    PackageModel m_model;
    QNetworkAccessManager m_net;
    QString m_query;
    QString m_mode = QStringLiteral("search");
    QString m_sourceFilter = QStringLiteral("all");
    QString m_stateFilter = QStringLiteral("all");
    QString m_kindFilter = QStringLiteral("all");
    QString m_notice;
    QVariantMap m_counts;
    QTimer m_debounce;

    // search state
    int m_gen = 0;
    int m_pending = 0;
    QStringList m_tokens;
    QHash<QString, QVector<Package>> m_bySource;
    QList<QPointer<QProcess>> m_searchProcs;
    QList<QPointer<QNetworkReply>> m_searchReplies;

    // installed state
    int m_instPending = 0;
    bool m_instDirty = false;
    QHash<QString, Package> m_instPacman;   // pacman + AUR (foreign), keyed by name
    QHash<QString, Package> m_instFlatpak;  // keyed by app id
    QHash<QString, QString> m_flatpakInstalledIn;
    QHash<QString, QString> m_repoByName;
    QSet<QString> m_foreign;
    QSet<QString> m_orphans;
    QVector<Package> m_tmpPacman;
    QVector<Package> m_tmpFlatpak;
    QList<QPointer<QProcess>> m_instProcs;

    // detail state
    int m_detailGen = 0;
    QVariantMap m_detail;
    bool m_detailLoading = false;
    QStringList m_detailFiles;
    bool m_filesLoading = false;
    int m_filesGen = 0;
    QList<QPointer<QProcess>> m_detailProcs;
    QPointer<QNetworkReply> m_detailReply;

    // updates: what the shell lists ("source:id" → {from, to}), and those still pending for what is installed
    QHash<QString, QPair<QString, QString>> m_shellUpdates;
    QVariantMap m_pendingUpdates;

    QHash<QString, QString> m_iconCache;
    bool m_hasPacman = false;
    bool m_hasYay = false;
    bool m_hasFlatpak = false;
    QString m_flatpakInstallation = QStringLiteral("system");
    QString m_defaultRemote = QStringLiteral("flathub");
};
