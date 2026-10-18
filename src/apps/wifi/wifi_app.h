#pragma once
#include <multi_tap.h>

#include "apps/app.h"
#include "core/net.h"

namespace apps {

// Settings → Wi-Fi (docs/apps/wifi.md), opened from the quick settings, not
// from the menu: scan, pick a network, type the password on a Nokia-style
// keypad. The network is saved on the device once it connects
// (core::net::join), so it survives reboots and firmware updates.
class WifiApp : public App {
 public:
  enum class View : uint8_t { List, Password, Joining };

  const AppInfo &info() const override;
  void onCreate(lv_obj_t *root) override;
  void onDestroy() override;
  void onTick(uint32_t nowMs) override;
  bool onBack() override;
  uint32_t idleDimMs() const override;
  bool resumable() const override { return false; }  // a half-typed password doesn't outlive a sleep

  View view() const { return view_; }
  const char *typed() const { return typer_.text(); }

 private:
  // Views are switched from onTick, never inside a click handler: switching
  // deletes the widgets, including the one whose handler is running.
  void go(View v) { next_ = v; }
  void build(View v);
  void buildList();
  void buildPassword();
  void buildJoining();
  void renderList();
  void renderKeys();
  void renderJoin();
  void connect();
  void drawField(lv_layer_t *layer);

  static void onNetwork(lv_event_t *e);
  static void onRescan(lv_event_t *e);
  int padTarget(const lv_point_t &p) const;
  void pressKey(char key);
  void backspace();
  void connectPressed();

  static void onPad(lv_event_t *e);
  static void onJoinButton(lv_event_t *e);
  static void onFieldDraw(lv_event_t *e);

  lv_obj_t *root_ = nullptr;
  View view_ = View::List, next_ = View::List;

  core::net::Network nets_[core::net::kMaxNetworks];
  int nNets_ = -1;  // -1 while scanning
  char ssid_[33] = {};
  bool secure_ = true;
  multitap::MultiTap typer_;
  core::net::Join join_ = core::net::Join::Idle;

  lv_obj_t *status_ = nullptr, *list_ = nullptr;                   // list
  lv_obj_t *field_ = nullptr, *mode_ = nullptr, *hint_ = nullptr;  // password
  lv_obj_t *keyLetters_[12] = {};
  lv_obj_t *targets_[14] = {};  // the 12 keys, Backspace, Connect
  int pressed_ = -1;            // index into targets_ while a finger is on the pad
  lv_obj_t *joinIcon_ = nullptr, *joinTitle_ = nullptr, *joinSub_ = nullptr, *joinBtn_ = nullptr,
           *spinner_ = nullptr;  // joining
};

}  // namespace apps
