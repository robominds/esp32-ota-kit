// Minimal esp32-ota-kit application: WiFi, push and pull OTA, and an observer
// that prints to the serial port. Send 'c' to check the server, 'i' to install.
//
// Real applications pass their own values from secrets.ini through build flags
// (see README.md). These defaults only make the example compile.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic Claude Opus 5).

#include <Arduino.h>
#include <WiFi.h>
#include <ota.h>

#ifndef EXAMPLE_WIFI_SSID
#define EXAMPLE_WIFI_SSID "your-ssid"
#endif
#ifndef EXAMPLE_WIFI_PASS
#define EXAMPLE_WIFI_PASS "your-password"
#endif
#ifndef EXAMPLE_HOSTNAME
#define EXAMPLE_HOSTNAME "ota-example"
#endif
#ifndef EXAMPLE_OTA_PASSWORD
#define EXAMPLE_OTA_PASSWORD "change-me"
#endif
#ifndef EXAMPLE_MANIFEST_URL
#define EXAMPLE_MANIFEST_URL "http://192.0.2.10:8000/manifest.json"
#endif
#ifndef EXAMPLE_FW_VERSION
#define EXAMPLE_FW_VERSION "0.0.1"
#endif

namespace {

const char* sourceName(ota::Source s) { return s == ota::Source::Push ? "push" : "pull"; }

class SerialObserver : public ota::Observer {
public:
    void onSlot(const ota::SlotInfo& s) override {
        Serial.printf("example: slot %s, %s, boots %lu\n", s.label, s.state,
                      static_cast<unsigned long>(s.boots));
    }
    void onPushListening(uint16_t port) override {
        Serial.printf("example: push listening on port %u\n", static_cast<unsigned>(port));
    }
    void onCheckStarted() override { Serial.println("example: checking"); }
    void onCheckResult(const ota::CheckResult& r) override {
        switch (r.kind) {
            case ota::CheckResult::UpToDate:
                Serial.printf("example: up to date (server %s)\n", r.version);
                break;
            case ota::CheckResult::Available:
                Serial.printf("example: %s available, %lu bytes - send 'i'\n", r.version,
                              static_cast<unsigned long>(r.size));
                break;
            case ota::CheckResult::Failed:
                Serial.printf("example: check failed: %s\n", r.message);
                break;
        }
    }
    void onTransferStarted(ota::Source s) override {
        Serial.printf("example: %s transfer started\n", sourceName(s));
    }
    void onProgress(ota::Source s, uint8_t percent) override {
        Serial.printf("example: %s %u %%\n", sourceName(s), static_cast<unsigned>(percent));
    }
    void onRebooting(ota::Source s) override {
        Serial.printf("example: %s complete, rebooting\n", sourceName(s));
    }
    void onError(ota::Source s, const char* message) override {
        Serial.printf("example: %s failed: %s\n", sourceName(s), message);
    }
};

SerialObserver g_observer;

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\nesp32-ota-kit example %s\n", EXAMPLE_FW_VERSION);

    ota::Config config;
    config.hostname        = EXAMPLE_HOSTNAME;
    config.push_password   = EXAMPLE_OTA_PASSWORD;
    config.manifest_url    = EXAMPLE_MANIFEST_URL;
    config.running_version = EXAMPLE_FW_VERSION;
    ota::begin(config, g_observer);

    // Hostname before mode(): arduino-esp32 3.x applies it when the station
    // interface is created.
    WiFi.setHostname(EXAMPLE_HOSTNAME);
    WiFi.mode(WIFI_STA);
    WiFi.begin(EXAMPLE_WIFI_SSID, EXAMPLE_WIFI_PASS);
}

void loop() {
    ota::setNetworkUp(WiFi.status() == WL_CONNECTED);
    ota::poll();

    if (Serial.available() > 0) {
        switch (Serial.read()) {
            case 'c': ota::requestCheck();   break;
            case 'i': ota::requestInstall(); break;
            default:                         break;
        }
    }
    delay(5);
}
