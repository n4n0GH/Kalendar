// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <CalendarEvents/CalendarEventsPlugin>

#include <QDate>
#include <QFileSystemWatcher>
#include <QSet>
#include <QTimer>

/// Shows Kalendar's events in the calendar of Plasma's clock.
///
/// It runs inside the Plasma shell, so it only ever reads: the events come
/// straight from Kalendar's database, which is read again whenever Kalendar
/// changes it. Calendars that are hidden in Kalendar are hidden here too.
class KalendarEventsPlugin : public CalendarEvents::CalendarEventsPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.CalendarEventsPlugin" FILE "kalendarevents.json")
    Q_INTERFACES(CalendarEvents::CalendarEventsPlugin)

public:
    explicit KalendarEventsPlugin(QObject *parent = nullptr);

    void loadEventsForDateRange(const QDate &startDate, const QDate &endDate) override;

private:
    void watch();
    void reload();

    const QString m_database;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    QDate m_start;
    QDate m_end;
    /// The events that were handed over, to take them back when they change.
    QSet<QString> m_shown;
};
