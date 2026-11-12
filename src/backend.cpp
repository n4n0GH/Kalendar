// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backend.h"

#include "googleauth.h"
#include "kalendar-config.h"
#include "secretstore.h"

#include <KLocalizedString>

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QTimer>
#include <QUuid>

using namespace Qt::StringLiterals;

namespace
{
Backend *s_instance = nullptr;
constexpr int SyncMinutes = 15;
}

// CalendarListModel

CalendarListModel::CalendarListModel(Engine *engine, QObject *parent)
    : QAbstractListModel(parent)
    , m_engine(engine)
{
    connect(engine, &Engine::collectionsChanged, this, &CalendarListModel::refresh);
    connect(engine, &Engine::accountsChanged, this, &CalendarListModel::refresh);
    refresh();
}

void CalendarListModel::refresh()
{
    // Calendars are kept together by account, in the order of the accounts.
    QStringList ids;
    for (const Account &account : m_engine->accounts()) {
        for (const Collection &collection : m_engine->collections()) {
            if (collection.accountId == account.id) {
                ids.append(collection.id);
            }
        }
    }
    if (ids == m_ids) {
        // Only details changed: the rows stay, so that the one in use keeps the focus.
        if (!ids.isEmpty()) {
            Q_EMIT dataChanged(index(0), index(ids.size() - 1));
        }
        return;
    }
    beginResetModel();
    m_ids = ids;
    endResetModel();
}

int CalendarListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_ids.size();
}

QVariant CalendarListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid)) {
        return {};
    }
    const Collection *collection = m_engine->collection(m_ids.at(index.row()));
    if (!collection) {
        return {};
    }
    switch (role) {
    case IdRole:
        return collection->id;
    case Qt::DisplayRole:
    case NameRole:
        return collection->name;
    case ColorRole:
        return collection->color;
    case ShownRole:
        return collection->enabled;
    case AccountNameRole: {
        const Account *account = m_engine->account(collection->accountId);
        return account ? account->name : QString();
    }
    case FirstOfAccountRole: {
        const Collection *previous = index.row() > 0 ? m_engine->collection(m_ids.at(index.row() - 1)) : nullptr;
        return !previous || previous->accountId != collection->accountId;
    }
    }
    return {};
}

QHash<int, QByteArray> CalendarListModel::roleNames() const
{
    return {
        {IdRole, "calendarId"},
        {NameRole, "name"},
        {ColorRole, "calendarColor"},
        {ShownRole, "shown"},
        {AccountNameRole, "accountName"},
        {FirstOfAccountRole, "firstOfAccount"},
    };
}

// Backend

