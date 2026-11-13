// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "singleinstance.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace
{
const QString Service = u"io.github.n4n0gh.kalendar"_s;
const QString Path = u"/io/github/n4n0gh/kalendar"_s;
const QString Interface = u"org.freedesktop.Application"_s;
const QString Token = u"activation-token"_s;
}

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
}

bool SingleInstance::claim(const QStringList &files, const QString &token)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return true; // no way to tell: better two than none
    }
    // The object first, so that whoever finds the name finds it answering.
    bus.registerObject(Path, this, QDBusConnection::ExportScriptableSlots);
    if (bus.registerService(Service)) {
        return true;
    }
    bus.unregisterObject(Path);

    QStringList uris;
    for (const QString &file : files) {
        uris.append(QUrl::fromUserInput(file, QDir::currentPath(), QUrl::AssumeLocalFile).toString());
    }
    const QVariantMap platformData{{Token, token}};
    QDBusMessage call = QDBusMessage::createMethodCall(Service, Path, Interface, uris.isEmpty() ? u"Activate"_s : u"Open"_s);
    if (uris.isEmpty()) {
        call.setArguments({platformData});
    } else {
        call.setArguments({uris, platformData});
    }
    const QDBusMessage reply = bus.call(call);
    // If the one that has the name does not answer, this one has to do.
    return reply.type() == QDBusMessage::ErrorMessage;
}

void SingleInstance::Activate(const QVariantMap &platformData)
{
    Q_EMIT activated(platformData.value(Token).toString());
}

void SingleInstance::Open(const QStringList &uris, const QVariantMap &platformData)
{
    Q_EMIT activated(platformData.value(Token).toString());
    Q_EMIT filesOpened(uris);
}

void SingleInstance::ActivateAction(const QString &, const QVariantList &, const QVariantMap &platformData)
{
    // The desktop file offers no actions; the window is all there is to show.
    Q_EMIT activated(platformData.value(Token).toString());
}
