// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "googleauth.h"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

namespace
{
const QString AuthorizeAddress = u"https://accounts.google.com/o/oauth2/v2/auth"_s;
const QString TokenAddress = u"https://oauth2.googleapis.com/token"_s;
// Calendars, and the account's address to name the account by.
const QString Scope = u"https://www.googleapis.com/auth/calendar openid email"_s;
constexpr int SignInMinutes = 5;

QString randomText(int bytes)
{
    QByteArray data(bytes, Qt::Uninitialized);
    QRandomGenerator::system()->generate(data.begin(), data.end());
    return QString::fromLatin1(data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

GoogleAuth::Result failed(const QString &error)
{
    GoogleAuth::Result result;
    result.error = error;
    return result;
}

QString page(const QString &text)
{
    return u"<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Kalendar</title></head>"
           "<body style=\"font-family: sans-serif; text-align: center; margin-top: 4em\"><p>%1</p></body></html>"_s.arg(text.toHtmlEscaped());
}
}

GoogleAuth::GoogleAuth(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_network(network)
{
}

QUrl GoogleAuth::calDavAddress(const QString &email)
{
    return QUrl(u"https://apidata.googleusercontent.com/caldav/v2/"_s + QString::fromLatin1(QUrl::toPercentEncoding(email)) + u"/user"_s);
}

void GoogleAuth::cancel()
{
    if (m_server) {
        m_server->deleteLater();
        m_server.clear();
    }
    if (m_timeout) {
        m_timeout->deleteLater();
        m_timeout.clear();
    }
}

void GoogleAuth::signIn(const QString &clientId, const QString &clientSecret, const Callback &done)
{
    cancel();
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost)) {
        cancel();
        done(failed(i18nc("@info", "Signing in could not be started on this system.")));
        return;
    }
    const QString redirect = u"http://127.0.0.1:%1"_s.arg(m_server->serverPort());
    const QString verifier = randomText(48);
    const QString state = randomText(16);
    const QByteArray challenge = QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)
                                     .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    QUrlQuery query;
    query.addQueryItem(u"client_id"_s, clientId);
    query.addQueryItem(u"redirect_uri"_s, redirect);
    query.addQueryItem(u"response_type"_s, u"code"_s);
    query.addQueryItem(u"scope"_s, Scope);
    query.addQueryItem(u"code_challenge"_s, QString::fromLatin1(challenge));
    query.addQueryItem(u"code_challenge_method"_s, u"S256"_s);
    query.addQueryItem(u"state"_s, state);
    // Asks for a token that keeps working after the first hour.
    query.addQueryItem(u"access_type"_s, u"offline"_s);
    query.addQueryItem(u"prompt"_s, u"consent"_s);
    QUrl address(AuthorizeAddress);
    address.setQuery(query.toString(QUrl::FullyEncoded));

    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [=, this] {
        cancel();
        done(failed(i18nc("@info", "Nobody signed in within %1 minutes. Try again.", SignInMinutes)));
    });
    m_timeout->start(SignInMinutes * 60 * 1000);

    connect(m_server, &QTcpServer::newConnection, this, [=, this] {
        QTcpSocket *socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [=, this] {
            if (!socket->canReadLine()) {
                return;
            }
            // "GET /?code=…&state=… HTTP/1.1"
            const QList<QByteArray> requestLine = socket->readLine().split(' ');
            const QUrlQuery answer(QUrl::fromEncoded(requestLine.value(1)).query());
            const bool ours = m_server && answer.queryItemValue(u"state"_s) == state;
            const QString code = answer.queryItemValue(u"code"_s, QUrl::FullyDecoded);
            const bool refused = answer.hasQueryItem(u"error"_s);
            if (!ours || (code.isEmpty() && !refused)) {
                socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
                return; // the browser asking for an icon, or something else entirely
            }
            const QByteArray body = page(refused ? i18nc("@info shown in the browser", "Kalendar was not given access to the account. This page can be closed.")
                                                 : i18nc("@info shown in the browser", "Signed in. This page can be closed; Kalendar continues by itself."))
                                        .toUtf8();
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size())
                          + "\r\n\r\n" + body);
            socket->disconnectFromHost();
            cancel();
            if (refused) {
                done(failed(i18nc("@info", "Kalendar was not given access to the Google account.")));
                return;
            }
            requestTokens({{u"grant_type"_s, u"authorization_code"_s},
                           {u"code"_s, code},
                           {u"code_verifier"_s, verifier},
                           {u"redirect_uri"_s, redirect},
                           {u"client_id"_s, clientId},
                           {u"client_secret"_s, clientSecret}},
                          [done](Result result) {
                              if (result.error.isEmpty() && (result.refreshToken.isEmpty() || result.email.isEmpty())) {
                                  result.error = i18nc("@info", "Google's answer was incomplete. Try again.");
                              }
                              done(result);
                          });
        });
    });

    if (!QDesktopServices::openUrl(address)) {
        cancel();
        done(failed(i18nc("@info", "The web browser could not be opened to sign in.")));
    }
}

void GoogleAuth::refresh(const QString &clientId, const QString &clientSecret, const QString &refreshToken, const Callback &done)
{
    requestTokens({{u"grant_type"_s, u"refresh_token"_s}, {u"refresh_token"_s, refreshToken}, {u"client_id"_s, clientId}, {u"client_secret"_s, clientSecret}},
                  done);
}

void GoogleAuth::requestTokens(const QList<QPair<QString, QString>> &form, const Callback &done)
{
    QUrlQuery query;
    for (const auto &field : form) {
        query.addQueryItem(field.first, QString::fromLatin1(QUrl::toPercentEncoding(field.second)));
    }
    QNetworkRequest request{QUrl(TokenAddress)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/x-www-form-urlencoded"_s);
    request.setTransferTimeout(30000);
    QNetworkReply *reply = m_network->post(request, query.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        Result result;
        result.accessToken = json.value("access_token"_L1).toString();
        result.refreshToken = json.value("refresh_token"_L1).toString();
        result.expiresIn = json.value("expires_in"_L1).toInt();
        // The middle part of the identity token says who signed in.
        const QByteArray claims = QByteArray::fromBase64(json.value("id_token"_L1).toString().section(u'.', 1, 1).toLatin1(), QByteArray::Base64UrlEncoding);
        result.email = QJsonDocument::fromJson(claims).object().value("email"_L1).toString();

        const QString problem = json.value("error"_L1).toString();
        if (problem == "invalid_grant"_L1) {
            result.error = i18nc("@info", "Access to the Google account has ended. Remove the account and add it again.");
        } else if (problem == "invalid_client"_L1 || problem == "unauthorized_client"_L1) {
            result.error = i18nc("@info", "Google did not accept the client ID or the client secret.");
        } else if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 0) {
            result.error = i18nc("@info", "Google could not be reached. Check the network connection.");
        } else if (result.accessToken.isEmpty()) {
            result.error = i18nc("@info", "Google did not let Kalendar sign in. Try again later.");
        }
        done(result);
    });
}
