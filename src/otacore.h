// Semantic version parse/compare and OTA manifest parse.
//
// Pure C++17 plus ArduinoJson; no Arduino.h, so it builds and tests on the host.
// Versions are strictly MAJOR.MINOR.PATCH with no leading zeros, no prefix and
// no suffix; anything else is a parse failure rather than a guess.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic Claude Opus 5).

#pragma once

#include <cstddef>
#include <string>

namespace otacore {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

// Returns false (and leaves out untouched) unless s is exactly M.m.p.
bool parseVersion(const char* s, Version& out);

// -1 if a < b, 0 if equal, 1 if a > b.
int compareVersions(const Version& a, const Version& b);

struct Manifest {
    std::string version;   // validated with parseVersion
    std::string url;       // non-empty
    size_t      size = 0;  // > 0
    std::string md5;       // 32 hex chars, lowercased
};

// All four fields are required. On failure error holds one of:
//   "manifest: not json", "manifest: missing <field>", "manifest: bad version",
//   "manifest: bad url", "manifest: bad size", "manifest: bad md5".
bool parseManifest(const char* json, size_t len, Manifest& out, std::string& error);

}  // namespace otacore
