// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "googleclient.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>
#include <KLocalizedString>

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUrlQuery>

using namespace Qt::StringLiterals;
using KCalendarCore::Event;

namespace
{
// Kept with each changed appearance of a repeating event, to tell later
// which of them were changed here.
const QByteArray App = "KALENDAR";
const QByteArray IdKey = "GOOGLE-ID";
const QByteArray StampKey = "GOOGLE-STAMP";

QString key(const QUrl &collection)
{
    return collection.toString(QUrl::FullyEncoded);
}

QString hrefOf(const QUrl &collection, const QString &id)
{
    return key(collection) + u'/' + QString::fromLatin1(QUrl::toPercentEncoding(id));
}

QString idOf(const QUrl &url)
{
    return QUrl::fromPercentEncoding(url.path(QUrl::FullyEncoded).section(u'/', -1).toLatin1());
}

QUrl collectionOf(const QUrl &url)
{
    const QString text = url.toString(QUrl::FullyEncoded);
    return QUrl(text.left(text.lastIndexOf(u'/')));
}

QTimeZone zoneOf(const QJsonObject &time)
{
    const QTimeZone zone(time.value("timeZone"_L1).toString().toLatin1());
    return zone.isValid() ? zone : QTimeZone::systemTimeZone();
}

/// A time as Google gives it: a day, or a moment with the time zone it is
/// meant in.
QDateTime timeOf(const QJsonObject &time, const QTimeZone &zone)
{
    if (time.value("date"_L1).isString()) {
        return QDateTime(QDate::fromString(time.value("date"_L1).toString(), Qt::ISODate), QTime(0, 0));
    }
    return QDateTime::fromString(time.value("dateTime"_L1).toString(), Qt::ISODate).toTimeZone(zone);
}

QString icalTime(const QDateTime &time, bool allDay)
{
    if (allDay) {
        return u";VALUE=DATE:"_s + time.date().toString(u"yyyyMMdd"_s);
    }
    return u";TZID="_s + QString::fromLatin1(time.timeZone().id()) + u':' + time.toString(u"yyyyMMdd'T'HHmmss"_s);
}

/// The event @p json describes; as an exception to a repeating event when
/// @p master is that event.
Event::Ptr toEvent(const QJsonObject &json, const Event::Ptr &master = {})
{
    const QJsonObject startJson = json.value("start"_L1).toObject();
    const QJsonObject endJson = json.value("end"_L1).toObject();
    const bool allDay = startJson.value("date"_L1).isString();
    const QTimeZone zone = zoneOf(startJson);
    const QDateTime start = timeOf(startJson, zone);
    QDateTime end = timeOf(endJson, zoneOf(endJson));
    if (!start.isValid()) {
        return {};
    }

    Event::Ptr event;
    const QJsonArray rules = json.value("recurrence"_L1).toArray();
    if (!rules.isEmpty() && !master) {
        // The rules are lines of a calendar file, so they are read as one.
        QString text = u"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:x\r\nDTSTART"_s + icalTime(start, allDay) + u"\r\n"_s;
        for (const QJsonValue &rule : rules) {
            text += rule.toString().trimmed() + u"\r\n"_s;
        }
        text += u"END:VEVENT\r\nEND:VCALENDAR\r\n"_s;
        const auto calendar = KCalendarCore::MemoryCalendar::Ptr::create(QTimeZone::systemTimeZone());
        if (KCalendarCore::ICalFormat().fromString(calendar, text) && !calendar->rawEvents().isEmpty()) {
            event = Event::Ptr(calendar->rawEvents().constFirst()->clone());
        }
    }
    if (!event) {
        event = Event::Ptr::create();
    }

    const QString id = json.value("id"_L1).toString();
    event->setUid(master ? master->uid() : json.value("iCalUID"_L1).toString(id));
    event->setSummary(json.value("summary"_L1).toString());
    event->setDescription(json.value("description"_L1).toString());
    event->setLocation(json.value("location"_L1).toString());
    event->setAllDay(allDay);
    event->setDtStart(start);
    if (allDay) {
        // Google names the day after the last one.
        end = end.isValid() && end > start ? end.addDays(-1) : start;
    } else if (!end.isValid() || end < start) {
        end = start;
    }
    event->setDtEnd(end);
    event->setTransparency(json.value("transparency"_L1).toString() == "transparent"_L1 ? Event::Transparent : Event::Opaque);
    event->setRevision(json.value("sequence"_L1).toInt());
    const QDateTime created = QDateTime::fromString(json.value("created"_L1).toString(), Qt::ISODate);
    if (created.isValid()) {
        event->setCreated(created.toUTC());
    }
    QDateTime updated = QDateTime::fromString(json.value("updated"_L1).toString(), Qt::ISODate).toUTC();
    if (!updated.isValid()) {
        updated = QDateTime::currentDateTimeUtc();
    }
    // Calendar files keep whole seconds only.
    updated.setTime(QTime(updated.time().hour(), updated.time().minute(), updated.time().second()));
    event->setLastModified(updated);

    if (master) {
        const QJsonObject original = json.value("originalStartTime"_L1).toObject();
        event->setRecurrenceId(timeOf(original, master->allDay() ? QTimeZone::systemTimeZone() : master->dtStart().timeZone()));
        event->setCustomProperty(App, IdKey, id);
        event->setCustomProperty(App, StampKey, updated.toString(Qt::ISODate));
    }
    return event;
}

QByteArray toIcs(const Event::List &events)
{
    const auto calendar = KCalendarCore::MemoryCalendar::Ptr::create(QTimeZone::systemTimeZone());
    for (const Event::Ptr &event : events) {
        calendar->addIncidence(event);
    }
    return KCalendarCore::ICalFormat().toString(calendar.staticCast<KCalendarCore::Calendar>()).toUtf8();
}

Event::List fromIcs(const QByteArray &ics)
{
    const auto calendar = KCalendarCore::MemoryCalendar::Ptr::create(QTimeZone::systemTimeZone());
    if (!KCalendarCore::ICalFormat().fromRawString(calendar, ics)) {
        return {};
    }
    return calendar->rawEvents();
}

QJsonObject timeJson(const QDateTime &time, bool allDay, const QTimeZone &zone)
{
    if (allDay) {
        return {{"date"_L1, time.date().toString(Qt::ISODate)}, {"dateTime"_L1, QJsonValue::Null}, {"timeZone"_L1, QJsonValue::Null}};
    }
    return {{"dateTime"_L1, time.toTimeZone(zone).toString(Qt::ISODate)}, {"timeZone"_L1, QString::fromLatin1(zone.id())}, {"date"_L1, QJsonValue::Null}};
}

/// The lines of @p event's calendar file that say how it repeats.
QJsonArray recurrenceLines(const Event::Ptr &event)
{
    QJsonArray lines;
    if (!event->recurs()) {
        return lines;
    }
    QString text = QString::fromUtf8(toIcs({Event::Ptr(event->clone())}));
    // Long lines are continued on the next, which then starts with a space.
    text.remove(QRegularExpression(u"\r?\n[ \t]"_s));
    static const QRegularExpression wanted(u"^(RRULE|EXRULE|RDATE|EXDATE)[;:]"_s);
    const QStringList all = text.split(QRegularExpression(u"\r?\n"_s), Qt::SkipEmptyParts);
    // The description of a time zone has such lines too, which are not meant.
    bool inEvent = false;
    for (const QString &line : all) {
        if (line == "BEGIN:VEVENT"_L1 || line == "END:VEVENT"_L1) {
            inEvent = line.startsWith("BEGIN"_L1);
        } else if (inEvent && wanted.match(line).hasMatch()) {
            lines.append(line);
        }
    }
    return lines;
}

QString describe(QNetworkReply *reply, int status, const QJsonObject &answer)
{
    if (status == 0) {
        return i18nc("@info %1 is a technical reason", "Google could not be reached: %1", reply->errorString());
    }
    if (status == 401) {
        return i18nc("@info", "Google no longer accepts the sign-in. Sign in again under Online Accounts in System Settings.");
    }
    const QString message = answer.value("error"_L1).toObject().value("message"_L1).toString();
    if (!message.isEmpty()) {
        return i18nc("@info %1 is what Google answered, in English", "Google answered: %1", message);
    }
    return i18nc("@info %1 is a number", "Google answered with an error (%1).", status);
}
}

