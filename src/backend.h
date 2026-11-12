// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "engine.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QQmlEngine>

class OnlineAccounts;
class QTimer;
class SecretStore;

/// The calendars, for the list in the sidebar.
class CalendarListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS

public:
    enum Role {
        IdRole = Qt::UserRole,
        NameRole,
        ColorRole,
        ShownRole,
        AccountNameRole,
        /// Whether this is the first calendar of its account in the list.
        FirstOfAccountRole,
    };

    explicit CalendarListModel(Engine *engine, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    void refresh();

    Engine *const m_engine;
    QStringList m_ids;
};

/// What the interface asks of the calendars: a thin layer over Engine that
/// adds the things only an app has, such as the wallet and signing in.
class Backend : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(CalendarListModel *calendars READ calendars CONSTANT)
    /// The accounts, each with id, name, kind (local, caldav or google),
    /// description, error and removable.
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    /// Goes up whenever events may look different; views read it so that
    /// they lay themselves out again.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(bool syncing READ isSyncing NOTIFY syncingChanged)
    /// How far the synchronization has come, from 0 to 1; negative while
    /// that cannot be told.
    Q_PROPERTY(double syncProgress READ syncProgress NOTIFY syncProgressChanged)
    /// Why the calendars may be out of date; empty when all is well.
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    /// Whether an account is being added right now.
    Q_PROPERTY(bool adding READ isAdding NOTIFY addingChanged)
    /// Whether times of day are written with 24 hours ("14:30") rather than
    /// twice 12 ("2:30 PM"). As is usual in the user's language, until set.
    Q_PROPERTY(bool use24Hour READ use24Hour WRITE setUse24Hour NOTIFY use24HourChanged)
    /// The day weeks start on, counted like JavaScript does: 0 is Sunday,
    /// 1 Monday. As is usual in the user's country, until set.
    Q_PROPERTY(int firstDayOfWeek READ firstDayOfWeek WRITE setFirstDayOfWeek NOTIFY firstDayOfWeekChanged)
    /// Whether the small month in the sidebar numbers its weeks.
    Q_PROPERTY(bool showWeekNumbers READ showWeekNumbers WRITE setShowWeekNumbers NOTIFY showWeekNumbersChanged)
    /// The calendar new events go to unless another is chosen.
    Q_PROPERTY(QString defaultCalendarId READ defaultCalendarId NOTIFY writableCalendarsChanged)
    /// The calendars that events can be added to, each with id, name, color,
    /// accountName and source (where it is kept, such as "Local").
    Q_PROPERTY(QVariantList writableCalendars READ writableCalendars NOTIFY writableCalendarsChanged)

public:
    explicit Backend(Engine *engine, SecretStore *secrets, OnlineAccounts *online, QObject *parent = nullptr);
    /// There is one Backend, made in main().
    static Backend *create(QQmlEngine *, QJSEngine *);
    static void setInstance(Backend *backend);

    CalendarListModel *calendars() const;
    QVariantList accounts() const;
    int revision() const;
    bool isSyncing() const;
    double syncProgress() const;
    bool use24Hour() const;
    void setUse24Hour(bool use);
    int firstDayOfWeek() const;
    void setFirstDayOfWeek(int day);
    bool showWeekNumbers() const;
    void setShowWeekNumbers(bool show);
    QString errorText() const;
    bool isAdding() const;
    QString defaultCalendarId() const;
    QVariantList writableCalendars() const;

    /// See Engine. @p revision is not used: handing in the property of that
    /// name is what makes a binding ask again when events change.
    Q_INVOKABLE QVariantMap weekLayout(const QDate &start, int days, bool barsOnly, int maxLanes, int revision) const;
    Q_INVOKABLE QVariantList dayLayout(const QDate &day, int revision) const;
    /// Gives an error message, or an empty string.
    Q_INVOKABLE QString saveEvent(const QVariantMap &data);
    /// Gives a token for undo(), or 0 if nothing was deleted.
    Q_INVOKABLE int deleteEvent(const QVariantMap &event, bool wholeSeries);
    Q_INVOKABLE void undo(int token);
    Q_INVOKABLE void setCalendarShown(const QString &id, bool shown);
    Q_INVOKABLE void sync();

    /// The result arrives as accountAdded() or accountFailed().
    Q_INVOKABLE void addCalDavAccount(const QString &server, const QString &user, const QString &password);
    /// The Google accounts set up under Online Accounts in System Settings,
    /// each with id, name, and added (whether its calendars are here already).
    Q_INVOKABLE QVariantList onlineGoogleAccounts() const;
    /// Adds the calendars of one of onlineGoogleAccounts().
    Q_INVOKABLE void addGoogleAccount(int onlineId);
    /// Shows Online Accounts in System Settings, where signing in is done.
    Q_INVOKABLE void openOnlineAccounts();
    Q_INVOKABLE void cancelAdding();
    Q_INVOKABLE void removeAccount(const QString &id);

    /// The time of day of @p time in words. @p use24Hour is to be the
    /// property of that name, which makes a binding ask again when it changes.
    Q_INVOKABLE QString formatTime(const QDateTime &time, bool use24Hour) const;
    /// The notes of an event, made fit to show: as rich text that holds
    /// nothing but harmless formatting and links. Notes written as HTML
    /// (which Google does) keep their formatting; in plain notes, line
    /// breaks are kept and addresses become links.
    Q_INVOKABLE QString renderNotes(const QString &notes) const;
    /// Whether @p notes are written as HTML.
    Q_INVOKABLE bool isHtml(const QString &notes) const;
    /// The time of day @p text names, as typed by the user ("9", "9:30",
    /// "2 pm"); invalid if it names none.
    Q_INVOKABLE QDateTime parseTime(const QString &text, const QDateTime &day) const;
    /// @p date at the time of @p time.
    Q_INVOKABLE QDateTime combine(const QDateTime &date, const QDateTime &time) const;

Q_SIGNALS:
    void accountsChanged();
    void revisionChanged();
    void syncingChanged();
    void syncProgressChanged();
    void use24HourChanged();
    void firstDayOfWeekChanged();
    void showWeekNumbersChanged();
    void errorTextChanged();
    void addingChanged();
    void writableCalendarsChanged();
    void accountAdded(const QString &name);
    void accountFailed(const QString &message);
    /// Something happened that could be missed, in words for the user.
    void notice(const QString &text);

private:
    void authorize(const Account &account, const Engine::AuthCallback &done);
    void setAdding(bool adding);
    void failAdding(const QString &message);
    void finishAdding(Account account, const QList<CalendarInfo> &calendars, const QString &secret);

    Engine *const m_engine;
    SecretStore *const m_secrets;
    OnlineAccounts *const m_online;
    CalendarListModel *const m_calendars;
    QTimer *const m_timer;
    int m_revision = 0;
    bool m_adding = false;
    /// Counts attempts to add an account, so that a cancelled one is ignored when it answers.
    int m_attempt = 0;

    struct Token {
        QByteArray header;
        QDateTime expires;
    };
    /// What accounts sign in with, once known; forgotten when Kalendar quits.
    QHash<QString, Token> m_tokens;
};
