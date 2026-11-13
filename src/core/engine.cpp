// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine.h"

#include "googleclient.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/OccurrenceIterator>
#include <KLocalizedString>

#include <QBitArray>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QTimeZone>
#include <QTimer>
#include <QUuid>

#include <algorithm>

using namespace Qt::StringLiterals;
using KCalendarCore::Event;

namespace
{
const QString LocalAccountId = u"local"_s;

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool isBar(const Occurrence &occurrence)
{
    return occurrence.allDay || occurrence.firstDay != occurrence.lastDay;
}

Event::Ptr masterOf(const Event::List &events)
{
    for (const Event::Ptr &event : events) {
        if (!event->hasRecurrenceId()) {
            return event;
        }
    }
    return {};
}

/// How an event repeats, as one of the choices offered when editing.
QString repeatKind(const Event::Ptr &event)
{
    if (!event->recurs()) {
        return u"none"_s;
    }
    const KCalendarCore::Recurrence *recurrence = event->recurrence();
    const bool plain = recurrence->rRules().size() == 1 && recurrence->frequency() == 1 && recurrence->duration() == -1;
    if (plain) {
        switch (recurrence->recurrenceType()) {
        case KCalendarCore::Recurrence::rDaily:
            return u"daily"_s;
        case KCalendarCore::Recurrence::rWeekly: {
            // "Every week" means: on the weekday it starts on.
            const QBitArray days = recurrence->days();
            const int weekday = event->dtStart().date().dayOfWeek();
            if (days.count(true) == 0 || (days.count(true) == 1 && days.testBit(weekday - 1))) {
                return u"weekly"_s;
            }
            break;
        }
        case KCalendarCore::Recurrence::rMonthlyDay:
            if (recurrence->monthDays().size() <= 1) {
                return u"monthly"_s;
            }
            break;
        case KCalendarCore::Recurrence::rYearlyMonth:
            if (recurrence->yearMonths().size() <= 1 && recurrence->yearDates().size() <= 1) {
                return u"yearly"_s;
            }
            break;
        default:
            break;
        }
    }
    return u"custom"_s;
}
}

