// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "systemcalendars.h"

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace
{
const QString ManagerPath = u"/AgentManager"_s;
const QString ManagerInterface = u"org.freedesktop.Akonadi.AgentManager"_s;
const QString SettingsPath = u"/Settings"_s;
const QString BusService = u"org.freedesktop.DBus"_s;
const QString BusPath = u"/org/freedesktop/DBus"_s;

/// Akonadi's kinds of agent that show calendar files, each with what its
/// settings are called on the bus. Its agents are called by their kind,
/// with a number.
struct Kind {
    QLatin1StringView name;
    QLatin1StringView settings;
    /// One file, which can be written to, rather than a folder of them.
    bool file;
};
constexpr Kind Kinds[] = {
    {"akonadi_ical_resource"_L1, "org.kde.Akonadi.ICal.Settings"_L1, true},
    {"akonadi_icaldir_resource"_L1, "org.kde.Akonadi.ICalDirectory.Settings"_L1, false},
};

constexpr int Timeout = 5000;
/// An agent that is busy, or gone, is not waited for long.
constexpr int AgentTimeout = 3000;
constexpr int SoonMilliseconds = 1000;
constexpr int AgainMilliseconds = 20 * 1000;
constexpr int Tries = 5;
constexpr int TidyMilliseconds = 30 * 1000;
/// The calendar files Kalendar wrote to, kept from one start to the next.
const QString WrittenKey = u"SystemCalendars/written"_s;

/// Where the programs of the system keep their data. Inside a Flatpak the
/// data folder is the app's own; the system's is named.
QString systemData()
{
    if (QFile::exists(u"/.flatpak-info"_s)) {
        return qEnvironmentVariable("HOST_XDG_DATA_HOME", QDir::homePath() + u"/.local/share"_s);
    }
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
}

/// Akonadi can run more than once for one user; each run has a name that
/// its services end in.
QString named(const QString &service)
{
    const QString run = qEnvironmentVariable("AKONADI_INSTANCE");
    return run.isEmpty() ? service : service + u'.' + run;
}

QString control()
{
    return named(u"org.freedesktop.Akonadi.Control"_s);
}

QString resource(const QString &instance)
{
    return named(u"org.freedesktop.Akonadi.Resource."_s + instance);
}

bool failed(const QDBusMessage &reply)
{
    return reply.type() != QDBusMessage::ReplyMessage;
}

CalendarInfo calendar(const QString &path, const QString &name, bool readOnly)
{
    CalendarInfo info;
    info.url = QUrl::fromLocalFile(QDir::cleanPath(path));
    // An agent may have no name; its file has one.
    info.name = name.trimmed().isEmpty() ? QFileInfo(path).completeBaseName() : name.trimmed();
    info.readOnly = readOnly;
    return info;
}
}

SystemCalendars::SystemCalendars(bool testMode, QObject *parent)
    : QObject(parent)
    , m_testMode(testMode)
    , m_agentData(systemData())
    , m_tidy(new QTimer(this))
    , m_soon(new QTimer(this))
    , m_again(new QTimer(this))
{
    m_tidy->setSingleShot(true);
    m_tidy->setInterval(TidyMilliseconds);
    connect(m_tidy, &QTimer::timeout, this, [this] {
        tidy(false);
    });
    m_soon->setSingleShot(true);
    m_soon->setInterval(SoonMilliseconds);
    connect(m_soon, &QTimer::timeout, this, &SystemCalendars::refresh);
    m_again->setSingleShot(true);
    m_again->setInterval(AgainMilliseconds);
    connect(m_again, &QTimer::timeout, this, &SystemCalendars::look);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (testMode || !bus.isConnected()) {
        return;
    }
    // Akonadi says when it gets, loses or renames an agent, and the bus says
    // when Akonadi starts. Listening starts nothing.
    for (const QString &change : {u"agentInstanceAdded"_s, u"agentInstanceRemoved"_s, u"agentInstanceNameChanged"_s}) {
        bus.connect(control(), ManagerPath, ManagerInterface, change, this, SLOT(agentsChanged()));
    }
    auto *watcher = new QDBusServiceWatcher(control(), bus, QDBusServiceWatcher::WatchForRegistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &SystemCalendars::agentsChanged);
}