Backend::Backend(Engine *engine, SecretStore *secrets, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_secrets(secrets)
    , m_google(new GoogleAuth(new QNetworkAccessManager(this), this))
    , m_calendars(new CalendarListModel(engine, this))
    , m_timer(new QTimer(this))
{
    engine->setAuthProvider([this](const Account &account, const Engine::AuthCallback &done) {
        authorize(account, done);
    });
    connect(engine, &Engine::eventsChanged, this, [this] {
        ++m_revision;
        Q_EMIT revisionChanged();
    });
    connect(engine, &Engine::accountsChanged, this, &Backend::accountsChanged);
    connect(engine, &Engine::accountsChanged, this, &Backend::writableCalendarsChanged);
    connect(engine, &Engine::collectionsChanged, this, &Backend::writableCalendarsChanged);
    connect(engine, &Engine::errorsChanged, this, &Backend::accountsChanged);
    connect(engine, &Engine::errorsChanged, this, &Backend::errorTextChanged);
    connect(engine, &Engine::syncingChanged, this, &Backend::syncingChanged);
    connect(engine, &Engine::notice, this, &Backend::notice);

    connect(m_timer, &QTimer::timeout, this, &Backend::sync);
    m_timer->start(SyncMinutes * 60 * 1000);
    QTimer::singleShot(0, this, &Backend::sync);
}

Backend *Backend::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT(s_instance);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void Backend::setInstance(Backend *backend)
{
    s_instance = backend;
}

CalendarListModel *Backend::calendars() const
{
    return m_calendars;
}

int Backend::revision() const
{
    return m_revision;
}

bool Backend::isSyncing() const
{
    return m_engine->isSyncing();
}

bool Backend::isAdding() const
{
    return m_adding;
}

bool Backend::isGoogleConfigured() const
{
    return !QLatin1StringView(KALENDAR_GOOGLE_CLIENT_ID).isEmpty();
}

QVariantList Backend::accounts() const
{
    QVariantList result;
    for (const Account &account : m_engine->accounts()) {
        int calendars = 0;
        for (const Collection &collection : m_engine->collections()) {
            calendars += collection.accountId == account.id;
        }
        QString kind = u"local"_s;
        QString description = i18nc("@info", "Kept on this system only");
        if (account.type == Account::Google) {
            kind = u"google"_s;
            description = i18ncp("@info", "Google, %1 calendar", "Google, %1 calendars", calendars);
        } else if (account.type == Account::CalDav) {
            kind = u"caldav"_s;
            description = i18ncp("@info %2 is the name of a server", "%2, %1 calendar", "%2, %1 calendars", calendars, account.url.host());
        }
        result.append(QVariantMap{
            {u"id"_s, account.id},
            {u"name"_s, account.name},
            {u"kind"_s, kind},
            {u"description"_s, description},
            {u"error"_s, m_engine->accountError(account.id)},
            {u"removable"_s, account.type != Account::Local},
        });
    }
    return result;
}

QString Backend::errorText() const
{
    for (const Account &account : m_engine->accounts()) {
        const QString error = m_engine->accountError(account.id);
        if (!error.isEmpty()) {
            return i18nc("@info %1 is the name of an account, %2 says what went wrong", "“%1” is not up to date. %2", account.name, error);
        }
    }
    return {};
}

QVariantList Backend::writableCalendars() const
{
    QVariantList result;
    for (const Account &account : m_engine->accounts()) {
        for (const Collection &collection : m_engine->collections()) {
            if (collection.accountId == account.id && !collection.readOnly) {
                result.append(QVariantMap{
                    {u"id"_s, collection.id},
                    {u"name"_s, collection.name},
                    {u"color"_s, collection.color},
                    {u"accountName"_s, account.name},
                });
            }
        }
    }
    return result;
}

QString Backend::defaultCalendarId() const
{
    // The one used last, if it can still be used.
    const QString last = QSettings().value(u"Events/calendar"_s).toString();
    const Collection *found = m_engine->collection(last);
    if (found && !found->readOnly) {
        return last;
    }
    const QVariantList writable = writableCalendars();
    return writable.isEmpty() ? QString() : writable.first().toMap().value(u"id"_s).toString();
}

QVariantMap Backend::weekLayout(const QDate &start, int days, bool barsOnly, int maxLanes, int) const
{
    return m_engine->weekLayout(start, days, barsOnly, maxLanes);
}

QVariantList Backend::dayLayout(const QDate &day, int) const
{
    return m_engine->dayLayout(day);
}

QString Backend::saveEvent(const QVariantMap &data)
{
    const QString error = m_engine->saveEvent(data);
    if (error.isEmpty()) {
        QSettings().setValue(u"Events/calendar"_s, data.value(u"collectionId"_s));
        Q_EMIT writableCalendarsChanged();
    }
    return error;
}

int Backend::deleteEvent(const QVariantMap &event, bool wholeSeries)
{
    return m_engine->deleteEvent(event.value(u"collectionId"_s).toString(),
                                 event.value(u"href"_s).toString(),
                                 event.value(u"slot"_s).toDateTime(),
                                 wholeSeries);
}

void Backend::undo(int token)
{
    m_engine->undo(token);
}

void Backend::setCalendarShown(const QString &id, bool shown)
{
    m_engine->setCollectionEnabled(id, shown);
}

void Backend::sync()
{
    m_engine->syncAll();
}

// Signing in

void Backend::authorize(const Account &account, const Engine::AuthCallback &done)
{
    const Token known = m_tokens.value(account.id);
    if (!known.header.isEmpty() && (!known.expires.isValid() || known.expires > QDateTime::currentDateTimeUtc().addSecs(60))) {
        done(known.header, {});
        return;
    }
    const QString id = account.id;
    const QString user = account.user;
    const bool google = account.type == Account::Google;
    m_secrets->read(id, [=, this](const QString &secret, const QString &error) {
        if (!error.isEmpty()) {
            done({}, error);
            return;
        }
        if (secret.isEmpty()) {
            done({}, i18nc("@info", "The password is no longer in the wallet. Remove the account and add it again."));
            return;
        }
        if (!google) {
            const Token token{"Basic " + (user + u':' + secret).toUtf8().toBase64(), {}};
            m_tokens.insert(id, token);
            done(token.header, {});
            return;
        }
        // For Google the wallet holds what new access tokens are fetched with.
        const QJsonObject stored = QJsonDocument::fromJson(secret.toUtf8()).object();
        m_google->refresh(stored.value("client_id"_L1).toString(),
                          stored.value("client_secret"_L1).toString(),
                          stored.value("refresh_token"_L1).toString(),
                          [=, this](const GoogleAuth::Result &result) {
                              if (!result.error.isEmpty()) {
                                  done({}, result.error);
                                  return;
                              }
                              const Token token{"Bearer " + result.accessToken.toLatin1(), QDateTime::currentDateTimeUtc().addSecs(result.expiresIn)};
                              m_tokens.insert(id, token);
                              done(token.header, {});
                          });
    });
}

void Backend::setAdding(bool adding)
{
    if (m_adding != adding) {
        m_adding = adding;
        Q_EMIT addingChanged();
    }
}

void Backend::failAdding(const QString &message)
{
    setAdding(false);
    Q_EMIT accountFailed(message);
}

void Backend::cancelAdding()
{
    ++m_attempt;
    m_google->cancel();
    setAdding(false);
}

void Backend::finishAdding(Account account, const QList<CalendarInfo> &calendars, const QString &secret)
{
    for (const Account &existing : m_engine->accounts()) {
        if (existing.type == account.type && existing.url == account.url && existing.user == account.user) {
            failAdding(i18nc("@info", "The account “%1” has been added before.", existing.name));
            return;
        }
    }
    // The password goes to the wallet first, under the name the account will
    // have: the account is asked for it the moment it exists.
    const int attempt = m_attempt;
    account.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_secrets->write(account.id, secret, [=, this](const QString &error) {
        if (attempt != m_attempt) {
            m_secrets->remove(account.id);
            return;
        }
        if (!error.isEmpty()) {
            failAdding(error);
            return;
        }
        m_engine->addAccount(account, calendars);
        setAdding(false);
        Q_EMIT accountAdded(account.name);
    });
}

void Backend::addCalDavAccount(const QString &server, const QString &user, const QString &password)
{
    QString address = server.trimmed();
    if (!address.contains("://"_L1)) {
        address.prepend(u"https://"_s);
    }
    const QUrl url(address);
    if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "https"_L1 && url.scheme() != "http"_L1)) {
        failAdding(i18nc("@info", "“%1” is not the address of a server.", server.trimmed()));
        return;
    }
    const int attempt = ++m_attempt;
    setAdding(true);
    const QString name = user.trimmed();
    const QByteArray authorization = "Basic " + (name + u':' + password).toUtf8().toBase64();
    m_engine->discover(url, authorization, [=, this](const Discovery &found) {
        if (attempt != m_attempt) {
            return; // cancelled
        }
        if (!found.error.isEmpty()) {
            failAdding(found.error);
            return;
        }
        Account account;
        account.type = Account::CalDav;
        account.user = name;
        account.url = found.home;
        account.name = name.contains(u'@') ? name : name + u'@' + url.host();
        finishAdding(account, found.calendars, password);
    });
}

