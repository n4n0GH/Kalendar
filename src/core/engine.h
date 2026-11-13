// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "database.h"
#include "davclient.h"
#include "types.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QVariant>

#include <functional>

class QNetworkAccessManager;

/// One appearance of an event: an event that repeats has many.
struct Occurrence {
    KCalendarCore::Event::Ptr event;
    QString collectionId;
    QString href;
    /// In local time.
    QDateTime start;
    QDateTime end;
    bool allDay = false;
    /// The days it is shown on.
    QDate firstDay;
    QDate lastDay;
};

/// What a calendar file from elsewhere holds, to show before it is imported.
struct CalendarFile {
    struct Entry {
        QString summary;
        /// In local time.
        QDateTime start;
        bool allDay = false;
        bool repeats = false;
    };

    /// Whether it is a calendar file at all.
    bool valid = false;
    /// One per event, however often it repeats; the earliest first.
    QList<Entry> events;
    /// What it holds besides events, such as to-dos; those are left out.
    int others = 0;
};

/// The calendars and their events: keeps them on disk, keeps them in step
/// with their servers, and answers what happens when.
///
/// Changes are applied here at once and sent to the server afterwards, so
/// the calendar can be used without a network; what could not be sent is
/// sent at the next synchronization.
class Engine : public QObject
{
    Q_OBJECT

public:
    using AuthCallback = std::function<void(const QByteArray &header, const QString &error)>;
    /// Provides the Authorization header for an account, which may take a
    /// while (a wallet to open, a token to renew).
    using AuthProvider = std::function<void(const Account &, const AuthCallback &)>;

    explicit Engine(QObject *parent = nullptr);
    ~Engine() override;

    /// Loads everything from the database at @p path. On first use, this
    /// creates a calendar that lives on this system only.
    bool open(const QString &path);
    /// Loads everything from the database at @p path to look at only:
    /// nothing is written, sent or fetched. For other programs that show
    /// Kalendar's events while Kalendar itself may be running.
    bool openReadOnly(const QString &path);
    QString errorString() const;
    void setAuthProvider(const AuthProvider &provider);

    const QList<Account> &accounts() const;
    const QList<Collection> &collections() const;
    const Account *account(const QString &id) const;
    const Collection *collection(const QString &id) const;
    /// Why an account could not be synchronized last time; empty if it could.
    QString accountError(const QString &accountId) const;

    /// Looks for the calendars at @p address without adding anything.
    void discover(const QUrl &address, const QByteArray &authorization, const std::function<void(const Discovery &)> &done);
    /// The same for a Google account, where @p api is where Google's
    /// Calendar API is (GoogleClient::api(), unless it is a test).
    void discoverGoogle(const QUrl &api, const QByteArray &authorization, const std::function<void(const Discovery &)> &done);
    /// Adds an account with the calendars found for it and starts fetching
    /// their events. Gives the account's id, which is made up here unless
    /// @p account brings one.
    QString addAccount(Account account, const QList<CalendarInfo> &calendars);
    void removeAccount(const QString &id);
    void setCollectionEnabled(const QString &id, bool enabled);

    void syncAll();
    void syncAccount(const QString &id);
    bool isSyncing() const;
    /// How far the synchronization under way has come, from 0 to 1;
    /// negative while that cannot be told yet, or none is under way.
    double syncProgress() const;
    /// How many events were changed here and are not on their server yet.
    int pendingChanges() const;

    /// Every appearance of an event from @p from to @p to (inclusive) in the
    /// calendars that are shown.
    QList<Occurrence> occurrences(const QDate &from, const QDate &to) const;
    /// The events of @p days days from @p start, each placed in a row
    /// ("lane") so that none overlap: for a week of a month view, or the
    /// all-day part of a week view. With @p barsOnly, only events that take
    /// whole days. At most @p maxLanes rows are used; "more" then counts,
    /// per day, the events that did not fit.
    QVariantMap weekLayout(const QDate &start, int days, bool barsOnly, int maxLanes) const;
    /// The events with a time of day on @p day, each placed in a column so
    /// that events at the same time are side by side.
    QVariantList dayLayout(const QDate &day) const;