void SystemCalendars::agentsChanged()
{
    m_soon->start();
}

void SystemCalendars::call(const QString &service,
                           const QString &path,
                           const QString &interface,
                           const QString &method,
                           const QVariantList &arguments,
                           const Reply &then,
                           int timeout)
{
    QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, timeout), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, then] {
        watcher->deleteLater();
        then(watcher->reply());
    });
}

void SystemCalendars::leaveOut(const QString &file)
{
    m_leftOut = QDir::cleanPath(file);
}

void SystemCalendars::refresh()
{
    m_tries = 0;
    m_again->stop();
    look();
}

void SystemCalendars::look()
{
    const quint64 round = ++m_round;
    m_asked.clear();
    if (m_testMode) {
        QList<CalendarInfo> calendars;
        const QStringList named = qEnvironmentVariable("KALENDAR_SYSTEM_CALENDARS").split(u',', Qt::SkipEmptyParts);
        for (const QString &entry : named) {
            const qsizetype split = entry.indexOf(u'=');
            if (split > 0) {
                // "rw:" before the file: one that may be changed.
                const QString file = entry.mid(split + 1);
                const bool writable = file.startsWith("rw:"_L1);
                calendars.append(calendar(writable ? file.mid(3) : file, entry.left(split), !writable));
            }
        }
        // Not from within the call: whoever asks is told later, as by a system.
        QTimer::singleShot(0, this, [this, round, calendars] {
            if (round == m_round) {
                finish(calendars, true);
            }
        });
        return;
    }
    if (!QDBusConnection::sessionBus().isConnected()) {
        return;
    }
    call(
        BusService,
        BusPath,
        BusService,
        u"NameHasOwner"_s,
        {control()},
        [this, round](const QDBusMessage &reply) {
            if (round != m_round || failed(reply)) {
                return;
            }
            if (!reply.arguments().value(0).toBool()) {
                // Not started to be asked. Where it cannot be started either,
                // there is no Akonadi and so there are no calendars of it; a
                // run with a name of its own is only ever started by hand.
                if (qEnvironmentVariableIsSet("AKONADI_INSTANCE")) {
                    return;
                }
                call(
                    BusService,
                    BusPath,
                    BusService,
                    u"ListActivatableNames"_s,
                    {},
                    [this, round](const QDBusMessage &reply) {
                        if (round == m_round && !failed(reply) && !reply.arguments().value(0).toStringList().contains(control())) {
                            finish({}, true);
                        }
                    },
                    Timeout);
                return;
            }
            call(
                control(),
                ManagerPath,
                ManagerInterface,
                u"agentInstances"_s,
                {},
                [this, round](const QDBusMessage &reply) {
                    if (round != m_round || failed(reply)) {
                        return;
                    }
                    QList<Agent> agents;
                    const QStringList instances = reply.arguments().value(0).toStringList();
                    for (const QString &instance : instances) {
                        for (const Kind &kind : Kinds) {
                            if (instance == kind.name || instance.startsWith(kind.name + u'_')) {
                                agents.append({instance, kind.settings, kind.file});
                            }
                        }
                    }
                    ask(round, agents, {}, true);
                },
                Timeout);
        },
        Timeout);
}

