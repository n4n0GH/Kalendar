// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "davclient.h"

#include <KLocalizedString>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QXmlStreamReader>

#include <memory>

using namespace Qt::StringLiterals;

namespace
{
using Headers = QList<QPair<QByteArray, QByteArray>>;

const QByteArray FindAccountBody = QByteArrayLiteral(
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop>"
    "<d:current-user-principal/><c:calendar-home-set/><d:resourcetype/>"
    "</d:prop></d:propfind>");

const QByteArray FindCalendarsBody = QByteArrayLiteral(
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" xmlns:cs=\"http://calendarserver.org/ns/\" "
    "xmlns:a=\"http://apple.com/ns/ical/\"><d:prop>"
    "<d:resourcetype/><d:displayname/><a:calendar-color/><cs:getctag/>"
    "<c:supported-calendar-component-set/><d:current-user-privilege-set/>"
    "</d:prop></d:propfind>");

const QByteArray FindEtagBody = QByteArrayLiteral(
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:getetag/></d:prop></d:propfind>");

const QByteArray ListEventsBody = QByteArrayLiteral(
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
    "<d:prop><d:getetag/></d:prop>"
    "<c:filter><c:comp-filter name=\"VCALENDAR\"><c:comp-filter name=\"VEVENT\"/></c:comp-filter></c:filter>"
    "</c:calendar-query>");

/// How many events are asked for in one request.
constexpr int FetchBatch = 50;

DavProp readProp(QXmlStreamReader &xml)
{
    DavProp prop;
    int depth = 1;
    while (depth > 0 && !xml.atEnd()) {
        switch (xml.readNext()) {
        case QXmlStreamReader::StartElement: {
            const QString name = xml.name().toString();
            prop.children.insert(name);
            if (name == "href"_L1) {
                // Reads up to and including the closing tag.
                prop.hrefs.append(xml.readElementText().trimmed());
            } else {
                if (name == "comp"_L1) {
                    prop.components.append(xml.attributes().value("name"_L1).toString().toUpper());
                }
                ++depth;
            }
            break;
        }
        case QXmlStreamReader::EndElement:
            --depth;
            break;
        case QXmlStreamReader::Characters:
            if (depth == 1) {
                prop.text += xml.text();
            }
            break;
        default:
            break;
        }
    }
    return prop;
}

int statusOf(const QString &line)
{
    // "HTTP/1.1 200 OK"
    const QStringList parts = line.simplified().split(u' ');
    return parts.size() >= 2 ? parts.at(1).toInt() : 0;
}

void readPropstat(QXmlStreamReader &xml, DavResponse &response)
{
    QHash<QString, DavProp> props;
    int status = 200;
    while (xml.readNextStartElement()) {
        if (xml.name() == "status"_L1) {
            status = statusOf(xml.readElementText());
        } else if (xml.name() == "prop"_L1) {
            while (xml.readNextStartElement()) {
                const QString name = xml.name().toString();
                props.insert(name, readProp(xml));
            }
        } else {
            xml.skipCurrentElement();
        }
    }
    if (status >= 200 && status < 300) {
        response.props.insert(props);
    }
}

DavResponse readResponse(QXmlStreamReader &xml)
{
    DavResponse response;
    while (xml.readNextStartElement()) {
        if (xml.name() == "href"_L1) {
            response.href = xml.readElementText().trimmed();
        } else if (xml.name() == "status"_L1) {
            response.status = statusOf(xml.readElementText());
        } else if (xml.name() == "propstat"_L1) {
            readPropstat(xml, response);
        } else {
            xml.skipCurrentElement();
        }
    }
    return response;
}

/// The last two parts of a host name, which is as close to "the same
/// provider" as can be told without a list of public suffixes.
QString siteOf(const QString &host)
{
    const QStringList parts = host.toLower().split(u'.');
    bool isNumber = false;
    parts.constLast().toInt(&isNumber);
    if (parts.size() <= 2 || isNumber) {
        return host.toLower();
    }
    return parts.at(parts.size() - 2) + u'.' + parts.constLast();
}

QString describe(QNetworkReply *reply, int status)
{
    switch (status) {
    case 0:
        break;
    case 401:
        return i18nc("@info", "The user name or password was not accepted.");
    case 403:
        return i18nc("@info", "The server refused access.");
    case 404:
    case 405:
        return i18nc("@info", "No calendars were found at this address.");
    case 429:
        return i18nc("@info", "The server is busy. Try again in a little while.");
    default:
        if (status >= 500) {
            return i18nc("@info", "The server reported a problem (error %1). Try again later.", status);
        }
        return i18nc("@info", "The server gave an unexpected answer (error %1).", status);
    }
    switch (reply->error()) {
    case QNetworkReply::HostNotFoundError:
        return i18nc("@info", "The server “%1” could not be found. Check the address and the network connection.", reply->url().host());
    case QNetworkReply::SslHandshakeFailedError:
        return i18nc("@info", "No secure connection to “%1” could be made.", reply->url().host());
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        return i18nc("@info", "The server “%1” did not answer in time.", reply->url().host());
    default:
        return i18nc("@info", "The server “%1” could not be reached. Check the network connection.", reply->url().host());
    }
}

QColor readColor(QString text)
{
    text = text.trimmed();
    // Apple's notation puts the opacity last (#RRGGBBAA), where Qt expects it first.
    if (text.size() == 9 && text.startsWith(u'#')) {
        text.truncate(7);
    }
    return QColor::fromString(text);
}
}

DavClient::DavClient(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_network(network)
{
}

void DavClient::setAuthorization(const QByteArray &header)
{
    m_authorization = header;
}

QList<DavResponse> DavClient::parseMultiStatus(const QByteArray &data)
{
    QList<DavResponse> responses;
    QXmlStreamReader xml(data);
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == "response"_L1) {
            responses.append(readResponse(xml));
        }
    }
    return responses;
}

