#pragma once
#include <stdint.h>

// Wi-Fi on demand. Apps call acquire()/release(); the radio is on only while
// someone holds it (plus a short grace period so switching apps doesn't
// reconnect). Connecting never blocks the UI: link up/down transitions arrive
// as WifiChanged events.
//
// Known networks: the ones saved on the device (core::settings, Wi-Fi
// screen, most recent first), then WIFI_SSID and WIFI_EXTRA_NETWORKS from
// secrets.h. With one it connects straight away; with several it scans and
// tries them strongest first. With none the radio never starts.
namespace core::net {

struct Network {
  char ssid[33];
  int8_t rssi;  // dBm
  bool secure;
};
constexpr int kMaxNetworks = 16;

void begin();
void loop();
void acquire();
void release();
void shutdown();  // radio off now (before deep sleep)
// While suspended (minimal view: light sleep and Wi-Fi don't mix) the radio
// stays off even if apps hold it; it comes back when the suspension ends.
void suspend(bool on);
bool connected();
int rssi();
const char *ip();    // "" when not connected
const char *ssid();  // the network it is on or trying ("" if none is set up)
bool known(const char *ssid);

// Try another network (Wi-Fi screen). It is saved to the device only once
// it connects; if it doesn't within WIFI_JOIN_TIMEOUT_MS, the saved network
// comes back. Needs a holder, like any connection. password nullptr: the one
// it knows for `ssid`.
enum class Join : uint8_t { Idle, Trying, Joined, Failed };
void join(const char *ssid, const char *password);
Join joinState();
// Why the last join failed, from the last disconnect reason the Wi-Fi driver
// reported before giving up (Timeout: no reason at all, the router never answered).
enum class JoinFail : uint8_t { None, Password, NotFound, Security, Timeout, Other };
JoinFail joinFail();
uint8_t joinReason();  // the raw WIFI_REASON_* code behind it, 0 if none

// Scan for networks in the background (the Wi-Fi screen holds the radio
// meanwhile). A connection attempt in progress is paused for the scan.
void startScan();
// -1 while scanning, else the number of networks: unique names, strongest first.
int scanResults(const Network *&list);

}  // namespace core::net
