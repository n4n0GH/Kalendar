// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>

#ifdef Q_OS_LINUX
#include <csignal>
#include <sys/prctl.h>
#endif

/// A stand-in for Google's Calendar API (fakegoogle.py) for tests to talk
/// to. It stops when the test does.
class FakeGoogleServer
{
public:
    /// Gives false where there is no Python to run it with.
    bool start(const QString &logFile)
    {
        using namespace Qt::StringLiterals;
        const QString python = QStandardPaths::findExecutable(u"python3"_s);
        if (python.isEmpty()) {
            return false;
        }
        // A port that is free right now.
        QTcpServer picker;
        if (!picker.listen(QHostAddress::LocalHost)) {
            return false;
        }
        const quint16 port = picker.serverPort();
        picker.close();

        m_process.setStandardOutputFile(QProcess::nullDevice());
        m_process.setStandardErrorFile(logFile);
#ifdef Q_OS_LINUX
        // The server must not outlive a test that is stopped from outside.
        m_process.setChildProcessModifier([] {
            prctl(PR_SET_PDEATHSIG, SIGTERM);
        });
#endif
        m_process.start(python, {QFINDTESTDATA("fakegoogle.py"), QString::number(port)});
        if (!m_process.waitForStarted()) {
            return false;
        }
        m_address = QUrl(u"http://127.0.0.1:%1/"_s.arg(port));
        return QTest::qWaitFor([&] {
            QTcpSocket socket;
            socket.connectToHost(QHostAddress::LocalHost, port);
            return socket.waitForConnected(200);
        }, 15000);
    }

    ~FakeGoogleServer()
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.terminate();
            m_process.waitForFinished(5000);
        }
    }

    /// Where its Calendar API is.
    QUrl api() const
    {
        return m_address.resolved(QUrl(QStringLiteral("calendar/v3/")));
    }

    /// Asks the server for something, as another program would. @p path is
    /// relative to the server, such as "control/…" or "calendar/v3/…".
    QJsonObject request(const QByteArray &verb, const QString &path, const QJsonObject &body = {}, int *status = nullptr) const
    {
        QNetworkAccessManager network;
        QNetworkRequest request(QUrl(m_address.toString() + path));
        request.setRawHeader("Authorization", "Bearer test-token-1");
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QNetworkReply *reply = network.sendCustomRequest(request, verb, body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson());
        QSignalSpy finished(reply, &QNetworkReply::finished);
        finished.wait(10000);
        if (status) {
            *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        }
        const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        return answer;
    }

private:
    QProcess m_process;
    QUrl m_address;
};