    /// Creates or changes an event. Gives an error message, or an empty
    /// string. See the implementation for the fields of @p data.
    QString saveEvent(const QVariantMap &data);
    /// What the calendar file @p ics holds.
    static CalendarFile inspect(const QByteArray &ics);
    /// Adds the events of the calendar file @p ics to a calendar and sends
    /// them to its server. An event that the calendar has already (one of
    /// the same UID) is replaced, so importing a file twice changes nothing.
    /// Gives an error message, or an empty string; @p imported is how many
    /// events were taken.
    QString importEvents(const QString &collectionId, const QByteArray &ics, int *imported = nullptr);
    /// Deletes an event: all of it, or for a repeating event with
    /// @p wholeSeries false, only the appearance at @p occurrenceStart.
    /// Gives a token for undo(), or 0 if nothing was deleted.
    int deleteEvent(const QString &collectionId, const QString &href, const QDateTime &occurrenceStart, bool wholeSeries);
    /// Takes back a deletion. Possible until the next one.
    void undo(int token);
    /// How long a deletion is held back from the server, in milliseconds.
    void setUndoPeriod(int milliseconds);

    static QColor defaultColor(int index);

Q_SIGNALS:
    void accountsChanged();
    void collectionsChanged();
    /// Events were added, changed or removed, or calendars shown or hidden.
    void eventsChanged();
    void syncingChanged();
    void syncProgressChanged();
    void errorsChanged();
    /// Something happened that could be missed, in words for the user.
    void notice(const QString &text);

private:
    struct Item {
        QString etag;
        QByteArray ics;
        int dirty = ItemRecord::Clean;
        KCalendarCore::Event::List events;
        /// Whether the events are among those that are shown.
        bool shown = false;
    };
    struct State {
        KCalendarCore::MemoryCalendar::Ptr calendar;
        QHash<QString, Item> items;
        /// Which file an event is in, by the event's UID.
        QHash<QString, QString> hrefByUid;
        /// The longest repeating event, in days (never shrinks).
        int longestRepeating = 0;
        bool pushing = false;
        /// Events whose deletion can still be undone, and is not sent yet.
        QSet<QString> held;
        /// Events that could not be sent in the current attempt.
        QSet<QString> failed;
        bool pushedSomething = false;
        QList<std::function<void()>> afterPush;
    };
    struct Undo {
        int token = 0;
        QString collectionId;
        QString href;
        Item before;
        bool wholeItem = false;
    };

    void nameLocalAccount();
    Collection *findCollection(const QString &id);
    CalendarClient *clientFor(const Account &account, const QByteArray &authorization);
    void setAccountError(const QString &accountId, const QString &error);

    void setItem(const QString &collectionId, const QString &href, const QString &etag, const QByteArray &ics, int dirty, bool store = true);
    void dropItem(const QString &collectionId, const QString &href);
    static KCalendarCore::Event::List parse(const QByteArray &ics);
    /// Where a new event of @p uid is kept in @p target.
    QString newHref(const Collection &target, const QString &uid) const;
    static QByteArray serialize(const KCalendarCore::Event::List &events);
    QVariantMap toVariant(const Occurrence &occurrence) const;

    void push(const QString &collectionId, const std::function<void()> &done = {});
    void pushNext(const QString &collectionId, CalendarClient *client);
    void pushFinished(const QString &collectionId);
    void load();
    void resolveConflict(const QString &collectionId, const QString &href, CalendarClient *client);
    void pull(const QString &collectionId, CalendarClient *client, const QString &ctag, const std::function<void()> &done);
    void reconcile(const QString &accountId, const QList<CalendarInfo> &calendars);
    void syncFinished(const QString &accountId);

    Database m_db;
    QNetworkAccessManager *const m_network;
    AuthProvider m_auth;
    QList<Account> m_accounts;
    QList<Collection> m_collections;
    QHash<QString, State> m_state;
    QHash<QString, CalendarClient *> m_clients;
    QHash<QString, QString> m_errors;
    /// Accounts being synchronized, with the number of calendars still to do.
    QHash<QString, int> m_syncing;
    /// The calendars of the synchronization under way: all, and those done.
    int m_syncTotal = 0;
    int m_syncDone = 0;
    Undo m_undo;
    int m_lastToken = 0;
    int m_undoPeriod = 10000;
    bool m_readOnly = false;
};
