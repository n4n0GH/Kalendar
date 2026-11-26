// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "systemcalendars.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDir>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
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
    /// Whether Akonadi lets the calendar be changed, and looks at the file
    /// again when another program wrote to it.
    bool locked = false;
    bool watches = true;

public Q_SLOTS:
    QString path()
    {
        return file;
    }
    bool readOnly()
    {
        return locked;
    }
    bool monitorFile()
    {
        return watches;
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
    bool readOnly()
    {
        return false;
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
    QTemporaryDir m_dir;

    /// A file that was last written @p days days ago.
    static void makeFile(const QString &path, int days)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).path()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n");
        // Written first: writing is what sets the time.
        file.close();
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QDateTime::currentDateTime().addDays(-days), QFileDevice::FileModificationTime));
    }

    static QStringList filesIn(const QString &folder)
    {
        return QDir(folder).entryList(QDir::Files, QDir::Name);
    }

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
        // What is remembered from one start to the next goes to a place for tests.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(u"kalendar-tests"_s);
        QCoreApplication::setApplicationName(u"systemcalendarstest"_s);
        QSettings().remove(u"SystemCalendars"_s);
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
        m_todos.locked = true;
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
        // One that Akonadi lets be changed may be changed; a folder of
        // files is only read, and so is one that Akonadi keeps to itself.
        QVERIFY(!calendars.at(0).readOnly);
        QCOMPARE(calendars.at(1).url, QUrl::fromLocalFile(u"/home/ada/entries"_s));
        QCOMPARE(calendars.at(1).name, u"Entries"_s);
        QVERIFY(calendars.at(1).readOnly);
        QVERIFY(calendars.at(2).readOnly);
        QCOMPARE(calendars.at(2).url, QUrl::fromLocalFile(u"/home/ada/.var/app/org.example.Lists/data/to-dos.ics"_s));
        QCOMPARE(calendars.at(2).name, u"to-dos"_s);

        // An agent is set up and named, which Akonadi tells.
        // It does not look at its file again when that is written to, so
        // what is written there would be lost: it is only read.
        m_unset.file = u"/home/ada/work.ics"_s;
        m_unset.watches = false;
        m_manager.names.insert(u"akonadi_ical_resource_1"_s, u"Work"_s);
        Q_EMIT m_manager.agentInstanceNameChanged(u"akonadi_ical_resource_1"_s, u"Work"_s);
        QTRY_COMPARE_WITH_TIMEOUT(found.size(), 2, 10000);
        calendars = found.last().at(0).value<QList<CalendarInfo>>();
        QCOMPARE(calendars.size(), 4);
        QCOMPARE(calendars.at(1).name, u"Work"_s);
        QVERIFY(calendars.at(1).readOnly);
        QVERIFY(!calendars.at(0).readOnly);
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

        // Akonadi's agents keep a copy of their calendar for every change
        // another program makes to its file. Those of a file that Kalendar
        // wrote to are cleared away, the old ones; those of others are not.
        const QString data = m_dir.filePath(u"agents"_s);
        const QString personal = data + u"/akonadi_ical_resource_0"_s;
        const QString todos = data + u"/akonadi_ical_resource_12"_s;
        for (int number = 1; number <= 8; ++number) {
            makeFile(personal + u"/std.ics-%1"_s.arg(number), 100 - number);
            makeFile(todos + u"/to-dos.ics-%1"_s.arg(number), 100 - number);
        }
        system.setAgentData(data);
        system.noteWritten(QUrl::fromLocalFile(u"/home/ada/.local/share/apps/korganizer/std.ics"_s));
        system.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(found.size(), 5, 10000);
        QCOMPARE(filesIn(personal), (QStringList{u"std.ics-4"_s, u"std.ics-5"_s, u"std.ics-6"_s, u"std.ics-7"_s, u"std.ics-8"_s}));
        QCOMPARE(filesIn(todos).size(), 8);
        QCOMPARE(QSettings().value(u"SystemCalendars/written"_s).toStringList().size(), 1);

        // Kalendar is started again: it still knows which file it wrote to.
        makeFile(personal + u"/std.ics-1"_s, 40);
        makeFile(personal + u"/std.ics-2"_s, 2);
        SystemCalendars later(false);
        later.setAgentData(data);
        QSignalSpy foundLater(&later, &SystemCalendars::found);
        later.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(foundLater.size(), 1, 10000);
        // The newest five stay, the two new ones among them, whatever their
        // numbers; the two that are left over are old, and go.
        QCOMPARE(filesIn(personal), (QStringList{u"std.ics-1"_s, u"std.ics-2"_s, u"std.ics-6"_s, u"std.ics-7"_s, u"std.ics-8"_s}));

        // A calendar that Akonadi no longer has is forgotten, and nothing of it is touched.
        later.noteWritten(QUrl::fromLocalFile(u"/home/ada/gone.ics"_s));
        QCOMPARE(QSettings().value(u"SystemCalendars/written"_s).toStringList().size(), 2);
        later.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(foundLater.size(), 2, 10000);
        QCOMPARE(QSettings().value(u"SystemCalendars/written"_s).toStringList(), QStringList{u"/home/ada/.local/share/apps/korganizer/std.ics"_s});
    }

    void clearsAwayOnlyOldCopies()
    {
        const QString folder = m_dir.filePath(u"copies"_s);
        // Copies of the calendar, the lower numbers not always the older ones.
        makeFile(folder + u"/std.ics-1"_s, 3);
        makeFile(folder + u"/std.ics-2"_s, 400);
        makeFile(folder + u"/std.ics-3"_s, 90);
        makeFile(folder + u"/std.ics-4"_s, 45);
        makeFile(folder + u"/std.ics-5"_s, 31);
        makeFile(folder + u"/std.ics-6"_s, 29);
        makeFile(folder + u"/std.ics-10"_s, 10);
        // What is not a copy of this calendar, however old.
        const QStringList others{u"std.ics"_s, u"std.ics-"_s, u"std.ics-2.bak"_s, u"std.ics-old"_s, u"other.ics-1"_s, u"xstd.ics-1"_s, u"notes.txt"_s};
        for (const QString &other : others) {
            makeFile(folder + u'/' + other, 500);
        }
        QVERIFY(QDir().mkpath(folder + u"/std.ics-99"_s)); // a folder of that name

        // Nothing goes while there are few, old as they may be.
        QCOMPARE(SystemCalendars::prune(folder, u"std.ics"_s, 7, 30), 0);
        // Nor what is not old, many as there may be.
        QCOMPARE(SystemCalendars::prune(folder, u"std.ics"_s, 0, 1000), 0);
        QCOMPARE(filesIn(folder).size(), 7 + others.size());

        // The newest three stay (3, 10 and 29 days), and of the rest, those of more than 30 days go.
        QCOMPARE(SystemCalendars::prune(folder, u"std.ics"_s, 3, 30), 4);
        QStringList left = others + QStringList{u"std.ics-1"_s, u"std.ics-10"_s, u"std.ics-6"_s};
        left.sort();
        QCOMPARE(filesIn(folder), left);
        QVERIFY(QFileInfo(folder + u"/std.ics-99"_s).isDir());
        // As it is used: five and thirty days.
        makeFile(folder + u"/std.ics-2"_s, 400);
        QCOMPARE(SystemCalendars::prune(folder, u"std.ics"_s), 0);
        makeFile(folder + u"/std.ics-3"_s, 31);
        makeFile(folder + u"/std.ics-4"_s, 500);
        QCOMPARE(SystemCalendars::prune(folder, u"std.ics"_s), 1);
        QVERIFY(!QFile::exists(folder + u"/std.ics-4"_s));
        // No folder, no name: nothing happens.
        QCOMPARE(SystemCalendars::prune(m_dir.filePath(u"none"_s), u"std.ics"_s), 0);
        QCOMPARE(SystemCalendars::prune(folder, QString()), 0);
        QCOMPARE(SystemCalendars::prune(QString(), u"std.ics"_s), 0);
    }

    void takesThemFromTheEnvironmentInTestMode()
    {
        qputenv("KALENDAR_SYSTEM_CALENDARS", "To-Dos=/tmp/lists/to-dos.ics,Other=rw:/tmp/other.ics");
        SystemCalendars system(true);
        QSignalSpy found(&system, &SystemCalendars::found);
        system.refresh();
        QTRY_COMPARE(found.size(), 1);
        const QList<CalendarInfo> calendars = found.first().at(0).value<QList<CalendarInfo>>();
        QCOMPARE(calendars.size(), 2);
        QCOMPARE(calendars.at(0).name, u"To-Dos"_s);
        QCOMPARE(calendars.at(0).url, QUrl::fromLocalFile(u"/tmp/lists/to-dos.ics"_s));
        QCOMPARE(calendars.at(1).name, u"Other"_s);
        QCOMPARE(calendars.at(1).url, QUrl::fromLocalFile(u"/tmp/other.ics"_s));
        QVERIFY(calendars.at(0).readOnly);
        QVERIFY(!calendars.at(1).readOnly);
    }
};

QTEST_GUILESS_MAIN(SystemCalendarsTest)

#include "systemcalendarstest.moc"
