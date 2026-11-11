#include "ArchCatalog.h"
#include "Backend.h"
#include "DevTools.h"
#include "Discover.h"
#include "HubCatalog.h"
#include "FlathubInfo.h"
#include "Operations.h"
#include "Privilege.h"
#include "Theme.h"
#include "ThemeIconProvider.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkRequest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>

namespace {

/// Remote images (Flathub icons/screenshots in "Descubrir") are cached on disk so the front
/// page does not re-download them on every start.
class ImageCacheFactory : public QQmlNetworkAccessManagerFactory {
public:
    QNetworkAccessManager *create(QObject *parent) override {
        auto *nam = new QNetworkAccessManager(parent);
        auto *cache = new QNetworkDiskCache(nam);
        cache->setCacheDirectory(QDir::home().filePath(QStringLiteral(".cache/things/img")));
        cache->setMaximumCacheSize(120LL * 1024 * 1024);
        nam->setCache(cache);
        nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
        return nam;
    }
};

void loadSylphrenaUiFont() {
    const QStringList candidates = {
        QDir::home().filePath(QStringLiteral(
            ".config/quickshell/sylphrena/assets/google-sans-flex/"
            "GoogleSansFlex-VariableFont_GRAD,ROND,opsz,slnt,wdth,wght.ttf")),
        QDir::home().filePath(QStringLiteral(".local/share/things/fonts/GoogleSansFlex.ttf")),
        QDir::home().filePath(QStringLiteral(".local/share/seeker/fonts/GoogleSansFlex.ttf")),
    };
    for (const QString &path : candidates) {
        if (QFile::exists(path) && QFontDatabase::addApplicationFont(path) >= 0)
            return;
    }
}

QString line(const Package &p) {
    return QStringLiteral("%1\t%2/%3\t%4\t%5%6\t%7")
        .arg(p.source, p.repo, p.id, p.version, p.installed ? QStringLiteral("[installed] ") : QString(),
             p.source == QLatin1String("aur") ? QStringLiteral("+%1").arg(p.votes) : QString(),
             p.description.left(70));
}

/// Headless self-test: `things --dump-search firefox`, `--dump-installed`, `--dump-detail aur:yay`.
int runDump(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addOption({QStringLiteral("dump-search"), QStringLiteral("Print unified search results"), QStringLiteral("term")});
    parser.addOption({QStringLiteral("dump-installed"), QStringLiteral("Print installed packages")});
    parser.addOption({QStringLiteral("dump-hub"), QStringLiteral("Print the Sylphrena Hub rows (THINGS_HUB_INDEX replaces the network)")});
    parser.addOption({QStringLiteral("dump-detail"), QStringLiteral("Print package detail"), QStringLiteral("source:id[:repo]")});
    parser.addOption({QStringLiteral("filter-source"), QStringLiteral("all|pacman|aur|flatpak"), QStringLiteral("s")});
    parser.addOption({QStringLiteral("filter-kind"), QStringLiteral("all|explicit|dep|orphan"), QStringLiteral("k")});
    parser.process(app);

    QTextStream out(stdout);
    if (parser.isSet(QStringLiteral("dump-hub"))) {
        HubCatalog hub;
        auto dumpHub = [&] {
            out << "# tool: " << (hub.tool().isEmpty() ? QStringLiteral("(not found)") : hub.tool()) << '\n';
            out << "# " << hub.items().size() << " rows, " << hub.updates() << " updates\n";
            for (const QVariant &v : hub.items()) {
                const QVariantMap r = v.toMap();
                out << r.value(QStringLiteral("state")).toString().leftJustified(10) << ' '
                    << r.value(QStringLiteral("id")).toString().leftJustified(22) << ' '
                    << r.value(QStringLiteral("installedVersion")).toString().leftJustified(8) << "-> "
                    << r.value(QStringLiteral("version")).toString().leftJustified(8) << ' '
                    << (r.value(QStringLiteral("system")).toBool() ? "[root] " : "       ")
                    << r.value(QStringLiteral("requires")).toString() << "  | "
                    << r.value(QStringLiteral("name")).toString() << " — "
                    << r.value(QStringLiteral("description")).toString() << '\n';
            }
            if (!hub.notice().isEmpty())
                out << "# notice: " << hub.notice() << '\n';
            out.flush();
        };
        hub.refresh(true);
        if (hub.loading()) {
            QObject::connect(&hub, &HubCatalog::loadingChanged, &app, [&] {
                if (!hub.loading()) {
                    dumpHub();
                    QCoreApplication::quit();
                }
            });
            QTimer::singleShot(20000, &app, [] { QCoreApplication::exit(3); });
            return app.exec();
        }
        dumpHub();
        return 0;
    }
    Backend be;
    if (parser.isSet(QStringLiteral("filter-source")))
        be.setSourceFilter(parser.value(QStringLiteral("filter-source")));
    if (parser.isSet(QStringLiteral("filter-kind")))
        be.setKindFilter(parser.value(QStringLiteral("filter-kind")));

    auto dumpList = [&] {
        const auto items = be.currentItems();
        out << "# " << items.size() << " results; counts:";
        const QVariantMap c = be.counts();
        for (auto it = c.begin(); it != c.end(); ++it)
            out << ' ' << it.key() << '=' << it.value().toInt();
        out << '\n';
        int n = 0;
        for (const Package &p : items) {
            out << line(p) << '\n';
            if (++n >= 40)
                break;
        }
        if (!be.notice().isEmpty())
            out << "# notice: " << be.notice() << '\n';
        out.flush();
        QCoreApplication::quit();
    };

    if (parser.isSet(QStringLiteral("dump-search"))) {
        be.setMode(QStringLiteral("search"));
        QObject::connect(&be, &Backend::installedChanged, &app, [&] {
            be.setQuery(parser.value(QStringLiteral("dump-search")));
            // Debounce (250 ms) + network: poll until the search round has settled.
            auto *poll = new QTimer(&app);
            poll->setInterval(150);
            int ticks = 0;
            QObject::connect(poll, &QTimer::timeout, &app, [&, poll, ticks]() mutable {
                if (++ticks > 4 && !be.searching()) {
                    poll->stop();
                    dumpList();
                }
            });
            poll->start();
        });
    } else if (parser.isSet(QStringLiteral("dump-installed"))) {
        be.setMode(QStringLiteral("installed"));
        QObject::connect(&be, &Backend::installedChanged, &app, dumpList);
    } else if (parser.isSet(QStringLiteral("dump-detail"))) {
        const QStringList parts = parser.value(QStringLiteral("dump-detail")).split(QLatin1Char(':'));
        QObject::connect(&be, &Backend::installedChanged, &app, [&, parts] {
            be.loadDetail(parts.value(0), parts.value(1), parts.value(2));
        });
        QObject::connect(&be, &Backend::detailChanged, &app, [&] {
            if (be.detailLoading() || be.detail().isEmpty())
                return;
            const QVariantMap d = be.detail();
            out << d.value(QStringLiteral("name")).toString() << ' ' << d.value(QStringLiteral("version")).toString()
                << " [" << d.value(QStringLiteral("source")).toString() << '/' << d.value(QStringLiteral("repo")).toString()
                << "] installed=" << d.value(QStringLiteral("installed")).toBool() << '\n'
                << d.value(QStringLiteral("description")).toString() << '\n';
            if (d.contains(QStringLiteral("error")))
                out << "ERROR: " << d.value(QStringLiteral("error")).toString() << '\n';
            for (const QVariant &v : d.value(QStringLiteral("fields")).toList()) {
                const QVariantMap f = v.toMap();
                out << "  " << f.value(QStringLiteral("label")).toString() << ": "
                    << f.value(QStringLiteral("value")).toString().left(110) << '\n';
            }
            out.flush();
            QCoreApplication::quit();
        });
    }
    QTimer::singleShot(60000, &app, [] { QCoreApplication::exit(3); });
    return app.exec();
}

} // namespace