QString DavClient::absolute(const QUrl &base, const QString &href)
{
    return base.resolved(QUrl(href)).toString(QUrl::FullyEncoded);
}

void DavClient::send(const QByteArray &verb, const QUrl &url, const Headers &headers, const QByteArray &body, const Callback &done, int redirectsLeft)
{
    if (m_authorizedHost.isEmpty()) {
        m_authorizedHost = url.host();
    }
    QNetworkRequest request(url);
    // Qt would turn a redirected PROPFIND into a GET, so they are followed by hand.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    request.setRawHeader("User-Agent", "Kalendar");
    // Servers point to other hosts of theirs, but the password is not for strangers.
    if (!m_authorization.isEmpty() && siteOf(url.host()) == siteOf(m_authorizedHost)) {
        request.setRawHeader("Authorization", m_authorization);
    }
    for (const auto &header : headers) {
        request.setRawHeader(header.first, header.second);
    }
    QNetworkReply *reply = m_network->sendCustomRequest(request, verb, body);
    connect(reply, &QNetworkReply::finished, this, [=, this] {
        finished(reply, verb, headers, body, done, redirectsLeft);
    });
}

void DavClient::finished(QNetworkReply *reply, const QByteArray &verb, const Headers &headers, const QByteArray &body, const Callback &done, int redirectsLeft)
{
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 301 && status <= 308 && status != 304 && redirectsLeft > 0) {
        const QUrl target = reply->url().resolved(QUrl::fromEncoded(reply->rawHeader("Location")));
        const bool lessSafe = reply->url().scheme() == "https"_L1 && target.scheme() != "https"_L1;
        if (target.isValid() && !lessSafe) {
            send(verb, target, headers, body, done, redirectsLeft - 1);
            return;
        }
    }

    DavReply result;
    result.status = status;
    result.url = reply->url();
    result.ok = status >= 200 && status < 300;
    result.etag = QString::fromLatin1(reply->rawHeader("ETag"));
    if (!result.ok) {
        result.error = describe(reply, status);
    } else if (status == 207) {
        result.responses = parseMultiStatus(reply->readAll());
    }
    done(result);
}

void DavClient::propfind(const QUrl &url, int depth, const QByteArray &body, const Callback &done)
{
    send("PROPFIND", url, {{"Depth", QByteArray::number(depth)}, {"Content-Type", "application/xml; charset=utf-8"}}, body, done);
}

