// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <CalendarEvents/CalendarEventsPlugin>

#include <QDate>
#include <QFileSystemWatcher>
#include <QHash>
#include <QTimer>

#include <optional>

/// Shows Kalendar's events in the calendar of Plasma's clock.
///
/// It runs inside the Plasma shell, so it only ever reads: the events come
/// straight from Kalendar's database, which is read again whenever Kalendar
/// changes it. If the days that are shown differ then, their events are
/// taken back in one go and handed over anew. Calendars that are hidden in Kalendar are hidden here too.
class KalendarEventsPlugin : public CalendarEvents::CalendarEventsPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.CalendarEventsPlugin" FILE "kalendarevents.json")
    Q_INTERFACES(CalendarEvents::CalendarEventsPlugin)

public:
    explicit KalendarEventsPlugin(QObject *parent = nullptr);

    void loadEventsForDateRange(const QDate &startDate, const QDate &endDate) override;

private:
    /// An event as it was handed over, and the days it is listed on.
    struct Entry {
        CalendarEvents::EventData event;
        QDate firstDay;
        QDate lastDay;
    };
    using Shown = QHash<QString, Entry>;

    void watch();
    void reload();
    /// The events of the days Plasma asked for; nothing if the database is
    /// there but cannot be read.
    std::optional<Shown> read() const;
    void announce(const Shown &events);

    const QString m_database;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    QDate m_start;
    QDate m_end;
    /// The events that were handed over, each by a name of its own, to tell
    /// whether anything differs when the database changes.
    Shown m_shown;
};
