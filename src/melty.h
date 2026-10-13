// Miles Together - Melty's files (sheet rows systems/melty_files and systems/test_mode).
#pragma once

#include "common.h"

namespace mt::melty {

// Creates the mod's folder, empties melty.log and reads join= from melty.properties.
void Init();

// The host's address when this player is joining; empty when hosting.
const std::string& JoinAddress();

// One line each, as Melty reads them.
void WriteHosting(const std::string& address);
void WriteJoined(const std::string& address);
void WriteJoinFailed(const std::string& reason);

// True while Melty is running a test of this mashup.
bool TestMode();

// Developer switch: a file named mirror.txt in the mod's folder shows a mirrored double when alone.
bool MirrorRequested();

// Reports a named check once: "pass <name>" or "fail <name>: <why>".
void ReportPass(const char* check);
void ReportFail(const char* check, const char* why);

}  // namespace mt::melty