void Backend::addGoogleAccount(const QString &clientId, const QString &clientSecret)
{
    const QString id = clientId.trimmed().isEmpty() ? QStringLiteral(KALENDAR_GOOGLE_CLIENT_ID) : clientId.trimmed();
    const QString secret = clientSecret.trimmed().isEmpty() ? QStringLiteral(KALENDAR_GOOGLE_CLIENT_SECRET) : clientSecret.trimmed();
    const int attempt = ++m_attempt;
    setAdding(true);
    m_google->signIn(id, secret, [=, this](const GoogleAuth::Result &result) {
        if (attempt != m_attempt) {
            return;
        }
        if (!result.error.isEmpty()) {
            failAdding(result.error);
            return;
        }
        const QByteArray authorization = "Bearer " + result.accessToken.toLatin1();
        m_engine->discover(GoogleAuth::calDavAddress(result.email), authorization, [=, this](const Discovery &found) {
            if (attempt != m_attempt) {
                return;
            }
            if (!found.error.isEmpty()) {
                failAdding(found.error);
                return;
            }
            Account account;
            account.type = Account::Google;
            account.user = result.email;
            account.url = found.home;
            account.name = result.email;
            const QJsonObject stored{{"refresh_token"_L1, result.refreshToken}, {"client_id"_L1, id}, {"client_secret"_L1, secret}};
            finishAdding(account, found.calendars, QString::fromUtf8(QJsonDocument(stored).toJson(QJsonDocument::Compact)));
        });
    });
}

void Backend::removeAccount(const QString &id)
{
    m_engine->removeAccount(id);
    m_secrets->remove(id);
    m_tokens.remove(id);
}

// Helpers for forms

QDateTime Backend::parseTime(const QString &text, const QDateTime &day) const
{
    const QString input = text.trimmed();
    const QLocale locale;
    QTime time = locale.toTime(input, QLocale::ShortFormat);
    // People type less than the full format: "9", "930", "9.30", "2 pm".
    static const QStringList formats{u"H:mm"_s, u"H.mm"_s, u"Hmm"_s, u"H"_s, u"h:mm ap"_s, u"h:mmap"_s, u"h ap"_s, u"hap"_s};
    for (auto it = formats.cbegin(); !time.isValid() && it != formats.cend(); ++it) {
        time = QTime::fromString(input.toLower(), *it);
    }
    return time.isValid() ? QDateTime(day.date(), time) : QDateTime();
}

QDateTime Backend::combine(const QDateTime &date, const QDateTime &time) const
{
    return QDateTime(date.date(), time.time());
}
