// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "davclient.h"
#include "davsamples.h"

#include <QTest>

using namespace Qt::StringLiterals;

class DavParserTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void calendarList()
    {
        const QByteArray xml = Samples::calendarList;
        const QList<DavResponse> responses = DavClient::parseMultiStatus(xml);
        QCOMPARE(responses.size(), 2);
        QVERIFY(!responses.at(0).props.contains(u"displayname"_s));
        QVERIFY(!responses.at(0).props.value(u"resourcetype"_s).children.contains(u"calendar"_s));

        const DavResponse &work = responses.at(1);
        QCOMPARE(work.href, u"/remote.php/dav/calendars/ada/work%20stuff/"_s);
        QVERIFY(work.props.value(u"resourcetype"_s).children.contains(u"calendar"_s));
        QCOMPARE(work.props.value(u"displayname"_s).text, u"Work & Stuff"_s);
        QCOMPARE(work.props.value(u"calendar-color"_s).text, u"#F64F00FF"_s);
        QCOMPARE(work.props.value(u"getctag"_s).text, u"http://sabre.io/ns/sync/42"_s);
        QCOMPARE(work.props.value(u"supported-calendar-component-set"_s).components, QStringList({u"VEVENT"_s, u"VTODO"_s}));
        QVERIFY(work.props.value(u"current-user-privilege-set"_s).children.contains(u"write"_s));
    }

    void principalAndData()
    {
        const QByteArray xml = Samples::principalAndData;
        const QList<DavResponse> responses = DavClient::parseMultiStatus(xml);
        QCOMPARE(responses.size(), 3);
        QCOMPARE(responses.at(0).props.value(u"current-user-principal"_s).hrefs, QStringList{u"/principals/ada/"_s});
        QCOMPARE(responses.at(0).props.value(u"calendar-home-set"_s).hrefs, QStringList{u"https://other.example.org/cal/ada/"_s});
        QCOMPARE(responses.at(1).props.value(u"getetag"_s).text, u"\"abc\""_s);
        QCOMPARE(responses.at(1).props.value(u"calendar-data"_s).text, u"BEGIN:VCALENDAR\nEND:VCALENDAR\n"_s);
        QCOMPARE(responses.at(2).status, 404);
        QVERIFY(responses.at(2).props.isEmpty());
    }

    void addresses()
    {
        const QUrl base(u"https://example.org/cal/ada/work/"_s);
        QCOMPARE(DavClient::absolute(base, u"/cal/ada/work/a%20b.ics"_s), u"https://example.org/cal/ada/work/a%20b.ics"_s);
        QCOMPARE(DavClient::absolute(base, u"https://example.org/cal/ada/work/c.ics"_s), u"https://example.org/cal/ada/work/c.ics"_s);
        QCOMPARE(DavClient::absolute(base, u"d.ics"_s), u"https://example.org/cal/ada/work/d.ics"_s);
    }
};

QTEST_GUILESS_MAIN(DavParserTest)

#include "davparsertest.moc"
