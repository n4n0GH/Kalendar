// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine.h"

#include <CalendarEvents/CalendarEventsPlugin>

#include <QPluginLoader>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using CalendarEvents::EventData;

/// The plugin for Plasma's clock, loaded the way Plasma loads it, next to a
/// Kalendar that changes the calendar meanwhile.
class PlasmaPluginTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private Q_SLOTS:
    void showsWhatKalendarHas()
    {
        const QString database = m_dir.filePath(u"calendars.sqlite"_s);
        qputenv("KALENDAR_DATABASE", database.toUtf8());

        QPluginLoader loader(QStringLiteral(PLUGIN_FILE));
        QVERIFY2(loader.load(), qPrintable(loader.errorString()));
        QCOMPARE(loader.metaData().value("IID"_L1).toString(), u"org.kde.CalendarEventsPlugin"_s);
        QCOMPARE(loader.metaData().value("MetaData"_L1).toObject().value("KPlugin"_L1).toObject().value("Name"_L1).toString(), u"Kalendar"_s);
        auto *plugin = qobject_cast<CalendarEvents::CalendarEventsPlugin *>(loader.instance());
        QVERIFY(plugin);

        QMultiHash<QDate, EventData> shown;
        QStringList removed;
        connect(plugin, &CalendarEvents::CalendarEventsPlugin::dataReady, this, [&](const QMultiHash<QDate, EventData> &data) {
            shown.unite(data);
        });
        connect(plugin, &CalendarEvents::CalendarEventsPlugin::eventRemoved, this, [&](const QString &uid) {
            removed.append(uid);
            shown.removeIf([&](const auto &entry) {
                return entry.value().uid() == uid;
            });
        });

        // Before Kalendar was ever started there is nothing, and nothing goes wrong.
        const QDate first(2026, 11, 1);
        const QDate last(2026, 11, 30);
        plugin->loadEventsForDateRange(first, last);
        QVERIFY(shown.isEmpty());
        QVERIFY(!QFile::exists(database));

        // Kalendar starts and gets three events.
        Engine kalendar;
        kalendar.setUndoPeriod(0);
        QVERIFY(kalendar.open(database));
        const QString calendar = kalendar.collections().first().id;
        const QDateTime lunch(QDate(2026, 11, 12), QTime(12, 0));
        QCOMPARE(kalendar.saveEvent({{u"collectionId"_s, calendar}, {u"summary"_s, u"Lunch"_s}, {u"location"_s, u"Café"_s}, {u"start"_s, lunch},
                                     {u"end"_s, lunch.addSecs(3600)}, {u"repeat"_s, u"weekly"_s}}),
                 QString());
        const QDateTime trip(QDate(2026, 11, 20), QTime(0, 0));
        QCOMPARE(kalendar.saveEvent({{u"collectionId"_s, calendar}, {u"summary"_s, u"Trip"_s}, {u"start"_s, trip}, {u"end"_s, trip.addDays(2)},
                                     {u"allDay"_s, true}, {u"repeat"_s, u"none"_s}}),
                 QString());

        // The plugin notices by itself.
        QTRY_COMPARE_WITH_TIMEOUT(shown.size(), 3 + 3, 5000); // three lunches, and a trip on three days
        const QList<EventData> onThe12th = shown.values(QDate(2026, 11, 12));
        QCOMPARE(onThe12th.size(), 1);
        QCOMPARE(onThe12th.first().title(), u"Lunch"_s);
        QCOMPARE(onThe12th.first().location(), u"Café"_s);
        QCOMPARE(onThe12th.first().startDateTime(), lunch);
        QCOMPARE(onThe12th.first().endDateTime(), lunch.addSecs(3600));
        QVERIFY(!onThe12th.first().isAllDay());
        QCOMPARE(onThe12th.first().type(), EventData::Event);
        QCOMPARE(QColor(onThe12th.first().eventColor()), kalendar.collections().first().color);
        QVERIFY(shown.contains(QDate(2026, 11, 19)));
        QVERIFY(shown.contains(QDate(2026, 11, 26)));
        for (int day = 20; day <= 22; ++day) {
            QCOMPARE(shown.values(QDate(2026, 11, day)).size(), 1);
            QVERIFY(shown.values(QDate(2026, 11, day)).first().isAllDay());
        }
        QVERIFY(!shown.contains(QDate(2026, 11, 23)));

        // Another month is asked for: only that month's events are there then.
        plugin->loadEventsForDateRange(QDate(2026, 12, 1), QDate(2026, 12, 31));
        QCOMPARE(shown.size(), 5); // the Thursdays of December
        QVERIFY(shown.contains(QDate(2026, 12, 31)));
        plugin->loadEventsForDateRange(first, last);
        QCOMPARE(shown.size(), 6);

        // The trip is deleted in Kalendar.
        const QVariantList segments = kalendar.weekLayout(QDate(2026, 11, 20), 1, true, 0).value(u"segments"_s).toList();
        QCOMPARE(segments.size(), 1);
        QVERIFY(kalendar.deleteEvent(calendar, segments.first().toMap().value(u"href"_s).toString(), {}, true) != 0);
        QTRY_COMPARE_WITH_TIMEOUT(shown.size(), 3, 5000);
        QVERIFY(!shown.contains(QDate(2026, 11, 21)));

        // A calendar that is hidden in Kalendar is hidden here too.
        kalendar.setCollectionEnabled(calendar, false);
        QTRY_COMPARE_WITH_TIMEOUT(shown.size(), 0, 5000);

        // Kalendar's database was only read: the same file as Kalendar left it.
        QVERIFY(kalendar.pendingChanges() == 0);
        kalendar.setCollectionEnabled(calendar, true);
        QTRY_COMPARE_WITH_TIMEOUT(shown.size(), 3, 5000);
    }
};

QTEST_GUILESS_MAIN(PlasmaPluginTest)

#include "plasmaplugintest.moc"