Engine::Engine(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

Engine::~Engine() = default;

QString Engine::errorString() const
{
    return m_db.errorString();
}

void Engine::setAuthProvider(const AuthProvider &provider)
{
    m_auth = provider;
}

void Engine::setUndoPeriod(int milliseconds)
{
    m_undoPeriod = milliseconds;
}

QColor Engine::defaultColor(int index)
{
    // Calendar colours are data, like the ones servers send; they are not
    // part of the colour scheme.
    static const QList<QColor> palette{
        QColor(0x3d, 0xae, 0xe9),
        QColor(0x27, 0xae, 0x60),
        QColor(0xf6, 0x74, 0x00),
        QColor(0x9b, 0x59, 0xb6),
        QColor(0xda, 0x44, 0x53),
        QColor(0x16, 0xa0, 0x85),
        QColor(0xc9, 0xa0, 0x00),
        QColor(0x7f, 0x8c, 0x8d),
    };
    return palette.at(index % palette.size());
}

void Engine::nameLocalAccount()
{
    // Named anew at every start, so that the name follows the language.
    for (Account &account : m_accounts) {
        if (account.type == Account::Local) {
            account.name = i18nc("@title the account for calendars kept on this system only", "Local Calendars");
        }
    }
}

bool Engine::open(const QString &path)
{
    if (!m_db.open(path)) {
        return false;
    }
    m_accounts = m_db.accounts();
    nameLocalAccount();
    m_collections = m_db.collections();
    if (!account(LocalAccountId)) {
        Account local;
        local.id = LocalAccountId;
        local.type = Account::Local;
        m_accounts.prepend(local);
        nameLocalAccount();
        m_db.saveAccount(local);

        Collection personal;
        personal.id = newId();
        personal.accountId = LocalAccountId;
        personal.url = QUrl(u"local:personal"_s);
        personal.name = i18nc("@title name of the calendar that exists from the start", "Personal");
        personal.color = defaultColor(0);
        m_collections.prepend(personal);
        m_db.saveCollection(personal);
    }
    load();
    // What was deleted on this system just before the last quit is cleared
    // away now; for servers, the first synchronization does the same.
    for (const Collection &collection : std::as_const(m_collections)) {
        if (collection.accountId == LocalAccountId) {
            push(collection.id);
        }
    }
    return true;
}

bool Engine::openReadOnly(const QString &path)
{
    m_readOnly = true;
    if (!m_db.open(path, true)) {
        return false;
    }
    m_accounts = m_db.accounts();
    nameLocalAccount();
    m_collections = m_db.collections();
    load();
    return true;
}

void Engine::load()
{
    for (const Collection &collection : std::as_const(m_collections)) {
        State &state = m_state[collection.id];
        state.calendar = KCalendarCore::MemoryCalendar::Ptr::create(QTimeZone::systemTimeZone());
        const QList<ItemRecord> items = m_db.items(collection.id);
        for (const ItemRecord &item : items) {
            setItem(collection.id, item.href, item.etag, item.ics, item.dirty, false);
        }
    }
}

const QList<Account> &Engine::accounts() const
{
    return m_accounts;
}

const QList<Collection> &Engine::collections() const
{
    return m_collections;
}

const Account *Engine::account(const QString &id) const
{
    const auto it = std::find_if(m_accounts.cbegin(), m_accounts.cend(), [&](const Account &account) {
        return account.id == id;
    });
    return it == m_accounts.cend() ? nullptr : &*it;
}

const Collection *Engine::collection(const QString &id) const
{
    return const_cast<Engine *>(this)->findCollection(id);
}

Collection *Engine::findCollection(const QString &id)
{
    const auto it = std::find_if(m_collections.begin(), m_collections.end(), [&](const Collection &collection) {
        return collection.id == id;
    });
    return it == m_collections.end() ? nullptr : &*it;
}

QString Engine::accountError(const QString &accountId) const
{
    return m_errors.value(accountId);
}

void Engine::setAccountError(const QString &accountId, const QString &error)
{
    if (m_errors.value(accountId) == error) {
        return;
    }
    if (error.isEmpty()) {
        m_errors.remove(accountId);
    } else {
        m_errors.insert(accountId, error);
    }
    Q_EMIT errorsChanged();
}

CalendarClient *Engine::clientFor(const Account &account, const QByteArray &authorization)
{
    CalendarClient *&client = m_clients[account.id];
    if (!client && account.type == Account::Google) {
        client = new GoogleClient(m_network, this);
    } else if (!client) {
        client = new DavClient(m_network, this);
    }
    client->setAuthorization(authorization);
    return client;
}

// Accounts and calendars

void Engine::discover(const QUrl &address, const QByteArray &authorization, const std::function<void(const Discovery &)> &done)
{
    auto *client = new DavClient(m_network, this);
    client->setAuthorization(authorization);
    client->discover(address, [client, done](const Discovery &discovery) {
        client->deleteLater();
        done(discovery);
    });
}

void Engine::discoverGoogle(const QUrl &api, const QByteArray &authorization, const std::function<void(const Discovery &)> &done)
{
    auto *client = new GoogleClient(m_network, this);
    client->setAuthorization(authorization);
    client->listCalendars(api, [client, api, done](const DavReply &reply, const QList<CalendarInfo> &calendars) {
        client->deleteLater();
        Discovery discovery;
        discovery.home = api;
        discovery.calendars = calendars;
        if (!reply.ok) {
            discovery.error = reply.error;
        } else if (calendars.isEmpty()) {
            discovery.error = i18nc("@info", "The account has no calendars.");
        }
        done(discovery);
    });
}

QString Engine::addAccount(Account account, const QList<CalendarInfo> &calendars)
{
    if (account.id.isEmpty()) {
        account.id = newId();
    }
    m_accounts.append(account);
    m_db.saveAccount(account);
    reconcile(account.id, calendars);
    Q_EMIT accountsChanged();
    syncAccount(account.id);
    return account.id;
}

void Engine::removeAccount(const QString &id)
{
    const Account *found = account(id);
    if (m_readOnly || !found || found->type == Account::Local) {
        return;
    }
    for (const Collection &collection : std::as_const(m_collections)) {
        if (collection.accountId == id) {
            m_state.remove(collection.id);
        }
    }
    m_collections.removeIf([&](const Collection &collection) {
        return collection.accountId == id;
    });
    m_accounts.removeIf([&](const Account &account) {
        return account.id == id;
    });
    m_db.removeAccount(id);
    if (CalendarClient *client = m_clients.take(id)) {
        client->deleteLater();
    }
    m_errors.remove(id);
    if (m_syncing.remove(id)) {
        Q_EMIT syncingChanged();
    }
    Q_EMIT accountsChanged();
    Q_EMIT collectionsChanged();
    Q_EMIT errorsChanged();
    Q_EMIT eventsChanged();
}

void Engine::setCollectionEnabled(const QString &id, bool enabled)
{
    Collection *found = findCollection(id);
    if (m_readOnly || !found || found->enabled == enabled) {
        return;
    }
    found->enabled = enabled;
    m_db.saveCollection(*found);
    Q_EMIT collectionsChanged();
    Q_EMIT eventsChanged();
}

void Engine::reconcile(const QString &accountId, const QList<CalendarInfo> &calendars)
{
    bool changed = false;
    int count = 0;
    for (const Collection &collection : std::as_const(m_collections)) {
        count += collection.accountId == accountId;
    }
    for (const CalendarInfo &info : calendars) {
        const auto it = std::find_if(m_collections.begin(), m_collections.end(), [&](const Collection &collection) {
            return collection.accountId == accountId && collection.url == info.url;
        });
        if (it == m_collections.end()) {
            Collection collection;
            collection.id = newId();
            collection.accountId = accountId;
            collection.url = info.url;
            collection.name = info.name;
            // Colours start after the one of the calendar on this system.
            collection.color = info.color.isValid() ? info.color : defaultColor(++count);
            collection.readOnly = info.readOnly;
            m_collections.append(collection);
            m_db.saveCollection(collection);
            State &state = m_state[collection.id];
            state.calendar = KCalendarCore::MemoryCalendar::Ptr::create(QTimeZone::systemTimeZone());
                changed = true;
        } else if (it->name != info.name || it->readOnly != info.readOnly || (info.color.isValid() && it->color != info.color)) {
            it->name = info.name;
            it->readOnly = info.readOnly;
            if (info.color.isValid()) {
                it->color = info.color;
            }
            m_db.saveCollection(*it);
            changed = true;
        }
    }
    // Calendars that are gone from the server go here too.
    QStringList gone;
    for (const Collection &collection : std::as_const(m_collections)) {
        const bool listed = std::any_of(calendars.cbegin(), calendars.cend(), [&](const CalendarInfo &info) {
            return info.url == collection.url;
        });
        if (collection.accountId == accountId && !listed) {
            gone.append(collection.id);
        }
    }
    for (const QString &id : std::as_const(gone)) {
        m_state.remove(id);
        m_db.removeCollection(id);
        m_collections.removeIf([&](const Collection &collection) {
            return collection.id == id;
        });
        changed = true;
    }
    if (changed) {
        Q_EMIT collectionsChanged();
        Q_EMIT eventsChanged();
    }
}

// Events in memory

Event::List Engine::parse(const QByteArray &ics)
{
    const auto calendar = KCalendarCore::MemoryCalendar::Ptr::create(QTimeZone::systemTimeZone());
    KCalendarCore::ICalFormat format;
    if (!format.fromRawString(calendar, ics)) {
        return {};
    }
    Event::List events;
    const Event::List parsed = calendar->rawEvents();
    for (const Event::Ptr &event : parsed) {
        events.append(Event::Ptr(event->clone()));
    }
    return events;
}

QByteArray Engine::serialize(const Event::List &events)
{
    const auto calendar = KCalendarCore::MemoryCalendar::Ptr::create(QTimeZone::systemTimeZone());
    // The event itself first, then the exceptions to its repetition.
    for (const bool exceptions : {false, true}) {
        for (const Event::Ptr &event : events) {
            if (event->hasRecurrenceId() == exceptions) {
                calendar->addIncidence(Event::Ptr(event->clone()));
            }
        }
    }
    return KCalendarCore::ICalFormat().toString(calendar.staticCast<KCalendarCore::Calendar>()).toUtf8();
}

void Engine::setItem(const QString &collectionId, const QString &href, const QString &etag, const QByteArray &ics, int dirty, bool store)
{
    State &state = m_state[collectionId];
    Item &item = state.items[href];
    for (const bool exceptions : {true, false}) {
        for (const Event::Ptr &event : std::as_const(item.events)) {
            if (item.shown && event->hasRecurrenceId() == exceptions) {
                state.calendar->deleteIncidence(event);
            }
        }
    }
    for (const Event::Ptr &event : std::as_const(item.events)) {
        state.hrefByUid.remove(event->uid());
    }
    item.shown = dirty != ItemRecord::Deleted;
    item.etag = etag;
    if (item.ics != ics || item.events.isEmpty()) {
        item.ics = ics;
        item.events = parse(ics);
    }
    item.dirty = dirty;
    for (const bool exceptions : {false, true}) {
        for (const Event::Ptr &event : std::as_const(item.events)) {
            if (item.shown && event->hasRecurrenceId() == exceptions) {
                state.calendar->addIncidence(event);
            }
        }
    }
    for (const Event::Ptr &event : std::as_const(item.events)) {
        state.hrefByUid.insert(event->uid(), href);
        if (event->recurs() && event->dtEnd().isValid()) {
            state.longestRepeating = std::max<qint64>(state.longestRepeating, event->dtStart().daysTo(event->dtEnd()) + 1);
        }
    }
    if (store) {
        m_db.saveItem({collectionId, href, etag, ics, dirty});
    }
}

void Engine::dropItem(const QString &collectionId, const QString &href)
{
    State &state = m_state[collectionId];
    const Item item = state.items.take(href);
    for (const bool exceptions : {true, false}) {
        for (const Event::Ptr &event : item.events) {
            if (item.shown && event->hasRecurrenceId() == exceptions) {
                state.calendar->deleteIncidence(event);
            }
        }
    }
    for (const Event::Ptr &event : item.events) {
        state.hrefByUid.remove(event->uid());
    }
    state.held.remove(href);
    m_db.removeItem(collectionId, href);
}

QList<Occurrence> Engine::occurrences(const QDate &from, const QDate &to) const
{
    QList<Occurrence> result;
    const QDateTime rangeStart(from, QTime(0, 0));
    const QDateTime rangeEnd(to, QTime(23, 59, 59, 999));
    for (const Collection &collection : m_collections) {
        if (!collection.enabled) {
            continue;
        }
        const auto state = m_state.constFind(collection.id);
        if (state == m_state.cend()) {
            continue;
        }
        // Appearances of repeating events are only found by their start, so the
        // search begins early enough for the longest of them to reach into the range.
        KCalendarCore::OccurrenceIterator it(*state->calendar, rangeStart.addDays(-state->longestRepeating), rangeEnd);
        while (it.hasNext()) {
            it.next();
            const Event::Ptr event = it.incidence().dynamicCast<Event>();
            if (!event) {
                continue;
            }
            Occurrence occurrence;
            occurrence.event = event;
            occurrence.collectionId = collection.id;
            occurrence.href = state->hrefByUid.value(event->uid());
            occurrence.allDay = event->allDay();
            const QDateTime start = it.occurrenceStartDate();
            QDateTime end = it.occurrenceEndDate();
            if (!end.isValid() || end < start) {
                end = start;
            }
            if (occurrence.allDay) {
                occurrence.start = QDateTime(start.date(), QTime(0, 0));
                occurrence.end = QDateTime(end.date(), QTime(0, 0));
                occurrence.firstDay = start.date();
                occurrence.lastDay = end.date();
            } else {
                occurrence.start = start.toLocalTime();
                occurrence.end = end.toLocalTime();
                occurrence.firstDay = occurrence.start.date();
                // An event that ends at midnight does not take place on the day that starts then.
                const bool endsAtMidnight = occurrence.end > occurrence.start && occurrence.end.time() == QTime(0, 0);
                occurrence.lastDay = endsAtMidnight ? occurrence.end.date().addDays(-1) : occurrence.end.date();
            }
            if (occurrence.lastDay < from || occurrence.firstDay > to) {
                continue;
            }
            result.append(occurrence);
        }
    }
    return result;
}

QVariantMap Engine::toVariant(const Occurrence &occurrence) const
{
    const Event::Ptr &event = occurrence.event;
    const Collection *found = collection(occurrence.collectionId);
    const Event::Ptr master = event->hasRecurrenceId() ? masterOf(m_state.value(occurrence.collectionId).items.value(occurrence.href).events) : event;
    const QString summary = event->summary().trimmed();
    return {
        {u"key"_s, QString(occurrence.href + u'#' + QString::number(occurrence.start.toMSecsSinceEpoch()))},
        {u"collectionId"_s, occurrence.collectionId},
        {u"href"_s, occurrence.href},
        {u"summary"_s, summary},
        {u"title"_s, summary.isEmpty() ? i18nc("@info shown in place of the title of an event that has none", "No Title") : summary},
        {u"location"_s, event->location()},
        {u"description"_s, event->description()},
        {u"start"_s, occurrence.start},
        // For an event that takes whole days: its last day.
        {u"end"_s, occurrence.end},
        {u"allDay"_s, occurrence.allDay},
        {u"color"_s, found ? found->color : QColor()},
        {u"calendarName"_s, found ? found->name : QString()},
        {u"accountName"_s, found && account(found->accountId) ? account(found->accountId)->name : QString()},
        {u"readOnly"_s, found && found->readOnly},
        {u"repeats"_s, event->recurs() || event->hasRecurrenceId()},
        {u"repeat"_s, event->hasRecurrenceId() ? u"exception"_s : repeatKind(event)},
        // Which appearance of a repeating event this is.
        {u"recurrenceId"_s, event->hasRecurrenceId() ? event->recurrenceId() : QDateTime()},
        {u"slot"_s, event->hasRecurrenceId() ? event->recurrenceId() : occurrence.start},
        {u"seriesRepeats"_s, master && master->recurs()},
    };
}

QVariantMap Engine::weekLayout(const QDate &start, int days, bool barsOnly, int maxLanes) const
{
    const QDate end = start.addDays(days - 1);
    QList<Occurrence> list = occurrences(start, end);
    if (barsOnly) {
        list.removeIf([](const Occurrence &occurrence) {
            return !isBar(occurrence);
        });
    }
    // Long events first, so that they get the upper rows and stay in one row across the week.
    std::stable_sort(list.begin(), list.end(), [](const Occurrence &a, const Occurrence &b) {
        if (isBar(a) != isBar(b)) {
            return isBar(a);
        }
        if (isBar(a)) {
            if (a.firstDay != b.firstDay) {
                return a.firstDay < b.firstDay;
            }
            if (a.lastDay != b.lastDay) {
                return a.lastDay > b.lastDay;
            }
        } else if (a.start != b.start) {
            return a.start < b.start;
        }
        return a.event->summary().localeAwareCompare(b.event->summary()) < 0;
    });

    struct Segment {
        int first;
        int last;
        int lane;
    };
    QList<Segment> segments;
    QList<QList<bool>> taken; // per lane, per day
    QList<int> lanesOfDay(days, 0);
    for (const Occurrence &occurrence : std::as_const(list)) {
        Segment segment;
        segment.first = std::max<qint64>(0, start.daysTo(occurrence.firstDay));
        segment.last = std::min<qint64>(days - 1, start.daysTo(occurrence.lastDay));
        segment.lane = 0;
        const auto fits = [&](int lane) {
            for (int day = segment.first; day <= segment.last; ++day) {
                if (taken.at(lane).at(day)) {
                    return false;
                }
            }
            return true;
        };
        while (segment.lane < taken.size() && !fits(segment.lane)) {
            ++segment.lane;
        }
        if (segment.lane == taken.size()) {
            taken.append(QList<bool>(days, false));
        }
        for (int day = segment.first; day <= segment.last; ++day) {
            taken[segment.lane][day] = true;
            lanesOfDay[day] = std::max(lanesOfDay.at(day), segment.lane + 1);
        }
        segments.append(segment);
    }

    QVariantList shown;
    QList<int> more(days, 0);
    for (int i = 0; i < segments.size(); ++i) {
        const Segment &segment = segments.at(i);
        bool visible = true;
        if (maxLanes > 0 && segment.lane >= maxLanes - 1) {
            // The last row is needed for "more" on any day that has too many.
            visible = segment.lane == maxLanes - 1;
            for (int day = segment.first; visible && day <= segment.last; ++day) {
                visible = lanesOfDay.at(day) <= maxLanes;
            }
        }
        if (!visible) {
            for (int day = segment.first; day <= segment.last; ++day) {
                ++more[day];
            }
            continue;
        }
        const Occurrence &occurrence = list.at(i);
        QVariantMap map = toVariant(occurrence);
        map.insert(u"column"_s, segment.first);
        map.insert(u"span"_s, segment.last - segment.first + 1);
        map.insert(u"lane"_s, segment.lane);
        map.insert(u"bar"_s, isBar(occurrence));
        map.insert(u"continuesBefore"_s, occurrence.firstDay < start);
        map.insert(u"continuesAfter"_s, occurrence.lastDay > end);
        shown.append(map);
    }
    QVariantList moreList;
    for (const int count : std::as_const(more)) {
        moreList.append(count);
    }
    const int lanes = maxLanes > 0 ? std::min<int>(maxLanes, taken.size()) : taken.size();
    return {{u"segments"_s, shown}, {u"more"_s, moreList}, {u"lanes"_s, lanes}};
}

QVariantList Engine::dayLayout(const QDate &day) const
{
    QList<Occurrence> list = occurrences(day, day);
    list.removeIf(isBar);
    std::stable_sort(list.begin(), list.end(), [](const Occurrence &a, const Occurrence &b) {
        return a.start != b.start ? a.start < b.start : a.end > b.end;
    });

    struct Block {
        int start;
        int end;
        int column = 0;
        int columns = 1;
    };
    // Short events are drawn at least this long, so they must be kept apart as if they were.
    constexpr int MinimumMinutes = 30;
    const QDateTime midnight(day, QTime(0, 0));
    QList<Block> blocks;
    QList<int> columnEnds;
    int groupStart = 0;
    int groupEnd = 0;
    const auto closeGroup = [&](int upTo) {
        for (int i = groupStart; i < upTo; ++i) {
            blocks[i].columns = columnEnds.size();
        }
        columnEnds.clear();
        groupStart = upTo;
        groupEnd = 0;
    };
    for (const Occurrence &occurrence : std::as_const(list)) {
        Block block;
        block.start = std::clamp<qint64>(midnight.secsTo(occurrence.start) / 60, 0, 24 * 60 - MinimumMinutes);
        block.end = std::clamp<qint64>(midnight.secsTo(occurrence.end) / 60, block.start + MinimumMinutes, 24 * 60);
        if (!columnEnds.isEmpty() && block.start >= groupEnd) {
            closeGroup(blocks.size());
        }
        while (block.column < columnEnds.size() && columnEnds.at(block.column) > block.start) {
            ++block.column;
        }
        if (block.column == columnEnds.size()) {
            columnEnds.append(block.end);
        } else {
            columnEnds[block.column] = block.end;
        }
        groupEnd = std::max(groupEnd, block.end);
        blocks.append(block);
    }
    closeGroup(blocks.size());

    QVariantList result;
    for (int i = 0; i < list.size(); ++i) {
        QVariantMap map = toVariant(list.at(i));
        map.insert(u"startMinute"_s, blocks.at(i).start);
        map.insert(u"endMinute"_s, blocks.at(i).end);
        map.insert(u"column"_s, blocks.at(i).column);
        map.insert(u"columns"_s, blocks.at(i).columns);
        map.insert(u"bar"_s, false);
        result.append(map);
    }
    return result;
}

// Changing events

/// The fields of @p data:
///   collectionId          the calendar to keep the event in
///   originalCollectionId  the calendar it is in now; empty for a new event
///   href                  the event's file; empty for a new event
///   recurrenceId          set to change one exception to a repetition
///   slot                  the start of the appearance that is being edited,
///                         as it was; a repeating event is moved by as much
///                         as this appearance was moved
///   summary, location, description
///   allDay, start, end    for whole days, end is the last day
///   repeat                none, daily, weekly, monthly, yearly, or custom
///                         to leave the repetition as it is
QString Engine::saveEvent(const QVariantMap &data)
{
    if (m_readOnly) {
        return i18nc("@info", "The calendars can only be looked at here.");
    }
    const Collection *target = collection(data.value(u"collectionId"_s).toString());
    if (!target) {
        return i18nc("@info", "The calendar no longer exists. Choose another one.");
    }
    if (target->readOnly) {
        return i18nc("@info", "The calendar “%1” cannot be changed.", target->name);
    }
    const QDateTime start = data.value(u"start"_s).toDateTime();
    const QDateTime end = data.value(u"end"_s).toDateTime();
    const bool allDay = data.value(u"allDay"_s).toBool();
    if (!start.isValid() || !end.isValid() || (allDay ? end.date() < start.date() : end < start)) {
        return i18nc("@info", "The event ends before it starts.");
    }

    QString href = data.value(u"href"_s).toString();
    const QString originalCollectionId = data.value(u"originalCollectionId"_s).toString();
    Event::List events;
    Event::Ptr event;
    QString etag;
    if (!href.isEmpty() && m_state.value(originalCollectionId).items.contains(href)) {
        const Item item = m_state.value(originalCollectionId).items.value(href);
        etag = item.etag;
        const QDateTime recurrenceId = data.value(u"recurrenceId"_s).toDateTime();
        for (const Event::Ptr &existing : item.events) {
            const Event::Ptr copy(existing->clone());
            events.append(copy);
            if (recurrenceId.isValid() ? (copy->hasRecurrenceId() && copy->recurrenceId() == recurrenceId) : !copy->hasRecurrenceId()) {
                event = copy;
            }
        }
    } else {
        href.clear();
    }
    if (!event) {
        events.clear();
        href.clear();
        etag.clear();
        event = Event::Ptr::create();
        event->setUid(newId());
        event->setCreated(QDateTime::currentDateTimeUtc());
        events.append(event);
    }

    event->setSummary(data.value(u"summary"_s).toString().trimmed());
    event->setLocation(data.value(u"location"_s).toString().trimmed());
    event->setDescription(data.value(u"description"_s).toString());

    // What is edited is one appearance; a repeating event as a whole moves along with it.
    const QDateTime slot = data.value(u"slot"_s).toDateTime();
    const bool series = event->recurs() && !event->hasRecurrenceId() && slot.isValid();
    const QDate oldFirstDay = event->allDay() ? event->dtStart().date() : event->dtStart().toLocalTime().date();
    const QDate slotDay = event->allDay() ? slot.date() : slot.toLocalTime().date();
    if (allDay) {
        QDate first = start.date();
        const qint64 length = start.date().daysTo(end.date());
        if (series) {
            first = oldFirstDay.addDays(slotDay.daysTo(start.date()));
        }
        event->setAllDay(true);
        event->setDtStart(QDateTime(first, QTime(0, 0)));
        event->setDtEnd(QDateTime(first.addDays(length), QTime(0, 0)));
    } else {
        QDateTime first = start.toTimeZone(QTimeZone::systemTimeZone());
        const qint64 length = start.secsTo(end);
        if (series && !event->allDay()) {
            first = event->dtStart().addSecs(slot.secsTo(start));
        } else if (series) {
            first = QDateTime(oldFirstDay.addDays(slotDay.daysTo(start.date())), start.time(), QTimeZone::systemTimeZone());
        }
        event->setAllDay(false);
        event->setDtStart(first);
        event->setDtEnd(first.addSecs(length));
    }

    const QString repeat = data.value(u"repeat"_s).toString();
    if (!event->hasRecurrenceId() && repeat != "custom"_L1 && repeat != repeatKind(event)) {
        event->recurrence()->clear();
        if (repeat == "daily"_L1) {
            event->recurrence()->setDaily(1);
        } else if (repeat == "weekly"_L1) {
            event->recurrence()->setWeekly(1);
        } else if (repeat == "monthly"_L1) {
            event->recurrence()->setMonthly(1);
        } else if (repeat == "yearly"_L1) {
            event->recurrence()->setYearly(1);
        }
    }
    event->setLastModified(QDateTime::currentDateTimeUtc());
    event->setRevision(event->revision() + 1);

    if (!href.isEmpty() && originalCollectionId != target->id) {
        // Moved to another calendar: gone from the one, new in the other.
        const Item old = m_state.value(originalCollectionId).items.value(href);
        setItem(originalCollectionId, href, old.etag, old.ics, ItemRecord::Deleted);
        push(originalCollectionId);
        href.clear();
        etag.clear();
    }
    if (href.isEmpty()) {
        href = newHref(*target, event->uid());
    }
    setItem(target->id, href, etag, serialize(events), ItemRecord::Changed);
    Q_EMIT eventsChanged();
    push(target->id);
    return {};
}

QString Engine::newHref(const Collection &target, const QString &uid) const
{
    switch (account(target.accountId)->type) {
    case Account::Local:
        return u"local:"_s + uid;
    case Account::CalDav: {
        // An event made elsewhere may be called anything; only a name of
        // letters, digits and the like is sure to make an address.
        static const QRegularExpression plain(u"^[A-Za-z0-9._@-]{1,200}$"_s);
        return DavClient::absolute(target.url, (plain.match(uid).hasMatch() ? uid : newId()) + u".ics"_s);
    }
    case Account::Google:
        return GoogleClient::hrefFor(target.url, uid);
    }
    return {};
}

// Calendar files from elsewhere

namespace
{
/// The events of a calendar file, those that belong together (an event and
/// the exceptions to its repetition share a UID) side by side.
QList<Event::List> eventsByUid(const QByteArray &ics, bool *valid = nullptr, int *others = nullptr)
{
    const auto calendar = KCalendarCore::MemoryCalendar::Ptr::create(QTimeZone::systemTimeZone());
    const bool parsed = KCalendarCore::ICalFormat().fromRawString(calendar, ics);
    if (valid) {
        *valid = parsed;
    }
    if (others) {
        *others = parsed ? calendar->rawTodos().size() + calendar->rawJournals().size() : 0;
    }
    QList<Event::List> result;
    if (!parsed) {
        return result;
    }
    QHash<QString, int> index;
    const Event::List events = calendar->rawEvents(KCalendarCore::EventSortStartDate);
    for (const Event::Ptr &event : events) {
        const Event::Ptr copy(event->clone());
        if (copy->uid().isEmpty()) {
            copy->setUid(newId());
        }
        const auto found = index.constFind(copy->uid());
        if (found == index.cend()) {
            index.insert(copy->uid(), result.size());
            result.append({copy});
        } else {
            result[*found].append(copy);
        }
    }
    return result;
}
}

CalendarFile Engine::inspect(const QByteArray &ics)
{
    CalendarFile file;
    const QList<Event::List> found = eventsByUid(ics, &file.valid, &file.others);
    for (const Event::List &events : found) {
        const Event::Ptr master = masterOf(events);
        const Event::Ptr event = master ? master : events.first();
        CalendarFile::Entry entry;
        entry.summary = event->summary().trimmed();
        entry.allDay = event->allDay();
        entry.start = entry.allDay ? QDateTime(event->dtStart().date(), QTime(0, 0)) : event->dtStart().toLocalTime();
        entry.repeats = event->recurs();
        file.events.append(entry);
    }
    std::stable_sort(file.events.begin(), file.events.end(), [](const CalendarFile::Entry &a, const CalendarFile::Entry &b) {
        return a.start < b.start;
    });
    return file;
}

QString Engine::importEvents(const QString &collectionId, const QByteArray &ics, int *imported)
{
    if (imported) {
        *imported = 0;
    }
    if (m_readOnly) {
        return i18nc("@info", "The calendars can only be looked at here.");
    }
    const Collection *target = collection(collectionId);
    if (!target) {
        return i18nc("@info", "The calendar no longer exists. Choose another one.");
    }
    if (target->readOnly) {
        return i18nc("@info", "The calendar “%1” cannot be changed.", target->name);
    }
    bool valid = false;
    const QList<Event::List> found = eventsByUid(ics, &valid);
    if (!valid) {
        return i18nc("@info", "This is not a calendar file.");
    }
    State &state = m_state[collectionId];
    // Written down together: there may be thousands.
    m_db.begin();
    for (const Event::List &events : found) {
        // An event the calendar has already is replaced by the one from the
        // file, on the server too, even if it was just deleted here.
        QString href = state.hrefByUid.value(events.first()->uid());
        const QString etag = href.isEmpty() ? QString() : state.items.value(href).etag;
        if (href.isEmpty()) {
            href = newHref(*target, events.first()->uid());
        }
        state.held.remove(href);
        setItem(collectionId, href, etag, serialize(events), ItemRecord::Changed);
    }
    m_db.commit();
    if (imported) {
        *imported = found.size();
    }
    if (!found.isEmpty()) {
        Q_EMIT eventsChanged();
        push(collectionId);
    }
    return {};
}

int Engine::deleteEvent(const QString &collectionId, const QString &href, const QDateTime &slot, bool wholeSeries)
{
    if (m_readOnly || !m_state.contains(collectionId) || !m_state[collectionId].items.contains(href)) {
        return 0;
    }
    State &state = m_state[collectionId];
    const Item before = state.items.value(href);
    const Event::Ptr master = masterOf(before.events);
    const bool whole = wholeSeries || !master || !master->recurs() || !slot.isValid();

    m_undo = Undo{++m_lastToken, collectionId, href, before, whole};
    if (whole) {
        // Gone from view at once, but the server only hears of it once it can no longer be undone.
        setItem(collectionId, href, before.etag, before.ics, ItemRecord::Deleted);
        state.held.insert(href);
        QTimer::singleShot(m_undoPeriod, this, [this, collectionId, href] {
            if (m_state.contains(collectionId)) {
                m_state[collectionId].held.remove(href);
                push(collectionId);
            }
        });
    } else {
        Event::List events;
        for (const Event::Ptr &existing : before.events) {
            if (existing->hasRecurrenceId() && existing->recurrenceId() == slot) {
                continue; // an exception for just this appearance
            }
            events.append(Event::Ptr(existing->clone()));
        }
        const Event::Ptr copy = masterOf(events);
        if (copy->allDay()) {
            copy->recurrence()->addExDate(slot.date());
        } else {
            copy->recurrence()->addExDateTime(slot.toTimeZone(copy->dtStart().timeZone()));
        }
        copy->setLastModified(QDateTime::currentDateTimeUtc());
        copy->setRevision(copy->revision() + 1);
        setItem(collectionId, href, before.etag, serialize(events), ItemRecord::Changed);
        push(collectionId);
    }
    Q_EMIT eventsChanged();
    return m_undo.token;
}

void Engine::undo(int token)
{
    if (token == 0 || m_undo.token != token || !m_state.contains(m_undo.collectionId)) {
        return;
    }
    const Undo undo = m_undo;
    m_undo = {};
    State &state = m_state[undo.collectionId];
    if (undo.wholeItem && state.held.remove(undo.href)) {
        // The server never heard of the deletion.
        setItem(undo.collectionId, undo.href, undo.before.etag, undo.before.ics, undo.before.dirty);
    } else {
        // Deleted for real by now, or changed: put back what was there.
        const QString etag = state.items.contains(undo.href) ? state.items.value(undo.href).etag : QString();
        setItem(undo.collectionId, undo.href, etag, undo.before.ics, ItemRecord::Changed);
    }
    Q_EMIT eventsChanged();
    push(undo.collectionId);
}

// Synchronization

bool Engine::isSyncing() const
{
    return !m_syncing.isEmpty();
}

double Engine::syncProgress() const
{
    return m_syncTotal > 0 ? double(m_syncDone) / m_syncTotal : -1;
}

int Engine::pendingChanges() const
{
    int count = 0;
    for (const State &state : m_state) {
        for (const Item &item : state.items) {
            count += item.dirty != ItemRecord::Clean;
        }
    }
    return count;
}

void Engine::syncAll()
{
    const QList<Account> accounts = m_accounts;
    for (const Account &account : accounts) {
        syncAccount(account.id);
    }
}

void Engine::syncFinished(const QString &accountId)
{
    if (m_syncing.remove(accountId)) {
        if (m_syncing.isEmpty()) {
            m_syncTotal = 0;
            m_syncDone = 0;
            Q_EMIT syncProgressChanged();
        }
        Q_EMIT syncingChanged();
    }
}

void Engine::syncAccount(const QString &id)
{
    const Account *found = account(id);
    if (m_readOnly || !found || found->type == Account::Local || m_syncing.contains(id) || !m_auth) {
        return;
    }
    m_syncing.insert(id, 0);
    Q_EMIT syncingChanged();
    m_auth(*found, [this, id](const QByteArray &authorization, const QString &error) {
        const Account *found = account(id);
        if (!found || !m_syncing.contains(id)) {
            return;
        }
        if (!error.isEmpty()) {
            setAccountError(id, error);
            syncFinished(id);
            return;
        }
        CalendarClient *client = clientFor(*found, authorization);
        client->listCalendars(found->url, [this, id, client](const DavReply &reply, const QList<CalendarInfo> &calendars) {
            if (!account(id) || !m_syncing.contains(id)) {
                return;
            }
            if (!reply.ok) {
                setAccountError(id, reply.error);
                syncFinished(id);
                return;
            }
            setAccountError(id, {});
            reconcile(id, calendars);

            QList<QPair<QString, QString>> todo; // calendar and what the server says its state is
            for (const CalendarInfo &info : calendars) {
                for (const Collection &collection : std::as_const(m_collections)) {
                    if (collection.accountId == id && collection.url == info.url) {
                        todo.append({collection.id, info.ctag});
                    }
                }
            }
            if (todo.isEmpty()) {
                syncFinished(id);
                return;
            }
            m_syncing[id] = todo.size();
            m_syncTotal += todo.size();
            Q_EMIT syncProgressChanged();
            const auto oneDone = [this, id] {
                if (m_syncing.contains(id)) {
                    ++m_syncDone;
                    Q_EMIT syncProgressChanged();
                }
                if (m_syncing.contains(id) && --m_syncing[id] <= 0) {
                    syncFinished(id);
                }
            };
            for (const auto &[collectionId, ctag] : std::as_const(todo)) {
                // What was changed here goes to the server first; then what changed there is fetched.
                push(collectionId, [=, this] {
                    const Collection *collection = this->collection(collectionId);
                    if (!collection || !m_clients.contains(id)) {
                        oneDone();
                        return;
                    }
                    State &state = m_state[collectionId];
                    const bool pushed = state.pushedSomething;
                    state.pushedSomething = false;
                    if (!pushed && !ctag.isEmpty() && ctag == collection->ctag) {
                        oneDone(); // nothing changed in this calendar
                        return;
                    }
                    // After sending, the state the server reported before is out of date.
                    pull(collectionId, client, pushed ? QString() : ctag, oneDone);
                });
            }
        });
    });
}

void Engine::pull(const QString &collectionId, CalendarClient *client, const QString &ctag, const std::function<void()> &done)
{
    const Collection *found = collection(collectionId);
    if (!found) {
        done();
        return;
    }
    const QUrl url = found->url;
    const QString accountId = found->accountId;
    client->listEtags(url, [=, this](const DavReply &reply, const QHash<QString, QString> &etags) {
        if (!m_state.contains(collectionId)) {
            done();
            return;
        }
        if (!reply.ok) {
            setAccountError(accountId, reply.error);
            done();
            return;
        }
        State &state = m_state[collectionId];
        QStringList wanted;
        for (auto it = etags.cbegin(); it != etags.cend(); ++it) {
            const auto item = state.items.constFind(it.key());
            if (item == state.items.cend() || (item->dirty == ItemRecord::Clean && item->etag != it.value())) {
                wanted.append(it.key());
            }
        }
        bool changed = false;
        const QStringList known = state.items.keys();
        m_db.begin();
        for (const QString &href : known) {
            if (!etags.contains(href) && state.items.value(href).dirty == ItemRecord::Clean) {
                dropItem(collectionId, href);
                changed = true;
            }
        }
        m_db.commit();
        const auto finish = [=, this](bool complete, bool changed) {
            Collection *found = findCollection(collectionId);
            if (found && complete && found->ctag != ctag) {
                found->ctag = ctag;
                m_db.saveCollection(*found);
            }
            if (changed) {
                Q_EMIT eventsChanged();
            }
            done();
        };
        if (wanted.isEmpty()) {
            finish(true, changed);
            return;
        }
        client->fetch(url, wanted, [=, this](const DavReply &reply, const QList<FetchedItem> &items) {
            if (!m_state.contains(collectionId)) {
                done();
                return;
            }
            if (!reply.ok) {
                setAccountError(accountId, reply.error);
                finish(false, changed);
                return;
            }
            const State &state = m_state[collectionId];
            m_db.begin();
            for (const FetchedItem &item : items) {
                if (state.items.value(item.href).dirty == ItemRecord::Clean) {
                    setItem(collectionId, item.href, item.etag, item.ics, ItemRecord::Clean);
                }
            }
            m_db.commit();
            finish(true, changed || !items.isEmpty());
        });
    });
}

void Engine::push(const QString &collectionId, const std::function<void()> &done)
{
    const Collection *found = collection(collectionId);
    const Account *owner = found ? account(found->accountId) : nullptr;
    if (!owner) {
        if (done) {
            done();
        }
        return;
    }
    State &state = m_state[collectionId];
    if (done) {
        state.afterPush.append(done);
    }
    if (state.pushing) {
        return;
    }
    QStringList pending;
    for (auto it = state.items.cbegin(); it != state.items.cend(); ++it) {
        if (it->dirty != ItemRecord::Clean && !state.held.contains(it.key())) {
            pending.append(it.key());
        }
    }
    if (owner->type == Account::Local) {
        // Nothing to send anywhere: what is stored is all there is.
        for (const QString &href : std::as_const(pending)) {
            const Item item = state.items.value(href);
            if (item.dirty == ItemRecord::Deleted) {
                dropItem(collectionId, href);
            } else {
                setItem(collectionId, href, item.etag, item.ics, ItemRecord::Clean);
            }
        }
        pushFinished(collectionId);
        return;
    }
    if (pending.isEmpty() || !m_auth) {
        pushFinished(collectionId);
        return;
    }
    state.pushing = true;
    state.failed.clear();
    const QString accountId = owner->id;
    m_auth(*owner, [=, this](const QByteArray &authorization, const QString &error) {
        const Account *owner = account(accountId);
        if (!owner || !m_state.contains(collectionId)) {
            return;
        }
        if (!error.isEmpty()) {
            setAccountError(accountId, error);
            pushFinished(collectionId);
            return;
        }
        pushNext(collectionId, clientFor(*owner, authorization));
    });
}

void Engine::pushFinished(const QString &collectionId)
{
    if (!m_state.contains(collectionId)) {
        return;
    }
    State &state = m_state[collectionId];
    state.pushing = false;
    const QList<std::function<void()>> callbacks = state.afterPush;
    state.afterPush.clear();
    for (const auto &callback : callbacks) {
        callback();
    }
}

void Engine::pushNext(const QString &collectionId, CalendarClient *client)
{
    const Collection *found = collection(collectionId);
    if (!found) {
        return;
    }
    State &state = m_state[collectionId];
    QString href;
    for (auto it = state.items.cbegin(); it != state.items.cend(); ++it) {
        if (it->dirty != ItemRecord::Clean && !state.held.contains(it.key()) && !state.failed.contains(it.key())) {
            href = it.key();
            break;
        }
    }
    if (href.isEmpty()) {
        pushFinished(collectionId);
        return;
    }
    const Item item = state.items.value(href);
    const QString accountId = found->accountId;
    const QString calendarName = found->name;

    // What to do when the server did not take it.
    const auto refused = [=, this](const DavReply &reply) {
        State &state = m_state[collectionId];
        if (reply.status == 412 || reply.status == 409) {
            Q_EMIT notice(i18nc("@info", "An event was changed elsewhere in the meantime. The newer version was kept."));
            resolveConflict(collectionId, href, client);
        } else if (reply.status == 403) {
            Q_EMIT notice(i18nc("@info", "The calendar “%1” cannot be changed, so the change was taken back.", calendarName));
            resolveConflict(collectionId, href, client);
        } else if (reply.status == 0 || reply.status == 401 || reply.status >= 500) {
            // The server cannot be used right now; everything stays for the next time.
            setAccountError(accountId, reply.error);
            pushFinished(collectionId);
        } else {
            state.failed.insert(href);
            pushNext(collectionId, client);
        }
    };

    if (item.dirty == ItemRecord::Deleted) {
        client->remove(QUrl(href), item.etag, [=, this](const DavReply &reply) {
            if (!m_state.contains(collectionId)) {
                return;
            }
            if (reply.ok || reply.status == 404 || reply.status == 410) {
                m_state[collectionId].pushedSomething = true;
                dropItem(collectionId, href);
                pushNext(collectionId, client);
            } else {
                refused(reply);
            }
        });
        return;
    }
    client->put(QUrl(href), item.ics, item.etag, [=, this](const DavReply &reply) {
        if (!m_state.contains(collectionId)) {
            return;
        }
        if (!reply.ok) {
            refused(reply);
            return;
        }
        m_state[collectionId].pushedSomething = true;
        setAccountError(accountId, {});
        const auto stored = [=, this](const QString &etag) {
            if (!m_state.contains(collectionId) || !m_state[collectionId].items.contains(href)) {
                return;
            }
            const Item now = m_state[collectionId].items.value(href);
            // It may have been changed again while this was under way.
            const bool unchanged = now.dirty == ItemRecord::Changed && now.ics == item.ics;
            setItem(collectionId, href, etag, now.ics, unchanged ? int(ItemRecord::Clean) : now.dirty);
            pushNext(collectionId, client);
        };
        if (reply.etag.isEmpty()) {
            // Without the new version's mark, the next change would look like a conflict.
            client->etagOf(QUrl(href), [=](const DavReply &reply) {
                stored(reply.etag);
            });
        } else {
            stored(reply.etag);
        }
    });
}

void Engine::resolveConflict(const QString &collectionId, const QString &href, CalendarClient *client)
{
    const Collection *found = collection(collectionId);
    if (!found) {
        return;
    }
    client->fetch(found->url, {href}, [=, this](const DavReply &reply, const QList<FetchedItem> &items) {
        if (!m_state.contains(collectionId)) {
            return;
        }
        if (!reply.ok) {
            m_state[collectionId].failed.insert(href);
        } else if (items.isEmpty()) {
            dropItem(collectionId, href); // deleted on the server
        } else {
            setItem(collectionId, href, items.first().etag, items.first().ics, ItemRecord::Clean);
        }
        Q_EMIT eventsChanged();
        pushNext(collectionId, client);
    });
}