GoogleClient::GoogleClient(QNetworkAccessManager *network, QObject *parent)
    : CalendarClient(parent)
    , m_network(network)
{
}

QUrl GoogleClient::api()
{
    return QUrl(u"https://www.googleapis.com/calendar/v3/"_s);
}

QString GoogleClient::hrefFor(const QUrl &collection, const QString &uid)
{
    // Google takes names made of digits and the letters a to v only.
    static const QRegularExpression allowed(u"^[0-9a-v]{5,1024}$"_s);
    QString id = uid.toLower().remove(u'-');
    if (!allowed.match(id).hasMatch()) {
        id = QString::fromLatin1(QCryptographicHash::hash(uid.toUtf8(), QCryptographicHash::Sha1).toHex());
    }
    return hrefOf(collection, id);
}

void GoogleClient::setAuthorization(const QByteArray &header)
{
    m_authorization = header;
}

void GoogleClient::send(const QByteArray &verb, const QUrl &url, const QString &ifMatch, const QJsonObject &body, const JsonCallback &done)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setRawHeader("User-Agent", "Kalendar");
    request.setRawHeader("Authorization", m_authorization);
    if (!ifMatch.isEmpty()) {
        request.setRawHeader("If-Match", ifMatch.toLatin1());
    }
    QByteArray data;
    if (verb == "POST" || verb == "PATCH") {
        request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
        data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    }
    QNetworkReply *reply = m_network->sendCustomRequest(request, verb, data);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
        DavReply result;
        result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.url = reply->url();
        result.ok = result.status >= 200 && result.status < 300;
        result.etag = answer.value("etag"_L1).toString();
        if (!result.ok) {
            result.error = describe(reply, result.status, answer);
            // Asked too much in too short a time: not the event's fault, so
            // it is to be tried again like when the server is down.
            const QString reason = answer.value("error"_L1).toObject().value("errors"_L1).toArray().at(0).toObject().value("reason"_L1).toString();
            if (result.status == 429 || (result.status == 403 && (reason.contains("imit"_L1) || reason.contains("quota"_L1)))) {
                result.status = 503;
            }
        }
        done(result, answer);
    });
}

