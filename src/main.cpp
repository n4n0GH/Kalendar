// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backend.h"
#include "engine.h"
#include "kalendar-version.h"
#include "clockplugin.h"
#include "desktopcolors.h"
#include "onlineaccounts.h"
#include "secretstore.h"
#include "singleinstance.h"

#include <KAboutData>
#include <KColorSchemeManager>
#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace
{
/// Tells the window when the pointer leaves it, which QML has no word for:
/// that is all a window gets to know of a click on its title bar.
class PointerWatcher : public QObject
{
public:
    using QObject::QObject;

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Leave) {
            QMetaObject::invokeMethod(watched, "dismissMenu");
        }
        return false;
    }
};

/// For trying things out and for taking pictures of the app, with
/// KALENDAR_TEST_MODE set: nothing of the user's is touched then.
///   KALENDAR_DEMO        fills the calendar with a few events around today
///   KALENDAR_EVAL        runs this JavaScript in the main window after start
///   KALENDAR_SCREENSHOT  saves a picture of the window there and quits
void setUpTestMode(Engine &engine, QQmlApplicationEngine &qml)
{
    if (qEnvironmentVariableIsSet("KALENDAR_DEMO") && engine.occurrences(QDate(2000, 1, 1), QDate(2100, 1, 1)).isEmpty()) {
        const QString calendar = engine.collections().first().id;
        const QDate today = QDate::currentDate();
        const auto add = [&](const QString &title, int day, int hour, int minutes, const QString &repeat = u"none"_s, const QString &location = {}) {
            const QDateTime start(today.addDays(day), QTime(hour, 0));
            engine.saveEvent({{u"collectionId"_s, calendar},
                              {u"summary"_s, title},
                              {u"location"_s, location},
                              {u"start"_s, start},
                              {u"end"_s, minutes == 0 ? start : start.addSecs(minutes * 60)},
                              {u"allDay"_s, minutes == 0},
                              {u"repeat"_s, repeat}});
        };
        add(u"Team Meeting"_s, -today.dayOfWeek() + 1, 10, 45, u"weekly"_s, u"Room 4"_s);
        add(u"Lunch with Ada"_s, 0, 12, 60, u"none"_s, u"Café Central"_s);
        add(u"Dentist"_s, 0, 15, 30);
        add(u"Call the Landlord"_s, 0, 15, 20);
        add(u"Cinema"_s, 1, 20, 120);
        add(u"Grace’s Birthday"_s, 2, 0, 0, u"yearly"_s);
        add(u"Yoga"_s, -1, 18, 60, u"weekly"_s);
        add(u"Project Deadline"_s, 5, 0, 0);
        add(u"Flight to Lisbon"_s, 8, 7, 150);
        const QDateTime trip(today.addDays(8), QTime(0, 0));
        engine.saveEvent({{u"collectionId"_s, calendar}, {u"summary"_s, u"Lisbon"_s}, {u"start"_s, trip}, {u"end"_s, trip.addDays(4)},
                          {u"allDay"_s, true}, {u"repeat"_s, u"none"_s}});
    }
    QObject *window = qml.rootObjects().value(0);
    const QString script = qEnvironmentVariable("KALENDAR_EVAL");
    if (window && !script.isEmpty()) {
        QTimer::singleShot(500, window, [window, script] {
            QQmlExpression expression(qmlContext(window), window, script);
            expression.evaluate();
            if (expression.hasError()) {
                qWarning("KALENDAR_EVAL: %s", qPrintable(expression.error().toString()));
            }
        });
    }
    const QString screenshot = qEnvironmentVariable("KALENDAR_SCREENSHOT");
    if (window && !screenshot.isEmpty()) {
        QTimer::singleShot(2500, window, [window, screenshot] {
            qobject_cast<QQuickWindow *>(window)->grabWindow().save(screenshot);
            QCoreApplication::quit();
        });
    }
}
}

int main(int argc, char **argv)
{
    const bool testMode = qEnvironmentVariableIsSet("KALENDAR_TEST_MODE");
    QStandardPaths::setTestModeEnabled(testMode);
    // What the desktop gave this start to bring a window to the front with;
    // Qt takes it away when it starts.
    const QString activationToken = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");

    // QApplication rather than QGuiApplication: the desktop style draws with widgets.
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("kalendar");

    KAboutData about(u"kalendar"_s,
                     i18nc("@title", "Kalendar"),
                     QStringLiteral(KALENDAR_VERSION_STRING),
                     i18nc("@info", "A simple calendar that works with online accounts"),
                     KAboutLicense::GPL_V3,
                     i18nc("@info:credit", "© 2026 Kalendar contributors"));
    about.setLicense(KAboutLicense::GPL_V3, KAboutLicense::OrLaterVersions);
    about.setDesktopFileName(u"io.github.n4n0gh.kalendar"_s);
    about.setHomepage(u"https://github.com/n4n0gh/kalendar"_s);
    about.setBugAddress("https://github.com/n4n0gh/kalendar/issues");
    KAboutData::setApplicationData(about);
    QGuiApplication::setWindowIcon(QIcon::fromTheme(u"io.github.n4n0gh.kalendar"_s, QIcon::fromTheme(u"office-calendar"_s)));

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    parser.addPositionalArgument(u"files"_s, i18nc("@info:shell", "Calendar files (.ics) to import"), i18nc("@info:shell placeholder for file names", "[files…]"));
    parser.process(app);
    about.processCommandLine(&parser);
    const QStringList files = parser.positionalArguments();

    // A Kalendar that is running already takes the files; tests keep to themselves.
    SingleInstance instance;
    if (!testMode && !instance.claim(files, activationToken)) {
        return 0;
    }

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(u"org.kde.desktop"_s);
    }
    KColorSchemeManager::instance();
    // In a Flatpak, the colors of the desktop as they change.
    DesktopColors desktopColors(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));

    const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDirectory);
    Engine engine;
    if (!engine.open(dataDirectory + u"/calendars.sqlite"_s)) {
        qCritical("Cannot open the calendar database: %s", qPrintable(engine.errorString()));
        return 1;
    }
    SecretStore secrets(testMode);
    OnlineAccounts online(testMode);
    Backend backend(&engine, &secrets, &online);
    Backend::setInstance(&backend);
    // An installed plugin is to be of this version of Kalendar.
    ClockPlugin().refresh();

    QQmlApplicationEngine qml;
    KLocalization::setupLocalizedContext(&qml);
    QObject::connect(
        &qml,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] {
            QCoreApplication::exit(1);
        },
        Qt::QueuedConnection);
    qml.loadFromModule("io.github.n4n0gh.kalendar", "Main");

    if (QObject *window = qml.rootObjects().value(0)) {
        window->installEventFilter(new PointerWatcher(window));
    }

    QObject::connect(&instance, &SingleInstance::activated, &app, [&qml](const QString &token) {
        if (auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().value(0))) {
            // The desktop lets a window come to the front only with the token of what asked for it.
            if (!token.isEmpty()) {
                qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
            }
            if (window->visibility() == QWindow::Minimized || !window->isVisible()) {
                window->showNormal();
            }
            window->raise();
            window->requestActivate();
        }
    });
    QObject::connect(&instance, &SingleInstance::filesOpened, &backend, [&backend](const QStringList &uris) {
        backend.openFiles(uris, QDir::currentPath());
    });
    // Once the window is there to ask where the events are to go.
    QTimer::singleShot(0, &backend, [&backend, files] {
        backend.openFiles(files, QDir::currentPath());
    });

    if (testMode) {
        setUpTestMode(engine, qml);
    }
    return app.exec();
}