void DavClient::discover(const QUrl &address, const std::function<void(const Discovery &)> &done)
{
    QUrl first = address;
    if (first.path().isEmpty()) {
        first.setPath(u"/"_s);
    }
    QUrl wellKnown = first;
    wellKnown.setPath(u"/.well-known/caldav"_s);
    QList<QUrl> candidates{first};
    if (wellKnown != first) {
        // A server's name alone is the common case, so the standard place is asked first.
        if (first.path() == "/"_L1) {
            candidates.prepend(wellKnown);
        } else {
            candidates.append(wellKnown);
        }
    }
    discoverFrom(candidates, {}, done);
}

void DavClient::discoverFrom(const QList<QUrl> &candidates, const QString &firstError, const std::function<void(const Discovery &)> &done)
{
    if (candidates.isEmpty()) {
        Discovery result;
        result.error = firstError.isEmpty() ? i18nc("@info", "No calendars were found at this address.") : firstError;
        done(result);
        return;
    }
    const QList<QUrl> rest = candidates.mid(1);
    propfind(candidates.first(), 0, FindAccountBody, [=, this](const DavReply &reply) {
        if (reply.status == 401) {
            Discovery result;
            result.error = reply.error;
            done(result);
            return;
        }
        // A wrong address is most likely the one that was typed, so its error is the one to show.
        const auto next = [=, this] {
            const bool wasTyped = rest.isEmpty() || !candidates.first().path().startsWith("/.well-known"_L1);
            discoverFrom(rest, wasTyped && !reply.ok && firstError.isEmpty() ? reply.error : firstError, done);
        };
        if (!reply.ok || reply.responses.isEmpty()) {
            next();
            return;
        }
        const QHash<QString, DavProp> props = reply.responses.first().props;
        const QStringList homes = props.value(u"calendar-home-set"_s).hrefs;
        const QStringList principals = props.value(u"current-user-principal"_s).hrefs;
        if (!homes.isEmpty()) {
            discoverHome(reply.url.resolved(QUrl(homes.first())), done);
        } else if (!principals.isEmpty()) {
            const QUrl principal = reply.url.resolved(QUrl(principals.first()));
            propfind(principal, 0, FindAccountBody, [=, this](const DavReply &reply) {
                const QStringList homes = reply.responses.isEmpty() ? QStringList() : reply.responses.first().props.value(u"calendar-home-set"_s).hrefs;
                if (homes.isEmpty()) {
                    next();
                } else {
                    discoverHome(reply.url.resolved(QUrl(homes.first())), done);
                }
            });
        } else if (props.value(u"resourcetype"_s).children.contains(u"calendar"_s)) {
            // The address of one calendar: its siblings are wanted too.
            discoverHome(reply.url.resolved(QUrl(reply.url.path().endsWith(u'/') ? u".."_s : u"."_s)), done);
        } else {
            next();
        }
    });
}

void DavClient::discoverHome(const QUrl &home, const std::function<void(const Discovery &)> &done)
{
    listCalendars(home, [=](const DavReply &reply, const QList<CalendarInfo> &calendars) {
        Discovery result;
        result.home = home;
        result.calendars = calendars;
        if (!reply.ok) {
            result.error = reply.error;
        } else if (calendars.isEmpty()) {
            result.error = i18nc("@info", "The account has no calendars.");
        }
        done(result);
    });
}

void DavClient::listCalendars(const QUrl &home, const std::function<void(const DavReply &, const QList<CalendarInfo> &)> &done)
{
    propfind(home, 1, FindCalendarsBody, [=](const DavReply &reply) {
        QList<CalendarInfo> calendars;
        for (const DavResponse &response : reply.responses) {
            if (!response.props.value(u"resourcetype"_s).children.contains(u"calendar"_s)) {
                continue;
            }
            const QStringList components = response.props.value(u"supported-calendar-component-set"_s).components;
            if (!components.isEmpty() && !components.contains(u"VEVENT"_s)) {
                continue; // a task list or a journal
            }
            CalendarInfo info;
            info.url = reply.url.resolved(QUrl(response.href));
            info.name = response.props.value(u"displayname"_s).text.trimmed();
            if (info.name.isEmpty()) {
                info.name = info.url.path().section(u'/', -1, -1, QString::SectionSkipEmpty);
            }
            info.color = readColor(response.props.value(u"calendar-color"_s).text);
            info.ctag = response.props.value(u"getctag"_s).text.trimmed();
            if (response.props.contains(u"current-user-privilege-set"_s)) {
                const QSet<QString> privileges = response.props.value(u"current-user-privilege-set"_s).children;
                info.readOnly = !privileges.contains(u"write"_s) && !privileges.contains(u"write-content"_s) && !privileges.contains(u"all"_s);
            }
            calendars.append(info);
        }
        done(reply, calendars);
    });
}