void SystemCalendars::ask(quint64 round, QList<Agent> agents, const QList<CalendarInfo> &calendars, bool complete)
{
    if (round != m_round) {
        return;
    }
    if (agents.isEmpty()) {
        finish(calendars, complete);
        return;
    }
    const Agent agent = agents.takeFirst();
    call(
        resource(agent.instance),
        SettingsPath,
        agent.interface,
        u"path"_s,
        {},
        [=, this](const QDBusMessage &reply) {
            if (failed(reply)) {
                // Not running right now, perhaps: its calendar is not known, which is not to say that it is gone.
                ask(round, agents, calendars, false);
                return;
            }
            const QString path = reply.arguments().value(0).toString();
            if (!QDir::isAbsolutePath(path) || QDir::cleanPath(path) == m_leftOut) {
                // Not set up yet, so that Akonadi shows nothing for it
                // either; or Kalendar's own events, on their way out.
                ask(round, agents, calendars, complete);
                return;
            }
            m_asked.insert(QDir::cleanPath(path), agent.instance);
            const auto named = [=, this](bool readOnly) {
                call(
                    control(),
                    ManagerPath,
                    ManagerInterface,
                    u"agentInstanceName"_s,
                    {agent.instance},
                    [=, this](const QDBusMessage &reply) {
                        const QString name = failed(reply) ? QString() : reply.arguments().value(0).toString();
                        ask(round, agents, calendars + QList<CalendarInfo>{calendar(path, name, readOnly)}, complete);
                    },
                    Timeout);
            };
            if (!agent.file) {
                named(true);
                return;
            }
            // A calendar may be changed if its agent lets the file be
            // changed, and looks at the file again when it was: one that
            // does not would write what it remembers over the change.
            const QString service = resource(agent.instance);
            call(
                service,
                SettingsPath,
                agent.interface,
                u"readOnly"_s,
                {},
                [=, this](const QDBusMessage &reply) {
                    if (failed(reply) || reply.arguments().value(0).toBool()) {
                        named(true);
                        return;
                    }
                    call(
                        service,
                        SettingsPath,
                        agent.interface,
                        u"monitorFile"_s,
                        {},
                        [=](const QDBusMessage &reply) {
                            named(failed(reply) || !reply.arguments().value(0).toBool());
                        },
                        AgentTimeout);
                },
                AgentTimeout);
        },
        AgentTimeout);
}

void SystemCalendars::finish(const QList<CalendarInfo> &calendars, bool complete)
{
    // An agent that did not answer may just be starting.
    if (!complete && m_tries < Tries) {
        ++m_tries;
        m_again->start();
    }
    Q_EMIT found(calendars, complete);
    if (!m_testMode) {
        // What an agent that did not answer has is still as it was found before.
        if (complete) {
            m_agents = m_asked;
        } else {
            m_agents.insert(m_asked);
        }
        tidy(complete);
    }
}

void SystemCalendars::setAgentData(const QString &folder)
{
    m_agentData = folder;
}

void SystemCalendars::noteWritten(const QUrl &file)
{
    const QString path = QDir::cleanPath(file.toLocalFile());
    if (m_testMode || !file.isLocalFile()) {
        return;
    }
    QSettings settings;
    QStringList written = settings.value(WrittenKey).toStringList();
    if (!written.contains(path)) {
        written.append(path);
        settings.setValue(WrittenKey, written);
    }
    m_tidy->start();
}

void SystemCalendars::tidy(bool complete)
{
    QSettings settings;
    const QStringList written = settings.value(WrittenKey).toStringList();
    QStringList kept;
    for (const QString &path : written) {
        const QString agent = m_agents.value(path);
        if (!agent.isEmpty()) {
            prune(m_agentData + u'/' + agent, QFileInfo(path).fileName());
        }
        // A calendar that Akonadi no longer has gets no more copies.
        if (!agent.isEmpty() || !complete) {
            kept.append(path);
        }
    }
    if (kept.isEmpty() && !written.isEmpty()) {
        settings.remove(WrittenKey);
    } else if (kept != written) {
        settings.setValue(WrittenKey, kept);
    }
}

int SystemCalendars::prune(const QString &folder, const QString &name, int keep, int days)
{
    if (folder.isEmpty() || name.isEmpty()) {
        return 0;
    }
    // Only what is named like a copy of this very file, and is a file.
    const QRegularExpression copy(u'^' + QRegularExpression::escape(name) + u"-[0-9]+$"_s);
    const QDateTime limit = QDateTime::currentDateTime().addDays(-days);
    // The newest first. Akonadi numbers its copies, but takes a number
    // that is free again, so the numbers say nothing about their age.
    const QFileInfoList files = QDir(folder).entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Time);
    int copies = 0;
    int removed = 0;
    for (const QFileInfo &file : files) {
        if (!copy.match(file.fileName()).hasMatch()) {
            continue;
        }
        if (++copies > keep && file.lastModified() < limit && QFile::remove(file.absoluteFilePath())) {
            ++removed;
        }
    }
    return removed;
}

#include "moc_systemcalendars.cpp"
