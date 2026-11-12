// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "davclient.h"

#include <KCalendarCore/Event>

#include <QJsonArray>
#include <QJsonObject>

class QNetworkReply;

/// Speaks Google's Calendar API (version 3), which is what the Google
/// accounts of the system's Online Accounts are allowed to use; Google's
/// CalDAV is closed to them.
///
/// To the rest of Kalendar its events look like those of any other server:
/// each one a calendar file with an address, holding an event together with
/// the exceptions to its repetition.
class GoogleClient : public CalendarClient
{
    Q_OBJECT

public:
    explicit GoogleClient(QNetworkAccessManager *network, QObject *parent = nullptr);

    /// Where Google's Calendar API is.
    static QUrl api();
    /// The address a new event with @p uid gets in @p collection.
    static QString hrefFor(const QUrl &collection, const QString &uid);

    void setAuthorization(const QByteArray &header) override;
    /// @p home is where the API is: api(), unless it is a test.
    void listCalendars(const QUrl &home, const std::function<void(const DavReply &, const QList<CalendarInfo> &)> &done) override;
    void listEtags(const QUrl &collection, const std::function<void(const DavReply &, const QHash<QString, QString> &)> &done) override;
    void fetch(const QUrl &collection, const QStringList &hrefs, const std::function<void(const DavReply &, const QList<FetchedItem> &)> &done) override;
    void put(const QUrl &url, const QByteArray &ics, const QString &etag, const Callback &done) override;
    void remove(const QUrl &url, const QString &etag, const Callback &done) override;
    void etagOf(const QUrl &url, const Callback &done) override;

    /// Turns events as Google lists them into calendar files: one for each
    /// event, with the changed appearances of a repeating event in the file
    /// of that event.
    static QList<FetchedItem> toItems(const QUrl &collection, const QJsonArray &events);
    /// An event as Google wants it. Fields that are not Kalendar's to change
    /// are left out, so that Google keeps what it has in them.
    static QJsonObject toJson(const KCalendarCore::Event::Ptr &event);

private:
    using JsonCallback = std::function<void(const DavReply &, const QJsonObject &)>;
    using Items = QHash<QString, FetchedItem>;

    void send(const QByteArray &verb, const QUrl &url, const QString &ifMatch, const QJsonObject &body, const JsonCallback &done);
    void listPage(const QUrl &home, const QString &pageToken, const QList<CalendarInfo> &sofar,
                  const std::function<void(const DavReply &, const QList<CalendarInfo> &)> &done);
    void list(const QUrl &collection, const QString &pageToken, const QJsonArray &sofar, const std::function<void(const DavReply &, const Items &)> &done);

    /// Sends the changed appearances of a repeating event, then the event.
    void patch(const QUrl &url, const QString &ifMatch, const KCalendarCore::Event::Ptr &master, const KCalendarCore::Event::List &changed,
               const Callback &done);

    QNetworkAccessManager *const m_network;
    QByteArray m_authorization;
    /// What listEtags() last saw, by calendar, for the fetch() that follows.
    QHash<QString, Items> m_listed;
};
