// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "engine.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QQmlEngine>

class GoogleAuth;
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
    /// Why the calendars may be out of date; empty when all is well.
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    /// Whether an account is being added right now.
    Q_PROPERTY(bool adding READ isAdding NOTIFY addingChanged)
    /// Whether this build has what signing in to Google needs. Otherwise the
    /// user has to supply a client ID and secret.
    Q_PROPERTY(bool googleConfigured READ isGoogleConfigured CONSTANT)
    /// The calendar new events go to unless another is chosen.
    Q_PROPERTY(QString defaultCalendarId READ defaultCalendarId NOTIFY writableCalendarsChanged)
    /// The calendars that events can be added to, each with id, name, color
    /// and accountName.
    Q_PROPERTY(QVariantList writableCalendars READ writableCalendars NOTIFY writableCalendarsChanged)

public:
    explicit Backend(Engine *engine, SecretStore *secrets, QObject *parent = nullptr);
    /// There is one Backend, made in main().
    static Backend *create(QQmlEngine *, QJSEngine *);
    static void setInstance(Backend *backend);

    CalendarListModel *calendars() const;
    QVariantList accounts() const;
    int revision() const;
    bool isSyncing() const;
    QString errorText() const;
    bool isAdding() const;
    bool isGoogleConfigured() const;
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
    /// Empty arguments mean: use what this build has.
    Q_INVOKABLE void addGoogleAccount(const QString &clientId, const QString &clientSecret);
    Q_INVOKABLE void cancelAdding();
    Q_INVOKABLE void removeAccount(const QString &id);

    /// The time of day @p text names, as typed by the user ("9", "9:30",
    /// "2 pm"); invalid if it names none.
    Q_INVOKABLE QDateTime parseTime(const QString &text, const QDateTime &day) const;
    /// @p date at the time of @p time.
    Q_INVOKABLE QDateTime combine(const QDateTime &date, const QDateTime &time) const;

Q_SIGNALS:
    void accountsChanged();
    void revisionChanged();
    void syncingChanged();
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
    GoogleAuth *const m_google;
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
