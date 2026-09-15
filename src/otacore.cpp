// See otacore.h.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic Claude Opus 5).

#include "otacore.h"

#include <ArduinoJson.h>

#include <cctype>
#include <cstring>

namespace otacore {
namespace {

// Parses one decimal component. Accepts "0" or a digit string not starting
// with 0. Advances p past the digits. Returns false on anything else.
bool parseComponent(const char*& p, int& out) {
    if (!std::isdigit(static_cast<unsigned char>(*p))) return false;
    if (*p == '0' && std::isdigit(static_cast<unsigned char>(p[1]))) return false;
    long v = 0;
    while (std::isdigit(static_cast<unsigned char>(*p))) {
        v = v * 10 + (*p - '0');
        if (v > 1000000) return false;
        ++p;
    }
    out = static_cast<int>(v);
    return true;
}

bool isHex32(const std::string& s) {
    if (s.size() != 32) return false;
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

}  // namespace

bool parseVersion(const char* s, Version& out) {
    if (s == nullptr) return false;
    const char* p = s;
    Version v;
    if (!parseComponent(p, v.major)) return false;
    if (*p++ != '.') return false;
    if (!parseComponent(p, v.minor)) return false;
    if (*p++ != '.') return false;
    if (!parseComponent(p, v.patch)) return false;
    if (*p != '\0') return false;
    out = v;
    return true;
}

int compareVersions(const Version& a, const Version& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

bool parseManifest(const char* json, size_t len, Manifest& out, std::string& error) {
    error.clear();
    JsonDocument doc;
    const DeserializationError derr = deserializeJson(doc, json, len);
    if (derr || !doc.is<JsonObject>()) {
        error = "manifest: not json";
        return false;
    }

    static const char* const kFields[] = {"version", "url", "size", "md5"};
    for (const char* f : kFields) {
        if (doc[f].isNull()) {
            error = std::string("manifest: missing ") + f;
            return false;
        }
    }

    Manifest m;

    const char* ver = doc["version"].as<const char*>();
    Version parsed;
    if (ver == nullptr || !parseVersion(ver, parsed)) {
        error = "manifest: bad version";
        return false;
    }
    m.version = ver;

    const char* url = doc["url"].as<const char*>();
    if (url == nullptr || *url == '\0') {
        error = "manifest: bad url";
        return false;
    }
    m.url = url;

    if (!doc["size"].is<unsigned long>() || doc["size"].as<unsigned long>() == 0) {
        error = "manifest: bad size";
        return false;
    }
    m.size = static_cast<size_t>(doc["size"].as<unsigned long>());

    const char* md5 = doc["md5"].as<const char*>();
    if (md5 == nullptr) {
        error = "manifest: bad md5";
        return false;
    }
    m.md5 = md5;
    for (char& c : m.md5) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!isHex32(m.md5)) {
        error = "manifest: bad md5";
        return false;
    }

    out = m;
    return true;
}

}  // namespace otacore
