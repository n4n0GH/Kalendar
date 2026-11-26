// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "systemcalendars.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

/// What Akonadi's control says of its agents.
class FakeManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Akonadi.AgentManager")

public:
    QStringList instances;
    QHash<QString, QString> names;

public Q_SLOTS:
    QStringList agentInstances()
    {
        return instances;
    }
    QString agentInstanceName(const QString &instance)
    {
        return names.value(instance);
    }

Q_SIGNALS:
    void agentInstanceAdded(const QString &instance);
    void agentInstanceRemoved(const QString &instance);
    void agentInstanceNameChanged(const QString &instance, const QString &name);
};

/// The settings of an agent for a calendar file.
class FakeFileAgent : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.Akonadi.ICal.Settings")

public:
    QString file;

public Q_SLOTS:
    QString path()
    {
        return file;
    }
};

/// The settings of an agent for a folder with a file for each entry.
class FakeFolderAgent : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.Akonadi.ICalDirectory.Settings")

public:
    QString folder;

public Q_SLOTS:
    QString path()
    {
        return folder;
    }
};

/// Asking Akonadi for its calendar files, with a stand-in for Akonadi on
/// the session bus. It runs under a name of its own (AKONADI_INSTANCE), as
/// a second Akonadi of one user does, so that the real one is left alone.
class SystemCalendarsTest : public QObject
{
    Q_OBJECT

    QString m_run;
    FakeManager m_manager;
    FakeFileAgent m_personal;
    FakeFileAgent m_todos;
    FakeFileAgent m_unset;
    FakeFolderAgent m_entries;
    int m_connections = 0;

    /// Each on a connection of its own, as each is a program of its own.
    QDBusConnection serve(const QString &service, const QString &path, QObject *object)
    {
        QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"fake-%1"_s.arg(++m_connections));
        bus.registerObject(path, object, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
        bus.registerService(service + u'.' + m_run);
        return bus;
    }

    void serveAgent(const QString &instance, QObject *settings)
    {
        m_manager.instances.append(instance);
        QVERIFY(serve(u"org.freedesktop.Akonadi.Resource."_s + instance, u"/Settings"_s, settings).isConnected());
    }

private Q_SLOTS:
    void initTestCase()
    {
        if (!QDBusConnection::sessionBus().isConnected()) {
            QSKIP("There is no session bus to put a stand-in for Akonadi on.");
        }
        m_run = u"kalendartest%1"_s.arg(QCoreApplication::applicationPid());
        qputenv("AKONADI_INSTANCE", m_run.toLatin1());
    }

