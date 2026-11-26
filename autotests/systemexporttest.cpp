// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine.h"
#include "systemcalendars.h"
#include "systemexport.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

/// The settings of an agent for a calendar file, as Akonadi lets them be set.
class FakeAgent : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.Akonadi.ICal.Settings")

public:
    QString file;
    QString displayName;
    bool readOnly = false;
    bool monitorFile = false;
    int saved = 0;

public Q_SLOTS:
    QString path()
    {
        return file;
    }
    void setPath(const QString &path)
    {
        file = path;
    }
    void setDisplayName(const QString &name)
    {
        displayName = name;
    }
    void setReadOnly(bool on)
    {
        readOnly = on;
    }
    void setMonitorFile(bool on)
    {
        monitorFile = on;
    }
    void save()
    {
        ++saved;
    }
};

/// Akonadi's control, which makes and removes agents when asked to.
class FakeManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Akonadi.AgentManager")

public:
    QString run;
    QHash<QString, FakeAgent *> agents;
    QHash<QString, QString> names;
    QStringList synchronized;
    int made = 0;

public Q_SLOTS:
    QStringList agentInstances()
    {
        return agents.keys();
    }
    QString createAgentInstance(const QString &type)
    {
        const QString instance = type + u'_' + QString::number(made++);
        auto *agent = new FakeAgent;
        // Each on a connection of its own, as each is a program of its own.
        QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, instance);
        bus.registerObject(u"/Settings"_s, agent, QDBusConnection::ExportAllSlots);
        bus.registerService(u"org.freedesktop.Akonadi.Resource."_s + instance + u'.' + run);
        agents.insert(instance, agent);
        return instance;
    }
    void removeAgentInstance(const QString &instance)
    {
        QDBusConnection::disconnectFromBus(instance);
        delete agents.take(instance);
        names.remove(instance);
    }
    QString agentInstanceName(const QString &instance)
    {
        return names.value(instance);
    }
    void setAgentInstanceName(const QString &instance, const QString &name)
    {
        names.insert(instance, name);
    }
    void agentInstanceSynchronize(const QString &instance)
    {
        synchronized.append(instance);
    }
};

