// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "systemexport.h"

#include "engine.h"

#include <KLocalizedString>

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace
{
SystemExport *s_instance = nullptr;

/// Akonadi's kind of agent that shows a calendar file. Its agents are
/// called by it, with a number.
const QString FileAgent = u"akonadi_ical_resource"_s;
const QString ManagerPath = u"/AgentManager"_s;
const QString ManagerInterface = u"org.freedesktop.Akonadi.AgentManager"_s;
const QString SettingsPath = u"/Settings"_s;
const QString SettingsInterface = u"org.kde.Akonadi.ICal.Settings"_s;
const QString BusService = u"org.freedesktop.DBus"_s;
const QString BusPath = u"/org/freedesktop/DBus"_s;

/// How long an agent that was just made is waited for, in steps.
constexpr int Attempts = 80;
constexpr int AttemptMilliseconds = 250;
constexpr int AgentTimeout = 3000;
/// Akonadi may have to be started before it answers.
constexpr int StartTimeout = 60 * 1000;

/// What is kept from one start to the next.
const QString EnabledKey = u"SystemExport/enabled"_s; ///< the file is wanted
const QString ShownKey = u"SystemExport/shown"_s; ///< Akonadi showed it when it was last asked
const QString ForgetKey = u"SystemExport/forget"_s; ///< it is to go from Akonadi once that runs

bool flag(const QString &key)
{
    return QSettings().value(key, false).toBool();
}

void setFlag(const QString &key, bool on)
{
    QSettings settings;
    if (on) {
        settings.setValue(key, true);
    } else {
        settings.remove(key);
    }
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

QString agent(const QString &instance)
{
    return named(u"org.freedesktop.Akonadi.Agent."_s + instance);
}

bool failed(const QDBusMessage &reply)
{
    return reply.type() != QDBusMessage::ReplyMessage;
}

/// What the calendar is called in other programs.
QString calendarName()
{
    return i18nc("@title the name of the calendar with Kalendar’s events, as other programs show it", "Kalendar");
}
}

SystemExport::SystemExport(Engine *engine, const QString &file, bool testMode, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_file(QDir::cleanPath(file))
    , m_testMode(testMode)
{
    if (flag(EnabledKey)) {
        m_engine->setExportFile(m_file);
    }
    refresh();
}

SystemExport *SystemExport::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT(s_instance);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void SystemExport::setInstance(SystemExport *instance)
{
    s_instance = instance;
}

bool SystemExport::isAvailable() const
{
    return m_available;
}

bool SystemExport::isShown() const
{
    return m_shown || m_adding;
}

bool SystemExport::isBusy() const
{
    return m_busy > 0;
}

QString SystemExport::problem() const
{
    return m_problem;
}

void SystemExport::call(const QString &service,
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
        if (then) {
            then(watcher->reply());
        }
    });
}

void SystemExport::look(const std::function<void(bool, bool)> &then)
{
    if (m_testMode || !QDBusConnection::sessionBus().isConnected()) {
        // Later, as an answer comes.
        QTimer::singleShot(0, this, [then] {
            then(false, false);
        });
        return;
    }
    call(BusService, BusPath, BusService, u"NameHasOwner"_s, {control()}, [this, then](const QDBusMessage &reply) {
        if (!failed(reply) && reply.arguments().value(0).toBool()) {
            then(true, true);
            return;
        }
        call(BusService, BusPath, BusService, u"ListActivatableNames"_s, {}, [then](const QDBusMessage &reply) {
            then(false, !failed(reply) && reply.arguments().value(0).toStringList().contains(control()));
        });
    });
}

void SystemExport::find(const std::function<void(const QString &)> &then)
{
    call(
        control(),
        ManagerPath,
        ManagerInterface,
        u"agentInstances"_s,
        {},
        [this, then](const QDBusMessage &reply) {
            QStringList instances;
            if (!failed(reply)) {
                const QStringList all = reply.arguments().value(0).toStringList();
                for (const QString &instance : all) {
                    if (instance == FileAgent || instance.startsWith(FileAgent + u'_')) {
                        instances.append(instance);
                    }
                }
            }
            probe(instances, then);
        },
        StartTimeout);
}

void SystemExport::probe(const QStringList &instances, const std::function<void(const QString &)> &then)
{
    if (instances.isEmpty()) {
        then({});
        return;
    }
    const QString instance = instances.constFirst();
    call(
        resource(instance),
        SettingsPath,
        SettingsInterface,
        u"path"_s,
        {},
        [this, instances, instance, then](const QDBusMessage &reply) {
            if (!failed(reply) && QDir::cleanPath(reply.arguments().value(0).toString()) == m_file) {
                then(instance);
            } else {
                probe(instances.mid(1), then);
            }
        },
        AgentTimeout);
}

void SystemExport::refresh()
{
    const quint64 round = ++m_round;
    const auto apply = [this](bool available, bool shown) {
        if (available != m_available || shown != m_shown) {
            m_available = available;
            m_shown = shown;
            Q_EMIT changed();
        }
    };
    look([this, round, apply](bool running, bool startable) {
        if (round != m_round) {
            return;
        }
        if (!running) {
            // Not started to be asked: as it was when it was last asked.
            apply(startable, startable && flag(EnabledKey) && flag(ShownKey));
            return;
        }
        find([this, round, apply](const QString &instance) {
            if (round != m_round) {
                return;
            }
            // What was to go while Akonadi was not there to be told.
            if (flag(ForgetKey)) {
                setFlag(ForgetKey, false);
                if (!instance.isEmpty() && !flag(EnabledKey)) {
                    call(control(), ManagerPath, ManagerInterface, u"removeAgentInstance"_s, {instance}, {});
                    setFlag(ShownKey, false);
                    apply(true, false);
                    return;
                }
            }
            setFlag(ShownKey, !instance.isEmpty());
            apply(true, !instance.isEmpty());
        });
    });
}

