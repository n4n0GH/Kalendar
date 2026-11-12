// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backend.h"

#include "googleclient.h"
#include "onlineaccounts.h"
#include "secretstore.h"

#include <KLocalizedString>

#include <QDesktopServices>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
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

Backend::Backend(Engine *engine, SecretStore *secrets, OnlineAccounts *online, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_secrets(secrets)
    , m_online(online)
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
    connect(engine, &Engine::syncProgressChanged, this, &Backend::syncProgressChanged);
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

double Backend::syncProgress() const
{
    return m_engine->syncProgress();
}

bool Backend::isAdding() const
{
    return m_adding;
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
        // Where it is kept, in a word or two.
        QString source = account.name;
        if (account.type == Account::Local) {
            source = i18nc("@info where a calendar is kept: on this system only", "Local");
        } else if (account.type == Account::Google && !account.name.contains("google"_L1, Qt::CaseInsensitive)) {
            source = i18nc("@info where a calendar is kept; %1 is the name of an account", "Google, %1", account.name);
        }
        for (const Collection &collection : m_engine->collections()) {
            if (collection.accountId == account.id && !collection.readOnly) {
                result.append(QVariantMap{
                    {u"id"_s, collection.id},
                    {u"name"_s, collection.name},
                    {u"color"_s, collection.color},
                    {u"accountName"_s, account.name},
                    {u"source"_s, source},
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
    if (account.type == Account::Google) {
        // Online Accounts holds the sign-in and hands out a token for it.
        m_online->token(user.toUInt(), [=, this](const QString &token, int expiresIn, const QString &error) {
            if (!error.isEmpty()) {
                done({}, error);
                return;
            }
            const Token fresh{"Bearer " + token.toLatin1(), QDateTime::currentDateTimeUtc().addSecs(expiresIn)};
            m_tokens.insert(id, fresh);
            done(fresh.header, {});
        });
        return;
    }
    m_secrets->read(id, [=, this](const QString &secret, const QString &error) {
        if (!error.isEmpty()) {
            done({}, error);
            return;
        }
        if (secret.isEmpty()) {
            done({}, i18nc("@info", "The password is no longer in the wallet. Remove the account and add it again."));
            return;
        }
        const Token token{"Basic " + (user + u':' + secret).toUtf8().toBase64(), {}};
        m_tokens.insert(id, token);
        done(token.header, {});
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
    if (secret.isEmpty()) {
        m_engine->addAccount(account, calendars);
        setAdding(false);
        Q_EMIT accountAdded(account.name);
        return;
    }
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

QVariantList Backend::onlineGoogleAccounts() const
{
    QVariantList result;
    const QList<OnlineAccounts::Entry> entries = m_online->googleAccounts();
    for (const OnlineAccounts::Entry &entry : entries) {
        bool added = false;
        for (const Account &account : m_engine->accounts()) {
            added = added || (account.type == Account::Google && account.user == QString::number(entry.id));
        }
        result.append(QVariantMap{{u"id"_s, entry.id}, {u"name"_s, entry.name}, {u"added"_s, added}});
    }
    return result;
}

void Backend::addGoogleAccount(int onlineId)
{
    QString name;
    for (const OnlineAccounts::Entry &entry : m_online->googleAccounts()) {
        if (int(entry.id) == onlineId) {
            name = entry.name;
        }
    }
    const int attempt = ++m_attempt;
    setAdding(true);
    m_online->token(onlineId, [=, this](const QString &token, int, const QString &error) {
        if (attempt != m_attempt) {
            return;
        }
        if (!error.isEmpty()) {
            failAdding(error);
            return;
        }
        // Tests have a server of their own to talk to.
        const QUrl api = qEnvironmentVariableIsSet("KALENDAR_GOOGLE_API") ? QUrl(qEnvironmentVariable("KALENDAR_GOOGLE_API")) : GoogleClient::api();
        m_engine->discoverGoogle(api, "Bearer " + token.toLatin1(), [=, this](const Discovery &found) {
            if (attempt != m_attempt) {
                return;
            }
            if (!found.error.isEmpty()) {
                failAdding(found.error);
                return;
            }
            Account account;
            account.type = Account::Google;
            account.user = QString::number(onlineId);
            account.url = found.home;
            account.name = name;
            finishAdding(account, found.calendars, {});
        });
    });
}

void Backend::openOnlineAccounts()
{
    QDesktopServices::openUrl(QUrl(u"systemsettings://kcm_kaccounts"_s));
}

void Backend::removeAccount(const QString &id)
{
    m_engine->removeAccount(id);
    m_secrets->remove(id);
    m_tokens.remove(id);
}

// Helpers for forms

namespace
{
/// How the user's language writes a short time, made to count the hours
/// to 24, or twice to 12 with a mark for the half of the day.
QString timeFormat(bool use24Hour)
{
    QString format = QLocale().timeFormat(QLocale::ShortFormat);
    static const QRegularExpression mark(u"\\s*[aA][pP]?\\s*"_s);
    static const QRegularExpression zone(u"\\s*t+\\s*"_s);
    format.remove(zone);
    if (use24Hour) {
        format.remove(mark);
        format.replace(u'h', u'H');
    } else {
        format.replace(u"HH"_s, u"h"_s).replace(u'H', u'h');
        if (!format.contains(mark)) {
            format += u" AP"_s;
        }
    }
    return format.trimmed();
}
}

bool Backend::isHtml(const QString &notes) const
{
    // A tag that formats, or one of the names HTML has for characters.
    static const QRegularExpression html(
        u"<\\s*/?\\s*(a|b|i|u|s|p|br|em|strong|span|div|ul|ol|li|h[1-6]|html|body|blockquote|pre|code|table|font|hr)(\\s[^<>]*)?/?>|&(nbsp|amp|lt|gt|quot|#\\d+);"_s,
        QRegularExpression::CaseInsensitiveOption);
    return html.match(notes).hasMatch();
}

QString Backend::renderNotes(const QString &notes) const
{
    static const QRegularExpression address(u"\\b(https?://[^\\s<>\"']+[^\\s<>\"'.,;:!?)])"_s);
    if (!isHtml(notes)) {
        QString text = notes.toHtmlEscaped();
        // "&" was written as "&amp;" just now, in addresses too, where a link needs it as it was.
        text.replace(address, u"<a href=\"\\1\">\\1</a>"_s);
        return text.replace(u'\n', u"<br>"_s);
    }

    // What is only formatting is kept, without anything said about it
    // (which is where pictures to fetch and scripts to run hide); a link
    // keeps where it leads, if that is an ordinary place. All else goes.
    static const QSet<QString> kept{u"a"_s,  u"b"_s,  u"strong"_s, u"i"_s,  u"em"_s, u"u"_s,  u"s"_s,          u"br"_s,  u"p"_s,   u"div"_s,
                                    u"span"_s, u"ul"_s, u"ol"_s,     u"li"_s, u"h1"_s, u"h2"_s, u"h3"_s,         u"h4"_s,  u"h5"_s,  u"h6"_s,
                                    u"pre"_s,  u"code"_s, u"hr"_s,   u"table"_s, u"tr"_s, u"td"_s, u"th"_s, u"blockquote"_s, u"sub"_s, u"sup"_s};
    static const QRegularExpression unseen(u"<\\s*(script|style|head|title|iframe|object|svg)\\b[^>]*>.*?<\\s*/\\s*\\1\\s*>"_s,
                                           QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression comment(u"<!--.*?-->"_s, QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression tag(u"<\\s*(/?)\\s*([a-zA-Z][a-zA-Z0-9]*)([^<>]*)>"_s);
    static const QRegularExpression target(u"\\bhref\\s*=\\s*(\"([^\"]*)\"|'([^']*)'|([^\\s\"'>]+))"_s, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression ordinary(u"^(https?://|mailto:|tel:)"_s, QRegularExpression::CaseInsensitiveOption);

    QString html = notes;
    html.remove(unseen).remove(comment);
    QString result;
    qsizetype from = 0;
    auto it = tag.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch found = it.next();
        result += QStringView(html).mid(from, found.capturedStart() - from);
        from = found.capturedEnd();
        const QString name = found.captured(2).toLower();
        if (!kept.contains(name)) {
            continue;
        }
        const bool closing = !found.captured(1).isEmpty();
        result += closing ? u"</"_s : u"<"_s;
        result += name;
        if (name == "a"_L1 && !closing) {
            const QRegularExpressionMatch href = target.match(found.captured(3));
            const QString where = QString(href.captured(2) + href.captured(3) + href.captured(4)).trimmed();
            if (ordinary.match(where).hasMatch()) {
                result += u" href=\""_s + where.toHtmlEscaped().replace(u"&amp;amp;"_s, u"&amp;"_s) + u'"';
            }
        }
        result += u'>';
    }
    result += QStringView(html).mid(from);
    // What is left of a tag that was cut off must not start one.
    static const QRegularExpression stray(u"<(?!/?(%1)\\b[^<>]*>)"_s.arg(QStringList(kept.cbegin(), kept.cend()).join(u'|')));
    result.replace(stray, u"&lt;"_s);
    // Line breaks in the text count as such only when no tags say where lines end.
    static const QRegularExpression breaks(u"<\\s*(br|p|div|li)\\b"_s, QRegularExpression::CaseInsensitiveOption);
    if (!breaks.match(result).hasMatch()) {
        result.replace(u'\n', u"<br>"_s);
    }
    return result;
}

bool Backend::use24Hour() const
{
    const QString chosen = QSettings().value(u"View/hours"_s).toString();
    if (chosen == "24"_L1 || chosen == "12"_L1) {
        return chosen == "24"_L1;
    }
    return !QLocale().timeFormat(QLocale::ShortFormat).contains(u'a', Qt::CaseInsensitive);
}

void Backend::setUse24Hour(bool use)
{
    if (use != use24Hour() || !QSettings().contains(u"View/hours"_s)) {
        QSettings().setValue(u"View/hours"_s, use ? u"24"_s : u"12"_s);
        Q_EMIT use24HourChanged();
    }
}

int Backend::firstDayOfWeek() const
{
    bool chosen = false;
    const int day = QSettings().value(u"View/firstDayOfWeek"_s).toInt(&chosen);
    if (chosen && day >= 0 && day <= 6) {
        return day;
    }
    // Qt counts from Monday as 1 to Sunday as 7.
    return QLocale().firstDayOfWeek() % 7;
}

void Backend::setFirstDayOfWeek(int day)
{
    if (day >= 0 && day <= 6 && (day != firstDayOfWeek() || !QSettings().contains(u"View/firstDayOfWeek"_s))) {
        QSettings().setValue(u"View/firstDayOfWeek"_s, day);
        Q_EMIT firstDayOfWeekChanged();
    }
}

bool Backend::showWeekNumbers() const
{
    return QSettings().value(u"View/weekNumbers"_s, false).toBool();
}

void Backend::setShowWeekNumbers(bool show)
{
    if (show != showWeekNumbers()) {
        QSettings().setValue(u"View/weekNumbers"_s, show);
        Q_EMIT showWeekNumbersChanged();
    }
}

QString Backend::formatTime(const QDateTime &time, bool use24Hour) const
{
    return QLocale().toString(time.time(), timeFormat(use24Hour));
}

QDateTime Backend::parseTime(const QString &text, const QDateTime &day) const
{
    const QString input = text.trimmed();
    const QLocale locale;
    QTime time = locale.toTime(input, QLocale::ShortFormat);
    // As Kalendar itself writes it, whichever way that is set to be.
    for (const bool hours24 : {true, false}) {
        if (!time.isValid()) {
            time = locale.toTime(input, timeFormat(hours24));
        }
    }
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