void GoogleClient::listCalendars(const QUrl &home, const std::function<void(const DavReply &, const QList<CalendarInfo> &)> &done)
{
    listPage(home, {}, {}, done);
}

void GoogleClient::listPage(const QUrl &home, const QString &pageToken, const QList<CalendarInfo> &sofar,
                            const std::function<void(const DavReply &, const QList<CalendarInfo> &)> &done)
{
    QUrl url(home.toString(QUrl::FullyEncoded) + u"users/me/calendarList"_s);
    QUrlQuery query{{u"maxResults"_s, u"250"_s}};
    if (!pageToken.isEmpty()) {
        query.addQueryItem(u"pageToken"_s, QString::fromLatin1(QUrl::toPercentEncoding(pageToken)));
    }
    url.setQuery(query);
    send("GET", url, {}, {}, [=, this](const DavReply &reply, const QJsonObject &answer) {
        if (!reply.ok) {
            done(reply, {});
            return;
        }
        QList<CalendarInfo> calendars = sofar;
        const QJsonArray items = answer.value("items"_L1).toArray();
        for (const QJsonValue &value : items) {
            const QJsonObject item = value.toObject();
            const QString role = item.value("accessRole"_L1).toString();
            // Calendars that only tell when someone is busy have no events to show.
            if (item.value("deleted"_L1).toBool() || item.value("hidden"_L1).toBool() || role == "freeBusyReader"_L1) {
                continue;
            }
            CalendarInfo info;
            const QString id = QString::fromLatin1(QUrl::toPercentEncoding(item.value("id"_L1).toString()));
            info.url = QUrl(home.toString(QUrl::FullyEncoded) + u"calendars/"_s + id + u"/events"_s);
            info.name = item.value("summaryOverride"_L1).toString(item.value("summary"_L1).toString());
            info.color = QColor(item.value("backgroundColor"_L1).toString());
            info.readOnly = role != "owner"_L1 && role != "writer"_L1;
            calendars.append(info);
        }
        const QString next = answer.value("nextPageToken"_L1).toString();
        if (next.isEmpty()) {
            done(reply, calendars);
        } else {
            listPage(home, next, calendars, done);
        }
    });
}