void SystemExport::add()
{
    if (isBusy()) {
        return;
    }
    m_problem.clear();
    m_adding = true;
    setBusy(true);
    setFlag(EnabledKey, true);
    setFlag(ForgetKey, false);
    // The file first: an agent is pointed at something that is there.
    if (!m_engine->setExportFile(m_file)) {
        fail(i18nc("@info %1 is a file", "The calendar file “%1” could not be written.", m_file));
        return;
    }
    look([this](bool running, bool startable) {
        if (!running && !startable) {
            fail(i18nc("@info Akonadi is the name of the service that keeps the calendars of KDE’s other programs",
                       "There is no Akonadi on this system to hand the events to."));
            return;
        }
        // Asking for its agents is what starts Akonadi if it is not running.
        find([this](const QString &instance) {
            if (instance.isEmpty()) {
                create();
            } else {
                configure(instance, 0);
            }
        });
    });
}

void SystemExport::create()
{
    call(
        control(),
        ManagerPath,
        ManagerInterface,
        u"createAgentInstance"_s,
        {FileAgent},
        [this](const QDBusMessage &reply) {
            const QString instance = failed(reply) ? QString() : reply.arguments().value(0).toString();
            if (instance.isEmpty()) {
                fail(failed(reply) ? i18nc("@info %1 is what the system said", "Akonadi could not be reached: %1", reply.errorMessage())
                                   : i18nc("@info", "Akonadi cannot show calendar files: what it needs for that is not installed."));
                return;
            }
            configure(instance, 0);
        },
        StartTimeout);
}

void SystemExport::configure(const QString &instance, int attempt)
{
    call(
        resource(instance),
        SettingsPath,
        SettingsInterface,
        u"setPath"_s,
        {m_file},
        [this, instance, attempt](const QDBusMessage &reply) {
            if (failed(reply)) {
                // An agent that was just made takes a moment until it can be talked to.
                if (attempt < Attempts) {
                    QTimer::singleShot(AttemptMilliseconds, this, [this, instance, attempt] {
                        configure(instance, attempt + 1);
                    });
                    return;
                }
                // Not to be left behind as a calendar of nothing.
                call(control(), ManagerPath, ManagerInterface, u"removeAgentInstance"_s, {instance}, {});
                fail(i18nc("@info %1 is what the system said", "Akonadi did not take the calendar file: %1", reply.errorMessage()));
                return;
            }
            // The file is Kalendar's to write: other programs are to show
            // it, and to look at it again whenever it changes, but not to
            // write into it.
            const QString service = resource(instance);
            const QString name = calendarName();
            call(service, SettingsPath, SettingsInterface, u"setDisplayName"_s, {name}, {});
            call(service, SettingsPath, SettingsInterface, u"setReadOnly"_s, {true}, {});
            call(service, SettingsPath, SettingsInterface, u"setMonitorFile"_s, {true}, {});
            call(service, SettingsPath, SettingsInterface, u"save"_s, {}, [this, instance, name](const QDBusMessage &) {
                call(agent(instance), u"/"_s, u"org.freedesktop.Akonadi.Agent.Control"_s, u"reconfigure"_s, {}, [this, instance, name](const QDBusMessage &) {
                    call(control(), ManagerPath, ManagerInterface, u"setAgentInstanceName"_s, {instance, name}, [this, instance](const QDBusMessage &) {
                        call(control(), ManagerPath, ManagerInterface, u"agentInstanceSynchronize"_s, {instance}, {});
                        finish();
                    });
                });
            });
        },
        AgentTimeout);
}

void SystemExport::remove()
{
    if (isBusy()) {
        return;
    }
    setBusy(true);
    setFlag(EnabledKey, false);
    m_engine->setExportFile({});
    const auto done = [this] {
        QFile::remove(m_file);
        setBusy(false);
        refresh();
    };
    look([this, done](bool running, bool startable) {
        if (!running) {
            // Not started to be told: it is told the next time it runs.
            setFlag(ForgetKey, startable);
            setFlag(ShownKey, false);
            done();
            return;
        }
        find([this, done](const QString &instance) {
            if (instance.isEmpty()) {
                done();
                return;
            }
            call(control(), ManagerPath, ManagerInterface, u"removeAgentInstance"_s, {instance}, [done](const QDBusMessage &) {
                done();
            });
        });
    });
}

void SystemExport::fail(const QString &problem)
{
    setFlag(EnabledKey, false);
    m_engine->setExportFile({});
    QFile::remove(m_file);
    finish(problem);
}

void SystemExport::finish(const QString &problem)
{
    m_problem = problem;
    m_adding = false;
    setBusy(false);
    refresh();
}

void SystemExport::setBusy(bool busy)
{
    m_busy = std::max(0, m_busy + (busy ? 1 : -1));
    Q_EMIT changed();
}

bool SystemExport::retireClockPlugin(const QString &home, const QString &config)
{
    const QString plugin = home + u"/.local/lib/kalendar/plugins/plasmacalendarplugins/kalendarevents.so"_s;
    const QString script = config + u"/plasma-workspace/env/kalendar-clock-plugin.sh"_s;
    bool found = false;
    for (const QString &path : {plugin, script}) {
        found = QFile::remove(path) || found;
    }
    // The folders made for it, as far as nothing else is in them.
    QDir().rmpath(QFileInfo(plugin).path());
    return found;
}

#include "moc_systemexport.cpp"