    void findsTheCalendarFiles()
    {
        SystemCalendars system(false);
        QSignalSpy found(&system, &SystemCalendars::found);

        // While Akonadi does not run, nothing is said: not that there are
        // calendars, and not that there are none.
        system.refresh();
        QTest::qWait(300);
        QCOMPARE(found.size(), 0);

        // It starts, with agents of all kinds. Only those for calendar
        // files count, and of those, the ones that have a file.
        m_manager.instances = {u"akonadi_maildir_resource_0"_s, u"akonadi_birthdays_resource"_s};
        m_personal.file = u"/home/ada/.local/share/apps/korganizer/std.ics"_s;
        m_manager.names.insert(u"akonadi_ical_resource_0"_s, u"Personal Calendar"_s);
        serveAgent(u"akonadi_ical_resource_0"_s, &m_personal);
        serveAgent(u"akonadi_ical_resource_1"_s, &m_unset);
        m_entries.folder = u"/home/ada/entries/"_s;
        m_manager.names.insert(u"akonadi_icaldir_resource_0"_s, u"Entries"_s);
        serveAgent(u"akonadi_icaldir_resource_0"_s, &m_entries);
        // Without a name of its own, and with a path that could be shorter.
        m_todos.file = u"/home/ada/lists/../.var/app/org.example.Lists/data/to-dos.ics"_s;
        serveAgent(u"akonadi_ical_resource_12"_s, &m_todos);
        const QDBusConnection control = serve(u"org.freedesktop.Akonadi.Control"_s, u"/AgentManager"_s, &m_manager);
        QVERIFY(control.isConnected());

        // That it started is noticed without asking.
        QTRY_COMPARE_WITH_TIMEOUT(found.size(), 1, 10000);
        QCOMPARE(found.first().at(1).toBool(), true);
        QList<CalendarInfo> calendars = found.first().at(0).value<QList<CalendarInfo>>();
        QCOMPARE(calendars.size(), 3);
        QCOMPARE(calendars.at(0).url, QUrl::fromLocalFile(u"/home/ada/.local/share/apps/korganizer/std.ics"_s));
        QCOMPARE(calendars.at(0).name, u"Personal Calendar"_s);
        QVERIFY(calendars.at(0).readOnly);
        QCOMPARE(calendars.at(1).url, QUrl::fromLocalFile(u"/home/ada/entries"_s));
        QCOMPARE(calendars.at(1).name, u"Entries"_s);
        QCOMPARE(calendars.at(2).url, QUrl::fromLocalFile(u"/home/ada/.var/app/org.example.Lists/data/to-dos.ics"_s));
        QCOMPARE(calendars.at(2).name, u"to-dos"_s);

        // An agent is set up and named, which Akonadi tells.
        m_unset.file = u"/home/ada/work.ics"_s;
        m_manager.names.insert(u"akonadi_ical_resource_1"_s, u"Work"_s);
        Q_EMIT m_manager.agentInstanceNameChanged(u"akonadi_ical_resource_1"_s, u"Work"_s);
        QTRY_COMPARE_WITH_TIMEOUT(found.size(), 2, 10000);
        calendars = found.last().at(0).value<QList<CalendarInfo>>();
        QCOMPARE(calendars.size(), 4);
        QCOMPARE(calendars.at(1).name, u"Work"_s);
        QCOMPARE(found.last().at(1).toBool(), true);

        // One is removed, and one does not answer: that one may still be
        // there, so what is found is not all there is.
        m_manager.instances.removeAll(u"akonadi_icaldir_resource_0"_s);
        m_manager.instances.append(u"akonadi_ical_resource_13"_s);
        Q_EMIT m_manager.agentInstanceRemoved(u"akonadi_icaldir_resource_0"_s);
        QTRY_COMPARE_WITH_TIMEOUT(found.size(), 3, 10000);
        calendars = found.last().at(0).value<QList<CalendarInfo>>();
        QCOMPARE(calendars.size(), 3);
        QCOMPARE(found.last().at(1).toBool(), false);

        // Asked, it answers again.
        m_manager.instances.removeAll(u"akonadi_ical_resource_13"_s);
        system.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(found.size(), 4, 10000);
        QCOMPARE(found.last().at(0).value<QList<CalendarInfo>>().size(), 3);
        QCOMPARE(found.last().at(1).toBool(), true);
    }

    void takesThemFromTheEnvironmentInTestMode()
    {
        qputenv("KALENDAR_SYSTEM_CALENDARS", "To-Dos=/tmp/lists/to-dos.ics,Other=/tmp/other.ics");
        SystemCalendars system(true);
        QSignalSpy found(&system, &SystemCalendars::found);
        system.refresh();
        QTRY_COMPARE(found.size(), 1);
        const QList<CalendarInfo> calendars = found.first().at(0).value<QList<CalendarInfo>>();
        QCOMPARE(calendars.size(), 2);
        QCOMPARE(calendars.at(0).name, u"To-Dos"_s);
        QCOMPARE(calendars.at(0).url, QUrl::fromLocalFile(u"/tmp/lists/to-dos.ics"_s));
        QCOMPARE(calendars.at(1).name, u"Other"_s);
    }
};

QTEST_GUILESS_MAIN(SystemCalendarsTest)

#include "systemcalendarstest.moc"
