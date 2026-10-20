#pragma once
#include <bot_face.h>
#include <pet_core.h>

#include "apps/app.h"

namespace apps {

// Hephy, the virtual pet (docs/apps/hephy.md). The rules are lib/PetCore,
// all on the wall clock (nothing runs while the screen is off); the face is
// lib/BotFace's, scaled down and in ember orange, with moods and reactions.
// Three views share the root: Main (face, status, meters, Coal / Strike /
// Forge), Strike (a 5-tap timing game) and Shelf (the collection), plus the
// reveal overlay when a forged trinket is collected.
class HephyApp : public App {
 public:
  explicit HephyApp(bool (*isDimmed)());
  const AppInfo &info() const override;
  void onCreate(lv_obj_t *root) override;
  void onDestroy() override;
  void onTick(uint32_t nowMs) override;
  bool onBack() override;
  uint32_t idleDimMs() const override;
  void onSleep(sleepplan::Kind kind) override;
  void onWake(Wake wake) override;

  enum class View : uint8_t { Main, Strike, Shelf };
  View view() const { return view_; }
  const pet::State &state() const { return s_; }
  bool asleep() const { return asleep_; }
  bool revealing() const { return reveal_ != nullptr; }
  const char *status() const;  // the status line's text (for the simulator)

  // What the buttons and taps do, callable from the simulator too.
  void feedCoal();
  void startStrike();
  void strike();  // a tap in the Strike view
  void forge();
  void petFace();
  void openShelf();
  void keepReveal();

 private:
  struct Spark {
    float x, y, vx, vy;
    uint16_t life, age;  // ms
  };
  static constexpr int kSparks = 12, kZs = 3;

  int64_t nowSec() const;
  void load();
  void save();
  void refresh(uint32_t nowMs);  // advance the pet, update status and meters
  void say(const char *msg, uint32_t forMs = 2500);
  void setView(View v);
  void buildMain();
  void buildStrike();
  void buildShelf();
  void buildFace(lv_obj_t *parent);
  void drawFace(uint32_t nowMs);
  void syncMain();
  void syncShelfDetail();
  void showReveal();
  void burst(float x, float y, int n, float speed);
  void tickSparks(uint32_t nowMs, uint32_t dt);
  void tickZs(uint32_t nowMs);
  void tickCoal(uint32_t nowMs);
  void tickStrike(uint32_t nowMs);
  void endRound();
  bot::Mood faceMood() const;

  static void onFacePress(lv_event_t *e);
  static void onButton(lv_event_t *e);
  static void onCatcher(lv_event_t *e);
  static void onSlot(lv_event_t *e);

  bool (*isDimmed_)();
  bot::Face face_;
  pet::State s_{};
  bool loaded_ = false;
  View view_ = View::Main;
  bool asleep_ = false;
  bool revealDue_ = false, keepDue_ = false, viewDue_ = false;
  View pendingView_ = View::Main;
  uint32_t revealAtMs_ = 0;
  char msg_[72] = "";
  uint32_t msgUntilMs_ = 0;
  uint32_t nextFrameMs_ = 0, nextRefreshMs_ = 0, lastTickMs_ = 0;
  int levelShown_ = 0;
  int selected_ = -1;  // shelf

  // Strike round
  uint8_t strikes_ = 0, scores_[pet::kStrikes] = {};
  float zone_ = 0.5f;  // hot zone centre, 0..1 along the bar
  uint32_t strikeMs_ = 0, roundEndMs_ = 0;
  bool roundOver_ = false;

  // Animations
  Spark sparks_[kSparks] = {};
  uint32_t nextForgeSparkMs_ = 0;
  uint32_t coalMs_ = 0;
  bool coalFlying_ = false;
  float coalFromX_ = 0;

  lv_obj_t *root_ = nullptr, *content_ = nullptr;
  lv_obj_t *eyeL_ = nullptr, *eyeR_ = nullptr, *mouth_ = nullptr, *mouthO_ = nullptr;
  static constexpr int kMouthPts = 13;
  lv_point_precise_t mouthPts_[kMouthPts];
  lv_obj_t *sparkObj_[kSparks] = {}, *zObj_[kZs] = {}, *coal_ = nullptr;
  lv_obj_t *level_ = nullptr, *status_ = nullptr, *energyFill_ = nullptr, *joyFill_ = nullptr;
  lv_obj_t *forgeBtn_ = nullptr, *strikeBtn_ = nullptr;
  lv_obj_t *strikeCount_ = nullptr, *strikeResult_ = nullptr, *zoneObj_ = nullptr, *marker_ = nullptr;
  lv_obj_t *shelfName_ = nullptr, *shelfInfo_ = nullptr, *slots_[pet::kItems] = {};
  lv_obj_t *reveal_ = nullptr;
};

}  // namespace apps