void DavClient::listEtags(const QUrl &collection, const std::function<void(const DavReply &, const QHash<QString, QString> &)> &done)
{
    send("REPORT", collection, {{"Depth", "1"}, {"Content-Type", "application/xml; charset=utf-8"}}, ListEventsBody, [=](const DavReply &reply) {
        QHash<QString, QString> etags;
        for (const DavResponse &response : reply.responses) {
            if (response.props.contains(u"getetag"_s)) {
                etags.insert(absolute(reply.url, response.href), response.props.value(u"getetag"_s).text.trimmed());
            }
        }
        done(reply, etags);
    });
}

void DavClient::fetch(const QUrl &collection, const QStringList &hrefs, const std::function<void(const DavReply &, const QList<FetchedItem> &)> &done)
{
    struct State {
        QStringList remaining;
        QList<FetchedItem> items;
        std::function<void()> next;
    };
    const auto state = std::make_shared<State>();
    state->remaining = hrefs;
    state->next = [=, this] {
        if (state->remaining.isEmpty()) {
            DavReply reply;
            reply.ok = true;
            // The state holds this function, which holds the state: taking it out
            // lets both go once this call is over, and not while it still runs.
            const auto self = std::move(state->next);
            state->next = nullptr;
            done(reply, state->items);
            return;
        }
        QByteArray body = QByteArrayLiteral(
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
            "<c:calendar-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
            "<d:prop><d:getetag/><c:calendar-data/></d:prop>");
        const QStringList batch = state->remaining.mid(0, FetchBatch);
        state->remaining = state->remaining.mid(FetchBatch);
        for (const QString &href : batch) {
            body += "<d:href>" + QUrl(href).path(QUrl::FullyEncoded).toHtmlEscaped().toUtf8() + "</d:href>";
        }
        body += "</c:calendar-multiget>";
        send("REPORT", collection, {{"Depth", "1"}, {"Content-Type", "application/xml; charset=utf-8"}}, body, [=](const DavReply &reply) {
            if (!reply.ok) {
                state->next = nullptr;
                done(reply, {});
                return;
            }
            for (const DavResponse &response : reply.responses) {
                if (!response.props.contains(u"calendar-data"_s)) {
                    continue;
                }
                FetchedItem item;
                item.href = absolute(reply.url, response.href);
                item.etag = response.props.value(u"getetag"_s).text.trimmed();
                item.ics = response.props.value(u"calendar-data"_s).text.toUtf8();
                state->items.append(item);
            }
            state->next();
        });
    };
    state->next();
}

void DavClient::put(const QUrl &url, const QByteArray &ics, const QString &etag, const Callback &done)
{
    Headers headers{{"Content-Type", "text/calendar; charset=utf-8"}};
    if (etag.isEmpty()) {
        headers.emplaceBack("If-None-Match", "*");
    } else {
        headers.emplaceBack("If-Match", etag.toLatin1());
    }
    send("PUT", url, headers, ics, done);
}

void DavClient::remove(const QUrl &url, const QString &etag, const Callback &done)
{
    Headers headers;
    if (!etag.isEmpty()) {
        headers.emplaceBack("If-Match", etag.toLatin1());
    }
    send("DELETE", url, headers, {}, done);
}

void DavClient::etagOf(const QUrl &url, const Callback &done)
{
    propfind(url, 0, FindEtagBody, [=](DavReply reply) {
        reply.etag = reply.responses.isEmpty() ? QString() : reply.responses.first().props.value(u"getetag"_s).text.trimmed();
        done(reply);
    });
}
