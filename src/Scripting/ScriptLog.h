#pragma once
#include <cstdint>
#include <deque>
#include <string>

namespace Scripting
{
/*
The messages scripts produce, kept for the editor's Output tab: what Lua's print writes (error = false) and the errors Scripting.cpp reports (error = true, with the traceback). Every message is also still written
to stdout / stderr as before (the replay tools read the REPLAY lines from stdout). Only the newest MaxLines are kept; total counts every message ever added, so a reader can tell when something new arrived.
*/
struct LogLine
{
	bool error = false;
	std::string text;
};

constexpr size_t MaxLines = 2000;

inline std::deque<LogLine> logLines;
inline uint64_t logTotal = 0;

inline void addLog(bool error_, std::string text_)
{
	logLines.push_back({error_, std::move(text_)});
	if(logLines.size() > MaxLines) logLines.pop_front();
	logTotal++;
}
}
