// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>

/// Sees to it that Kalendar runs once: two of them would each keep the
/// calendars in memory and know nothing of what the other changed.
///
/// The first Kalendar takes its name on the session bus and answers there
/// to what the desktop asks of an application (org.freedesktop.Application).
/// One that is started while it runs hands over the files it was started
/// with and quits.
class SingleInstance : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Application")

public:
    explicit SingleInstance(QObject *parent = nullptr);

    /// Takes the name. If it is taken, hands @p files (paths or addresses,
    /// relative to the current folder) to the Kalendar that has it and
    /// gives false: there is nothing left to do then but quit. @p token is
    /// what the desktop gave this start to bring a window to the front with.
    bool claim(const QStringList &files, const QString &token);

public Q_SLOTS:
    Q_SCRIPTABLE void Activate(const QVariantMap &platformData);
    Q_SCRIPTABLE void Open(const QStringList &uris, const QVariantMap &platformData);
    Q_SCRIPTABLE void ActivateAction(const QString &name, const QVariantList &parameter, const QVariantMap &platformData);

Q_SIGNALS:
    /// The window is to come to the front; @p token allows it to.
    void activated(const QString &token);
    void filesOpened(const QStringList &uris);
};