int main(int argc, char *argv[]) {
    // Askpass mode: sudo -A runs this same binary to fetch the in-memory password.
    if (qEnvironmentVariableIsSet("THINGS_ASKPASS_SOCK"))
        return Privilege::runAskpass();

    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]).startsWith("--dump-"))
            return runDump(argc, argv);
    }

    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Sylphrena"));
    app.setApplicationName(QStringLiteral("Things"));
    app.setApplicationDisplayName(QStringLiteral("Paquetes"));
    app.setDesktopFileName(QStringLiteral("things"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("things"),
                                       QIcon(QStringLiteral(":/data/things.svg"))));
    loadSylphrenaUiFont();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Things — gestor de paquetes Material You (pacman · AUR · Flatpak)"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("dry-run"),
                      QStringLiteral("Simula instalaciones/eliminaciones sin tocar el sistema")});
    parser.addOption({QStringLiteral("search"), QStringLiteral("Búsqueda inicial"), QStringLiteral("texto")});
    parser.addOption({QStringLiteral("installed"), QStringLiteral("Abrir en la vista de instalados")});
    parser.addOption({QStringLiteral("shot-dir"), QStringLiteral("(dev) screenshot output dir"), QStringLiteral("dir")});
    parser.addOption({QStringLiteral("demo"), QStringLiteral("(dev) scripted UI demo"), QStringLiteral("name")});
    parser.addOption({QStringLiteral("select"), QStringLiteral("(dev) open detail source:id[:repo]"), QStringLiteral("pkg")});
    parser.addOption({QStringLiteral("hub-op"), QStringLiteral("(dev) queue a Sylphrena Hub operation: install:<id> | remove:<id>"), QStringLiteral("op")});
    parser.process(app);

    QQuickWindow::setDefaultAlphaBuffer(true);

    Theme theme;
    Backend backend;
    Operations ops(&backend);
    ops.setDryRun(parser.isSet(QStringLiteral("dry-run")) || qEnvironmentVariableIsSet("THINGS_DRY_RUN"));
    QObject::connect(&ops, &Operations::operationFinished, &backend,
                     [&](bool ok, const QString &, const QString &, const QString &) {
        if (ok && !ops.dryRun())
            backend.refreshInstalled();
    });

    HubCatalog hub;
    ops.setHub(&hub);
    QObject::connect(&ops, &Operations::operationFinished, &hub,
                     [&](bool, const QString &, const QString &source, const QString &) {
        if (source == QLatin1String("sylphrena"))
            hub.rescan();
    });

    ImageCacheFactory imageCache;
    Discover discover;
    ArchCatalog archCatalog;
    FlathubInfo flathubInfo;
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&imageCache);
    engine.addImageProvider(QStringLiteral("theme"), new ThemeIconProvider());
    engine.rootContext()->setContextProperty(QStringLiteral("Theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("Things"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("Ops"), &ops);
    engine.rootContext()->setContextProperty(QStringLiteral("Discover"), &discover);
    engine.rootContext()->setContextProperty(QStringLiteral("Hub"), &hub);
    engine.rootContext()->setContextProperty(QStringLiteral("ArchApps"), &archCatalog);
    engine.rootContext()->setContextProperty(QStringLiteral("FlathubInfo"), &flathubInfo);
    auto *devp = new DevTools(parser.value(QStringLiteral("shot-dir")), parser.value(QStringLiteral("demo")),
                              parser.value(QStringLiteral("select")), &app);
    DevTools &dev = *devp;
    engine.rootContext()->setContextProperty(QStringLiteral("Dev"), &dev);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (parser.isSet(QStringLiteral("hub-op"))) {
        // (dev) the same call the Install / Remove buttons end up making, without needing a click
        const QStringList op = parser.value(QStringLiteral("hub-op")).split(QLatin1Char(':'));
        if (op.size() == 2 && op[0] == QLatin1String("open") && !engine.rootObjects().isEmpty()) {
            // (dev) open the Sylphrena section on an extension's details, without needing a click
            QObject *root = engine.rootObjects().first();
            QTimer::singleShot(2500, &app, [root, id = op[1]] {
                QMetaObject::invokeMethod(root, "showSection", Q_ARG(QVariant, QStringLiteral("sylphrena")));
                QMetaObject::invokeMethod(root, "openHubDetail", Q_ARG(QVariant, id));
            });
        } else if (op.size() == 2 && op[0] == QLatin1String("zip")) {
            // (dev) install a local zip: prepare (inspect) first, as the confirmation dialog does, then queue it
            ops.prepare(QStringLiteral("install"), QStringLiteral("sylphrena-zip"), op[1], QString());
            QObject::connect(&ops, &Operations::prepared, &app, [&ops, op](const QVariantMap &r) {
                qInfo().noquote() << "prepared:" << r.value(QStringLiteral("ok")).toBool() << r.value(QStringLiteral("error")).toString()
                                  << r.value(QStringLiteral("packages")).toList().value(0).toMap().value(QStringLiteral("name")).toString();
                if (r.value(QStringLiteral("ok")).toBool())
                    ops.enqueue(QStringLiteral("install"), QStringLiteral("sylphrena-zip"), op[1], op[1], QString());
            });
        } else if (op.size() == 2) {
            QTimer::singleShot(1500, &app, [&ops, op, &hub] {
                const QVariantMap e = hub.entry(op[1]);
                ops.enqueue(op[0], QStringLiteral("sylphrena"), op[1], e.value(QStringLiteral("name"), op[1]).toString(), QString());
            });
        }
    }
    if (engine.rootObjects().isEmpty())
        return 1;
    dev.setWindow(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));

    if (parser.isSet(QStringLiteral("installed")))
        backend.setMode(QStringLiteral("installed"));
    if (parser.isSet(QStringLiteral("search"))) {
        backend.setMode(QStringLiteral("search"));
        backend.setQuery(parser.value(QStringLiteral("search")));
    }
    return app.exec();
}
