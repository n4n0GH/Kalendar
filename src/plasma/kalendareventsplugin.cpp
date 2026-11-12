// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kalendareventsplugin.h"

#include "engine.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace
{
QString databasePath()
{
    // Tests point to a database of their own.
    const QString custom = qEnvironmentVariable("KALENDAR_DATABASE");
    if (!custom.isEmpty()) {
        return custom;
    }
    // Kalendar installed as a Flatpak keeps its data in a place of its own.
    // Of the two, the one that was used last is the one in use.
    const QString plain = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/kalendar/calendars.sqlite"_s;
    const QString flatpak = QDir::homePath() + u"/.var/app/io.github.n4n0gh.kalendar/data/kalendar/calendars.sqlite"_s;
    return QFileInfo(flatpak).lastModified() > QFileInfo(plain).lastModified() || !QFileInfo::exists(plain) ? flatpak : plain;
}
}

KalendarEventsPlugin::KalendarEventsPlugin(QObject *parent)
    : CalendarEvents::CalendarEventsPlugin(parent)
    , m_database(databasePath())
{
    // Kalendar writes in several steps; they are waited out and then read as one.
    m_settle.setSingleShot(true);
    m_settle.setInterval(500);
    connect(&m_settle, &QTimer::timeout, this, &KalendarEventsPlugin::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_settle, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    watch();
}

void KalendarEventsPlugin::watch()
{
    // The folder tells when the database appears; the files tell when it
    // changes. Changes go to the file ending in -wal first.
    const QStringList wanted{QFileInfo(m_database).absolutePath(), m_database, m_database + u"-wal"_s};
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    for (const QString &path : wanted) {
        if (!watched.contains(path) && QFileInfo::exists(path)) {
            m_watcher.addPath(path);
        }
    }
}

void KalendarEventsPlugin::loadEventsForDateRange(const QDate &startDate, const QDate &endDate)
{
    m_start = startDate;
    m_end = endDate;
    reload();
}

void KalendarEventsPlugin::reload()
{
    watch();
    if (!m_start.isValid() || !m_end.isValid()) {
        return;
    }
    // Take back what was shown; what is still there comes again below.
    for (const QString &uid : std::as_const(m_shown)) {
        Q_EMIT eventRemoved(uid);
    }
    m_shown.clear();

    Engine engine;
    if (!QFileInfo::exists(m_database) || !engine.openReadOnly(m_database)) {
        return; // Kalendar has not been used yet
    }
    QMultiHash<QDate, CalendarEvents::EventData> data;
    const QList<Occurrence> occurrences = engine.occurrences(m_start, m_end);
    for (const Occurrence &occurrence : occurrences) {
        const Collection *calendar = engine.collection(occurrence.collectionId);
        CalendarEvents::EventData event;
        event.setUid(occurrence.href + u'#' + QString::number(occurrence.start.toMSecsSinceEpoch()));
        event.setEventType(CalendarEvents::EventData::Event);
        event.setTitle(occurrence.event->summary());
        event.setDescription(occurrence.event->description());
        event.setLocation(occurrence.event->location());
        event.setIsAllDay(occurrence.allDay);
        event.setStartDateTime(occurrence.start);
        event.setEndDateTime(occurrence.end);
        if (calendar) {
            event.setEventColor(calendar->color.name());
        }
        m_shown.insert(event.uid());
        // An event that takes several days is listed on each of them.
        for (QDate day = std::max(occurrence.firstDay, m_start); day <= std::min(occurrence.lastDay, m_end); day = day.addDays(1)) {
            data.insert(day, event);
        }
    }
    if (!data.isEmpty()) {
        Q_EMIT dataReady(data);
    }
}