void GoogleClient::list(const QUrl &collection, const QString &pageToken, const QJsonArray &sofar,
                        const std::function<void(const DavReply &, const Items &)> &done)
{
    QUrl url = collection;
    QUrlQuery query{{u"maxResults"_s, u"2500"_s}, {u"singleEvents"_s, u"false"_s}, {u"showDeleted"_s, u"false"_s}};
    if (!pageToken.isEmpty()) {
        query.addQueryItem(u"pageToken"_s, QString::fromLatin1(QUrl::toPercentEncoding(pageToken)));
    }
    url.setQuery(query);
    send("GET", url, {}, {}, [=, this](const DavReply &reply, const QJsonObject &answer) {
        if (!reply.ok) {
            done(reply, {});
            return;
        }
        QJsonArray events = sofar;
        const QJsonArray page = answer.value("items"_L1).toArray();
        for (const QJsonValue &event : page) {
            events.append(event);
        }
        const QString next = answer.value("nextPageToken"_L1).toString();
        if (!next.isEmpty()) {
            list(collection, next, events, done);
            return;
        }
        Items items;
        const QList<FetchedItem> converted = toItems(collection, events);
        for (const FetchedItem &item : converted) {
            items.insert(item.href, item);
        }
        done(reply, items);
    });
}

void GoogleClient::listEtags(const QUrl &collection, const std::function<void(const DavReply &, const QHash<QString, QString> &)> &done)
{
    list(collection, {}, {}, [=, this](const DavReply &reply, const Items &items) {
        QHash<QString, QString> etags;
        if (reply.ok) {
            for (const FetchedItem &item : items) {
                etags.insert(item.href, item.etag);
            }
            // Google sends the events along with their versions, so they
            // are kept for the moment they are asked for.
            m_listed.insert(key(collection), items);
        }
        done(reply, etags);
    });
}

void GoogleClient::fetch(const QUrl &collection, const QStringList &hrefs, const std::function<void(const DavReply &, const QList<FetchedItem> &)> &done)
{
    const auto pick = [hrefs, done](const DavReply &reply, const Items &items) {
        QList<FetchedItem> wanted;
        for (const QString &href : hrefs) {
            if (items.contains(href)) {
                wanted.append(items.value(href));
            }
        }
        done(reply, wanted);
    };
    if (m_listed.contains(key(collection))) {
        DavReply reply;
        reply.ok = true;
        reply.status = 200;
        pick(reply, m_listed.take(key(collection)));
        return;
    }
    list(collection, {}, {}, pick);
}

void GoogleClient::put(const QUrl &url, const QByteArray &ics, const QString &etag, const Callback &done)
{
    const QUrl collection = collectionOf(url);
    m_listed.remove(key(collection));
    const Event::List events = fromIcs(ics);
    Event::Ptr master;
    // The appearances of a repeating event that were changed here.
    Event::List changed;
    for (const Event::Ptr &event : events) {
        if (!event->hasRecurrenceId()) {
            master = event;
        } else if (!event->customProperty(App, IdKey).isEmpty()
                   && event->customProperty(App, StampKey) != event->lastModified().toUTC().toString(Qt::ISODate)) {
            changed.append(event);
        }
    }
    if (!master) {
        DavReply reply;
        reply.status = 400;
        reply.error = i18nc("@info", "The event could not be read.");
        done(reply);
        return;
    }
    if (etag.isEmpty()) {
        QJsonObject body = toJson(master);
        body.insert("id"_L1, idOf(url));
        body.insert("iCalUID"_L1, master->uid());
        send("POST", collection, {}, body, [done](const DavReply &reply, const QJsonObject &) {
            done(reply);
        });
        return;
    }
    // Changing its appearances may give the event a new version, which is
    // then no sign of a change made elsewhere.
    patch(url, changed.isEmpty() ? etag.section(u'|', 0, 0) : QString(), master, changed, done);
}

void GoogleClient::patch(const QUrl &url, const QString &ifMatch, const Event::Ptr &master, const Event::List &changed, const Callback &done)
{
    if (changed.isEmpty()) {
        send("PATCH", url, ifMatch, toJson(master), [done](const DavReply &reply, const QJsonObject &) {
            done(reply);
        });
        return;
    }
    Event::List rest = changed;
    const Event::Ptr exception = rest.takeFirst();
    const QUrl address(hrefOf(collectionOf(url), exception->customProperty(App, IdKey)));
    send("PATCH", address, {}, toJson(exception), [=, this](const DavReply &reply, const QJsonObject &) {
        if (reply.ok) {
            patch(url, ifMatch, master, rest, done);
        } else {
            done(reply);
        }
    });
}

void GoogleClient::remove(const QUrl &url, const QString &etag, const Callback &done)
{
    m_listed.remove(key(collectionOf(url)));
    send("DELETE", url, etag.section(u'|', 0, 0), {}, [done](const DavReply &reply, const QJsonObject &) {
        done(reply);
    });
}

