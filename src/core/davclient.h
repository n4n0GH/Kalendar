// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "types.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

/// One property of a resource, as a server reported it.
struct DavProp {
    /// The text directly inside the property.
    QString text;
    /// Addresses given inside it.
    QStringList hrefs;
    /// Names of the elements inside it, at any depth.
    QSet<QString> children;
    /// Component types named inside it, such as VEVENT.
    QStringList components;
};

/// What a server reported about one resource.
struct DavResponse {
    QString href;
    /// The properties the server did have, by name (without namespace).
    QHash<QString, DavProp> props;
    /// HTTP status given for the resource as a whole; 0 when none was.
    int status = 0;
};

struct DavReply {
    bool ok = false;
    /// HTTP status; 0 when the server was not reached.
    int status = 0;
    /// Why it failed, in words for the user.
    QString error;
    /// Where the answer finally came from, after redirections.
    QUrl url;
    QString etag;
    QList<DavResponse> responses;
};

struct Discovery {
    /// Empty when calendars were found.
    QString error;
    /// The folder holding the calendars.
    QUrl home;
    QList<CalendarInfo> calendars;
};

/// One event file as fetched from a server.
struct FetchedItem {
    QString href;
    QString etag;
    QByteArray ics;
};

/// Speaks CalDAV (RFC 4791): finds an account's calendars, lists and fetches
/// their events, and stores and deletes them.
class DavClient : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(const DavReply &)>;

    explicit DavClient(QNetworkAccessManager *network, QObject *parent = nullptr);

    /// The value of the Authorization header, such as "Basic …" or "Bearer …".
    void setAuthorization(const QByteArray &header);

    /// Finds the calendars of the account at @p address, which may be the
    /// server's name alone, or any address at or above the calendars.
    void discover(const QUrl &address, const std::function<void(const Discovery &)> &done);
    /// Lists the calendars in the calendar home @p home.
    void listCalendars(const QUrl &home, const std::function<void(const DavReply &, const QList<CalendarInfo> &)> &done);
    /// Lists address and version (etag) of every event in @p collection.
    void listEtags(const QUrl &collection, const std::function<void(const DavReply &, const QHash<QString, QString> &)> &done);
    /// Fetches the events at @p hrefs, which are full addresses.
    void fetch(const QUrl &collection, const QStringList &hrefs, const std::function<void(const DavReply &, const QList<FetchedItem> &)> &done);
    /// Stores an event. With an empty @p etag it must be new; otherwise the
    /// server's version must still be @p etag.
    void put(const QUrl &url, const QByteArray &ics, const QString &etag, const Callback &done);
    void remove(const QUrl &url, const QString &etag, const Callback &done);
    /// Asks for the current version (etag) of one event.
    void etagOf(const QUrl &url, const Callback &done);

    /// Reads a "multistatus" answer.
    static QList<DavResponse> parseMultiStatus(const QByteArray &xml);
    /// The address @p href, as given in an answer from @p base, in the one
    /// form used to tell events apart.
    static QString absolute(const QUrl &base, const QString &href);

private:
    void send(const QByteArray &verb, const QUrl &url, const QList<QPair<QByteArray, QByteArray>> &headers, const QByteArray &body, const Callback &done,
              int redirectsLeft = 5);
    void propfind(const QUrl &url, int depth, const QByteArray &body, const Callback &done);
    void finished(QNetworkReply *reply, const QByteArray &verb, const QList<QPair<QByteArray, QByteArray>> &headers, const QByteArray &body,
                  const Callback &done, int redirectsLeft);
    void discoverFrom(const QList<QUrl> &candidates, const QString &firstError, const std::function<void(const Discovery &)> &done);
    void discoverHome(const QUrl &home, const std::function<void(const Discovery &)> &done);

    QNetworkAccessManager *const m_network;
    QByteArray m_authorization;
    /// The host the authorization is meant for: the first one asked.
    QString m_authorizedHost;
};
