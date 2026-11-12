// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clockplugin.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

/// Installing the plugin for Plasma's clock for one user.
class ClockPluginTest : public QObject
{
    Q_OBJECT

    static QByteArray read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

private Q_SLOTS:
    void installsRefreshesAndRemoves()
    {
        QTemporaryDir home;
        const QString source = home.filePath(u"shipped.so"_s);
        write(source, "version one");

        ClockPlugin plugin(home.path(), source);
        QSignalSpy changed(&plugin, &ClockPlugin::installedChanged);
        QVERIFY(plugin.isAvailable());
        QVERIFY(!plugin.isSystemWide());
        QVERIFY(!plugin.isInstalled());

        QCOMPARE(plugin.install(), QString());
        QVERIFY(plugin.isInstalled());
        QVERIFY(plugin.isFresh());
        QCOMPARE(changed.size(), 1);
        QCOMPARE(plugin.pluginFile(), home.filePath(u".local/lib/kalendar/plugins/plasmacalendarplugins/kalendarevents.so"_s));
        QCOMPARE(read(plugin.pluginFile()), QByteArray("version one"));
        // Plasma is told at login where to look.
        QCOMPARE(plugin.loginScript(), home.filePath(u".config/plasma-workspace/env/kalendar-clock-plugin.sh"_s));
        QVERIFY(read(plugin.loginScript()).contains("export QT_PLUGIN_PATH=\"$HOME/.local/lib/kalendar/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}\""));

        // A newer Kalendar brings a newer plugin.
        write(source, "version two");
        ClockPlugin later(home.path(), source);
        QVERIFY(later.isInstalled());
        QVERIFY(!later.isFresh());
        later.refresh();
        QCOMPARE(read(later.pluginFile()), QByteArray("version two"));

        QCOMPARE(later.remove(), QString());
        QVERIFY(!later.isInstalled());
        QVERIFY(!QFile::exists(later.pluginFile()));
        QVERIFY(!QFile::exists(later.loginScript()));
        QVERIFY(!QFile::exists(home.filePath(u".local/lib/kalendar"_s)));
        // Nothing to refresh then.
        later.refresh();
        QVERIFY(!QFile::exists(later.pluginFile()));
    }

    void saysWhenThereIsNoPlugin()
    {
        QTemporaryDir home;
        ClockPlugin plugin(home.path(), home.filePath(u"missing.so"_s));
        QVERIFY(!plugin.install().isEmpty());
        QVERIFY(!plugin.isInstalled());
    }
};

QTEST_GUILESS_MAIN(ClockPluginTest)

#include "clockplugintest.moc"