/// Handing Kalendar's events to Akonadi, with a stand-in for Akonadi on the
/// session bus that runs under a name of its own (AKONADI_INSTANCE), so that
/// the real one is left alone.
class SystemExportTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    FakeManager m_manager;

    static QByteArray contentsOf(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static QString addEvent(Engine &engine, const QString &summary, int day)
    {
        const QDateTime start(QDate(2026, 11, day), QTime(14, 0));
        return engine.saveEvent({{u"collectionId"_s, engine.collections().first().id},
                                 {u"summary"_s, summary},
                                 {u"start"_s, start},
                                 {u"end"_s, start.addSecs(3600)},
                                 {u"allDay"_s, false},
                                 {u"repeat"_s, u"none"_s}});
    }

private Q_SLOTS:
    void initTestCase()
    {
        // What is remembered from one start to the next goes to a place for tests.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(u"kalendar-tests"_s);
        QCoreApplication::setApplicationName(u"systemexporttest"_s);
        QSettings().remove(u"SystemExport"_s);
    }

    void handsTheEventsToAkonadi()
    {
        if (!QDBusConnection::sessionBus().isConnected()) {
            QSKIP("There is no session bus to put a stand-in for Akonadi on.");
        }
        m_manager.run = u"kalendartest%1"_s.arg(QCoreApplication::applicationPid());
        qputenv("AKONADI_INSTANCE", m_manager.run.toLatin1());
        const QString file = m_dir.filePath(u"data/kalendar.ics"_s);

        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"export.sqlite"_s)));
        QCOMPARE(addEvent(engine, u"Dentist"_s, 12), QString());

        {
            // Without Akonadi there is nothing to add to, and nothing is left behind.
            SystemExport system(&engine, file, false);
            QSignalSpy changed(&system, &SystemExport::changed);
            QTest::qWait(300);
            QVERIFY(!system.isAvailable());
            QVERIFY(!system.isShown());
            system.add();
            QTRY_VERIFY(!system.isBusy());
            QVERIFY(!system.problem().isEmpty());
            QVERIFY(!system.isShown());
            QVERIFY(!QFile::exists(file));

            // Akonadi runs.
            QDBusConnection control = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"fake-control"_s);
            control.registerObject(u"/AgentManager"_s, &m_manager, QDBusConnection::ExportAllSlots);
            QVERIFY(control.registerService(u"org.freedesktop.Akonadi.Control."_s + m_manager.run));
            system.refresh();
            QTRY_VERIFY(system.isAvailable());
            QVERIFY(!system.isShown());

            // The events become a calendar file, and an agent of Akonadi's is
            // made for it: to show it, to follow it, and not to write to it.
            system.add();
            QVERIFY(system.isBusy());
            QTRY_VERIFY_WITH_TIMEOUT(!system.isBusy(), 10000);
            QCOMPARE(system.problem(), QString());
            QTRY_VERIFY(system.isShown());
            QVERIFY(contentsOf(file).contains("SUMMARY:Dentist"));
            QCOMPARE(m_manager.agents.size(), 1);
            const QString instance = m_manager.agents.constBegin().key();
            QVERIFY(instance.startsWith("akonadi_ical_resource_"_L1));
            const FakeAgent *agent = m_manager.agents.value(instance);
            QCOMPARE(agent->file, file);
            QVERIFY(agent->readOnly);
            QVERIFY(agent->monitorFile);
            QCOMPARE(agent->saved, 1);
            QCOMPARE(agent->displayName, u"Kalendar"_s);
            QCOMPARE(m_manager.names.value(instance), u"Kalendar"_s);
            QCOMPARE(m_manager.synchronized, QStringList{instance});

            // Added once more, nothing comes twice.
            system.add();
            QTRY_VERIFY_WITH_TIMEOUT(!system.isBusy(), 10000);
            QCOMPARE(m_manager.agents.size(), 1);

            // The file follows the calendar by itself.
            QCOMPARE(addEvent(engine, u"Cinema"_s, 13), QString());
            QTRY_VERIFY_WITH_TIMEOUT(contentsOf(file).contains("SUMMARY:Cinema"), 5000);
        }
        {
            // Kalendar is started again: it knows that the file is wanted, and
            // finds that it is still shown. Its own events do not come back to
            // it as a calendar of the system; those of other agents do.
            QVERIFY(QFile::remove(file));
            SystemExport system(&engine, file, false);
            QVERIFY(contentsOf(file).contains("SUMMARY:Cinema"));
            QTRY_VERIFY(system.isShown());
            QVERIFY(system.isAvailable());

            const QString other = m_manager.createAgentInstance(u"akonadi_ical_resource"_s);
            m_manager.agents.value(other)->file = m_dir.filePath(u"other.ics"_s);
            SystemCalendars reader(false);
            reader.leaveOut(file);
            QSignalSpy found(&reader, &SystemCalendars::found);
            reader.refresh();
            QTRY_COMPARE(found.size(), 1);
            const QList<CalendarInfo> calendars = found.first().at(0).value<QList<CalendarInfo>>();
            QCOMPARE(calendars.size(), 1);
            QCOMPARE(calendars.first().url, QUrl::fromLocalFile(m_dir.filePath(u"other.ics"_s)));

            // Removed in another program: that is found out by asking.
            const QString instance = m_manager.names.constBegin().key();
            m_manager.removeAgentInstance(instance);
            system.refresh();
            QTRY_VERIFY(!system.isShown());
            system.add();
            QTRY_VERIFY_WITH_TIMEOUT(!system.isBusy(), 10000);
            QTRY_VERIFY(system.isShown());
            QCOMPARE(m_manager.agents.size(), 2);

            // Taken away again: the agent goes, the file goes, and the file
            // is no longer written. The other program's agent stays.
            system.remove();
            QTRY_VERIFY_WITH_TIMEOUT(!system.isBusy(), 10000);
            QTRY_VERIFY(!system.isShown());
            QCOMPARE(m_manager.agents.size(), 1);
            QVERIFY(m_manager.agents.contains(other));
            QVERIFY(!QFile::exists(file));
            QCOMPARE(addEvent(engine, u"Yoga"_s, 14), QString());
            QTest::qWait(1500);
            QVERIFY(!QFile::exists(file));
        }
        // And it stays away at the next start.
        SystemExport system(&engine, file, false);
        QTest::qWait(300);
        QVERIFY(!system.isShown());
        QVERIFY(!QFile::exists(file));
    }

    void doesNothingInTestMode()
    {
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"testmode.sqlite"_s)));
        const QString file = m_dir.filePath(u"testmode.ics"_s);
        SystemExport system(&engine, file, true);
        system.add();
        QTRY_VERIFY(!system.isBusy());
        QVERIFY(!system.isAvailable());
        QVERIFY(!system.isShown());
        QVERIFY(!QFile::exists(file));
    }

    void clearsAwayTheOldPlugin()
    {
        // What earlier versions put into the user's folders.
        const QString home = m_dir.filePath(u"home"_s);
        const QString plugin = home + u"/.local/lib/kalendar/plugins/plasmacalendarplugins/kalendarevents.so"_s;
        const QString script = home + u"/.config/plasma-workspace/env/kalendar-clock-plugin.sh"_s;
        const QString neighbour = home + u"/.config/plasma-workspace/env/another.sh"_s;
        for (const QString &path : {plugin, script, neighbour}) {
            QVERIFY(QDir().mkpath(QFileInfo(path).path()));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        QVERIFY(SystemExport::retireClockPlugin(home, home + u"/.config"_s));
        QVERIFY(!QFile::exists(plugin));
        QVERIFY(!QFile::exists(script));
        QVERIFY(!QFile::exists(home + u"/.local/lib/kalendar"_s));
        // What is not Kalendar's stays.
        QVERIFY(QFile::exists(neighbour));
        // There is nothing the second time.
        QVERIFY(!SystemExport::retireClockPlugin(home, home + u"/.config"_s));
    }
};

QTEST_GUILESS_MAIN(SystemExportTest)

#include "systemexporttest.moc"
