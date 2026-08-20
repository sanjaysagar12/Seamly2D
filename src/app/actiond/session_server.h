//---------------------------------------------------------------------------------------------------------------------
//  @file   session_server.h
//  @author Seamly2D Contributors
//  @date   20 Aug, 2026
//
//  @copyright
//  Copyright (C) 2017 - 2026 Seamly, LLC
//  https://github.com/fashionfreedom/seamly2d
//
//  @brief
//  Seamly2D is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  Seamly2D is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with Seamly2D. If not, see <http://www.gnu.org/licenses/>.
//---------------------------------------------------------------------------------------------------------------------

#ifndef SESSION_SERVER_H // Include guard start, prevents this header being processed twice in one translation unit.
#define SESSION_SERVER_H // Marks SESSION_SERVER_H as defined for the remainder of the include guard.

class PatternSession; // Forward declaration; only a reference to it is passed to run().
class QIODevice;       // Forward declaration; only pointers to it are passed to run().

// SessionServer implements the NDJSON request/response protocol loop that turns a single
// PatternSession into a persistent daemon: read one JSON line, dispatch its "actions" batch
// through the session, write one JSON line back, repeat until EOF or a "session.close" action is
// reached. Takes QIODevice* (not literally stdin/stdout) so it can be driven by any device --
// actiond's main.cpp wires it to real stdin/stdout, but the same class can be pointed at an
// in-memory buffer for a standalone test.
//
// Protocol (one line in, one line out, both single-line compact JSON, no embedded newlines):
//   in:  {"id"?: "...", "onError"?: "abort"|"continue", "actions": [{"op": "...", ...}, ...]}
//   out: {"id": <echoed, or null>, "status": "ok"|"partial"|"error", "appliedCount": <int>,
//         "results": [{"op","index","status":"ok"|"error","result","error"}, ...], "error": <null|"...">}
// A JSON-parse failure or a request missing/mistyped "actions" produces a top-level
// {"id":null-or-echoed,"status":"error",...,"error":"..."} response instead of exiting or
// crashing. Every response is written as one line and flushed immediately, since the caller is
// blocking on a readline.
namespace SessionServer
{
    // Runs the read/dispatch/write loop against session until input reaches EOF or a
    // "session.close" action is processed (that batch's response is still written first). Never
    // throws: every exception a handler/PatternSession::runActions() could still let escape is
    // caught here and turned into a top-level error response for that one request, so one bad
    // line never brings down the whole persistent process.
    void run(PatternSession &session, QIODevice *input, QIODevice *output);
}

#endif // SESSION_SERVER_H // End of include guard started above.