void GoogleClient::etagOf(const QUrl &url, const Callback &done)
{
    QUrl address = url;
    address.setQuery(QUrlQuery{{u"fields"_s, u"etag"_s}});
    send("GET", address, {}, {}, [done](const DavReply &reply, const QJsonObject &) {
        done(reply);
    });
}

QList<FetchedItem> GoogleClient::toItems(const QUrl &collection, const QJsonArray &events)
{
    // Google lists the changed appearances of a repeating event as events of
    // their own, which name the event they belong to.
    QHash<QString, QJsonObject> masters;
    QHash<QString, QList<QJsonObject>> exceptions;
    QStringList order;
    for (const QJsonValue &value : events) {
        const QJsonObject event = value.toObject();
        const QString parent = event.value("recurringEventId"_L1).toString();
        if (!parent.isEmpty()) {
            exceptions[parent].append(event);
        } else if (event.value("status"_L1).toString() != "cancelled"_L1) {
            masters.insert(event.value("id"_L1).toString(), event);
            order.append(event.value("id"_L1).toString());
        }
    }
    // Without its event, a changed appearance is an event like any other.
    for (auto it = exceptions.cbegin(); it != exceptions.cend(); ++it) {
        if (masters.contains(it.key())) {
            continue;
        }
        for (const QJsonObject &event : it.value()) {
            if (event.value("status"_L1).toString() != "cancelled"_L1) {
                masters.insert(event.value("id"_L1).toString(), event);
                order.append(event.value("id"_L1).toString());
            }
        }
    }

    QList<FetchedItem> items;
    for (const QString &id : std::as_const(order)) {
        const QJsonObject json = masters.value(id);
        const Event::Ptr master = toEvent(json);
        if (!master) {
            continue;
        }
        Event::List all{master};
        QStringList versions;
        QList<QJsonObject> own = json.contains("recurringEventId"_L1) ? QList<QJsonObject>() : exceptions.value(id);
        for (const QJsonObject &exception : std::as_const(own)) {
            versions.append(exception.value("id"_L1).toString() + exception.value("etag"_L1).toString());
            if (exception.value("status"_L1).toString() == "cancelled"_L1) {
                const QJsonObject original = exception.value("originalStartTime"_L1).toObject();
                if (master->allDay()) {
                    master->recurrence()->addExDate(timeOf(original, QTimeZone::systemTimeZone()).date());
                } else {
                    master->recurrence()->addExDateTime(timeOf(original, master->dtStart().timeZone()));
                }
            } else if (const Event::Ptr event = toEvent(exception, master)) {
                all.append(event);
            }
        }
        FetchedItem item;
        item.href = hrefOf(collection, id);
        // The version says whether anything in the file changed, so the
        // changed appearances count too. What comes before the bar is the
        // version of the event itself, which Google is asked to compare.
        item.etag = json.value("etag"_L1).toString();
        if (!versions.isEmpty()) {
            versions.sort();
            item.etag += u'|' + QString::fromLatin1(QCryptographicHash::hash(versions.join(u' ').toUtf8(), QCryptographicHash::Sha1).toHex());
        }
        item.ics = toIcs(all);
        items.append(item);
    }
    return items;
}

QJsonObject GoogleClient::toJson(const Event::Ptr &event)
{
    QJsonObject json{
        {"summary"_L1, event->summary()},
        {"description"_L1, event->description()},
        {"location"_L1, event->location()},
        {"transparency"_L1, event->transparency() == Event::Transparent ? u"transparent"_s : u"opaque"_s},
    };
    const QDateTime start = event->dtStart();
    QDateTime end = event->hasEndDate() ? event->dtEnd() : start;
    if (event->allDay()) {
        json.insert("start"_L1, timeJson(start, true, {}));
        json.insert("end"_L1, timeJson(end.addDays(1), true, {}));
    } else {
        // Google wants the name of a time zone for events that repeat.
        QTimeZone zone = start.timeSpec() == Qt::LocalTime ? QTimeZone::systemTimeZone() : start.timeZone();
        if (start.timeSpec() == Qt::OffsetFromUTC || !zone.isValid() || !QTimeZone::isTimeZoneIdAvailable(zone.id())) {
            zone = QTimeZone::utc();
        }
        json.insert("start"_L1, timeJson(start, false, zone));
        json.insert("end"_L1, timeJson(end, false, zone));
    }
    if (!event->hasRecurrenceId()) {
        json.insert("recurrence"_L1, recurrenceLines(event));
    }
    return json;
}
