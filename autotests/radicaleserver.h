// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QFile>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>

#ifdef Q_OS_LINUX
#include <csignal>
#include <sys/prctl.h>
#endif

/// A real CalDAV server (Radicale) for tests to talk to, with one user,
/// "ada" with the password "lovelace". It stops when the test does.
class RadicaleServer
{
public:
    /// Starts the server with its files in @p directory. Gives false where
    /// Radicale is not installed; KALENDAR_RADICALE names the Python to run
    /// it with.
    bool start(const QString &directory)
    {
        using namespace Qt::StringLiterals;
        const QString python = qEnvironmentVariable("KALENDAR_RADICALE", u"python3"_s);
        QProcess probe;
        probe.start(python, {u"-c"_s, u"import radicale"_s});
        if (!probe.waitForFinished() || probe.exitCode() != 0) {
            return false;
        }
        // A port that is free right now.
        QTcpServer picker;
        if (!picker.listen(QHostAddress::LocalHost)) {
            return false;
        }
        const quint16 port = picker.serverPort();
        picker.close();

        QFile users(directory + u"/users"_s);
        if (!users.open(QIODevice::WriteOnly)) {
            return false;
        }
        users.write("ada:lovelace\n");
        users.close();
        m_process.setStandardOutputFile(QProcess::nullDevice());
        m_process.setStandardErrorFile(directory + u"/server.log"_s);
#ifdef Q_OS_LINUX
        // The server must not outlive a test that is stopped from outside.
        m_process.setChildProcessModifier([] {
            prctl(PR_SET_PDEATHSIG, SIGTERM);
        });
#endif
        m_process.start(python,
                        {u"-m"_s, u"radicale"_s,
                         u"--server-hosts"_s, u"127.0.0.1:%1"_s.arg(port),
                         u"--storage-filesystem-folder"_s, directory + u"/collections"_s,
                         u"--auth-type"_s, u"htpasswd"_s,
                         u"--auth-htpasswd-filename"_s, users.fileName(),
                         u"--auth-htpasswd-encryption"_s, u"plain"_s,
                         u"--logging-level"_s, u"warning"_s});
        if (!m_process.waitForStarted()) {
            return false;
        }
        m_address = QUrl(u"http://127.0.0.1:%1"_s.arg(port));
        return QTest::qWaitFor([&] {
            QTcpSocket socket;
            socket.connectToHost(QHostAddress::LocalHost, port);
            return socket.waitForConnected(200);
        }, 15000);
    }

    ~RadicaleServer()
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.terminate();
            m_process.waitForFinished(5000);
        }
    }

    QUrl address() const
    {
        return m_address;
    }

private:
    QProcess m_process;
    QUrl m_address;
};
