#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Kalendar contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""A stand-in for Google's Calendar API, with what Kalendar uses of it.

    fakegoogle.py PORT [SECONDS]

Like Google it wants a token ("Bearer test-token-…"), tells versions apart
by etag, refuses changes to calendars that are shared for reading, and
lists the changed appearances of a repeating event as events of their own.
Two things are for tests only: PUT /control/CALENDAR/EVENT stores an event
exactly as given, as if changed elsewhere, and events are listed two at a
time, so that asking for the next page is tried too. With SECONDS, it takes
that long to list events, to look at what Kalendar shows meanwhile.
"""

import json
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, unquote, urlsplit

CALENDARS = {
    "ada@example.org": {"summary": "Ada", "accessRole": "owner", "backgroundColor": "#27ae60"},
    "en.holidays#holiday@group.v.calendar.google.com": {"summary": "Holidays", "accessRole": "reader", "backgroundColor": "#3daee9"},
    "busy@example.org": {"summary": "Somebody", "accessRole": "freeBusyReader", "backgroundColor": "#000000"},
}
EVENTS = {name: {} for name in CALENDARS}
VERSION = [0]
PAGE = 2


def stamp(event):
    VERSION[0] += 1
    event["etag"] = '"%d"' % VERSION[0]
    event["updated"] = "2026-11-01T10:00:%02d.000Z" % (VERSION[0] % 60)
    event.setdefault("status", "confirmed")
    return event


def merge(event, changes):
    for key, value in changes.items():
        if value is None:
            event.pop(key, None)
        elif isinstance(value, dict):
            event[key] = {k: v for k, v in value.items() if v is not None}
        else:
            event[key] = value


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def answer(self, status, body=None):
        data = b"" if body is None else json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def fail(self, status, reason):
        self.answer(status, {"error": {"code": status, "message": reason, "errors": [{"reason": reason}]}})

    def handle_any(self):
        url = urlsplit(self.path)
        query = parse_qs(url.query)
        parts = [unquote(part) for part in url.path.strip("/").split("/")]
        length = int(self.headers.get("Content-Length") or 0)
        body = json.loads(self.rfile.read(length)) if length else {}
        verb = self.command

        if parts[0] == "control" and verb == "PUT":
            EVENTS[parts[1]][parts[2]] = stamp(dict(body, id=parts[2]))
            return self.answer(200, EVENTS[parts[1]][parts[2]])
        if not re.fullmatch(r"Bearer test-token-\d+", self.headers.get("Authorization") or ""):
            return self.fail(401, "authError")
        if parts[:2] != ["calendar", "v3"]:
            return self.fail(404, "notFound")
        parts = parts[2:]

        if parts == ["users", "me", "calendarList"]:
            return self.answer(200, {"items": [dict(value, id=name) for name, value in CALENDARS.items()]})
        if len(parts) < 3 or parts[0] != "calendars" or parts[2] != "events" or parts[1] not in CALENDARS:
            return self.fail(404, "notFound")
        calendar = parts[1]
        events = EVENTS[calendar]
        writable = CALENDARS[calendar]["accessRole"] in ("owner", "writer")

        if len(parts) == 3 and verb == "GET":
            time.sleep(float(sys.argv[2]) if len(sys.argv) > 2 else 0)
            everything = list(events.values())
            first = int(query.get("pageToken", ["0"])[0])
            answer = {"items": everything[first:first + PAGE]}
            if first + PAGE < len(everything):
                answer["nextPageToken"] = str(first + PAGE)
            return self.answer(200, answer)
        if len(parts) == 3 and verb == "POST":
            if not writable:
                return self.fail(403, "forbidden")
            name = body.get("id")
            if not name or not re.fullmatch(r"[0-9a-v]{5,1024}", name):
                return self.fail(400, "invalid")
            if name in events:
                return self.fail(409, "duplicate")
            event = {"id": name}
            merge(event, body)
            events[name] = stamp(event)
            return self.answer(200, events[name])
        if len(parts) != 4:
            return self.fail(404, "notFound")

        name = parts[3]
        event = events.get(name)
        if verb == "GET":
            return self.answer(200, event) if event else self.fail(404, "notFound")
        if not writable:
            return self.fail(403, "forbidden")
        wanted = self.headers.get("If-Match")
        if verb == "PATCH" and event is None:
            # An appearance of a repeating event, changed for the first time.
            found = re.fullmatch(r"(.+)_(\d{8})(T\d{6}Z)?", name)
            if not found or found.group(1) not in events:
                return self.fail(404, "notFound")
            day = "%s-%s-%s" % (found.group(2)[:4], found.group(2)[4:6], found.group(2)[6:])
            event = {"id": name, "recurringEventId": found.group(1), "originalStartTime": {"date": day}}
            events[name] = event
        if event is None:
            return self.fail(410, "deleted")
        if wanted and wanted != event.get("etag"):
            return self.fail(412, "conditionNotMet")
        if verb == "PATCH":
            merge(event, body)
            return self.answer(200, stamp(event))
        if verb == "DELETE":
            del events[name]
            for other in [key for key, value in events.items() if value.get("recurringEventId") == name]:
                del events[other]
            return self.answer(204)
        return self.fail(405, "notAllowed")

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = handle_any


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
