// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusMessage>
#include <QObject>
#include <QQmlEngine>

#include <functional>

class Engine;

/// Puts Kalendar's events among the calendars of the system: those of
/// Akonadi, which KOrganizer, Merkuro and the calendar of Plasma's clock
/// show.
///
/// The engine keeps the events as one calendar file. Akonadi shows a
/// calendar file through an agent of its own (akonadi_ical_resource) that
/// reads the file and follows its changes. Such an agent is made, told the
/// file and removed here by talking to Akonadi over D-Bus, which takes no
/// library of Akonadi's: where there is no Akonadi, there is nobody to talk
/// to, and this says that it is not available.
///
/// Whether the file is shown is found out by asking the agents for calendar
/// files which file they have, so it stays right when the calendar is
/// removed in another program. Akonadi is started only to add the calendar,
/// never to be asked or to be told to forget it: while it does not run, what
/// was found the last time is taken to be still so, and a calendar that is
/// to go is taken away the next time Akonadi is found running.
///
/// Nothing here waits for an answer; `changed` says when something is known.
class SystemExport : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// There is a system calendar to add to. It may have to be started first.
    Q_PROPERTY(bool available READ isAvailable NOTIFY changed)
    /// Kalendar's events are among its calendars, or are just being put there.
    Q_PROPERTY(bool shown READ isShown NOTIFY changed)
    /// They are being added or removed.
    Q_PROPERTY(bool busy READ isBusy NOTIFY changed)
    /// Why adding them did not work; empty if it did.
    Q_PROPERTY(QString problem READ problem NOTIFY changed)

public:
    /// @p file is where the calendar file is kept while it is wanted. With
    /// @p testMode there is no system to talk to.
    explicit SystemExport(Engine *engine, const QString &file, bool testMode, QObject *parent = nullptr);
    /// There is one, made in main().
    static SystemExport *create(QQmlEngine *, QJSEngine *);
    static void setInstance(SystemExport *instance);

    bool isAvailable() const;
    bool isShown() const;
    bool isBusy() const;
    QString problem() const;

    /// Finds out anew how things are.
    Q_INVOKABLE void refresh();
    /// Has the events shown as a calendar of the system. Starts Akonadi if
    /// it is not running.
    Q_INVOKABLE void add();
    /// Takes the calendar away again, and the file with it.
    Q_INVOKABLE void remove();

    /// Before this, a plugin that Kalendar installed for the user showed the
    /// events in the calendar of Plasma's clock. Removes what is left of it
    /// below @p home (the plugin) and @p config (the line that told Plasma
    /// at login where to find it), and gives whether there was anything.
    static bool retireClockPlugin(const QString &home, const QString &config);

Q_SIGNALS:
    void changed();

private:
    using Reply = std::function<void(const QDBusMessage &)>;

    void call(const QString &service, const QString &path, const QString &interface, const QString &method, const QVariantList &arguments, const Reply &then,
              int timeout = 5000);
    /// Whether Akonadi runs, and if not, whether it can be started.
    void look(const std::function<void(bool running, bool startable)> &then);
    /// The agent that shows the file, or nothing. Only to be asked while Akonadi runs.
    void find(const std::function<void(const QString &instance)> &then);
    void probe(const QStringList &instances, const std::function<void(const QString &instance)> &then);
    void create();
    /// Waits for the agent that was made to answer, and tells it the file.
    void configure(const QString &instance, int attempt);
    /// Adding did not work: nothing of it is left behind.
    void fail(const QString &problem);
    void finish(const QString &problem = {});
    void setBusy(bool busy);

    Engine *const m_engine;
    const QString m_file;
    const bool m_testMode;
    QString m_problem;
    bool m_available = false;
    bool m_shown = false;
    bool m_adding = false;
    int m_busy = 0;
    /// Goes up with every refresh: answers to an earlier one do not count.
    quint64 m_round = 0;
};
