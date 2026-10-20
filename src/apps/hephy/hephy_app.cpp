#include "hephy_app.h"

#include <Arduino.h>
#include <esp_random.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <type_traits>

#include "board.h"
#include "config.h"
#include "core/rtc_clock.h"
#include "core/store.h"
#include "core/ticks.h"
#include "ui/theme.h"

namespace apps {

static const AppInfo kInfo = {"hephy", "Hephy", ICON_HARDWARE, HEPHY_COLOR};
static constexpr char kKey[] = "hephy";
static_assert(std::is_trivially_copyable<pet::State>::value, "saved and parked byte for byte");

// The face: Bot face's shapes at 72 %, centred here.
static constexpr float kFaceScale = 0.72f, kFaceCx = LCD_WIDTH / 2.0f, kFaceCy = 168;
static constexpr int32_t kStroke = 9;  // mouth line, 12 px × 0.72
static constexpr int32_t kStatusY = 276, kMeterY = 314, kButtonsY = 350;
static constexpr float kAnvilY = 250;  // where forge sparks fly from

// Strike bar.
static constexpr int32_t kBarW = 300, kBarH = 28, kBarY = 330;
static constexpr float kZoneHalf = 0.09f;  // of the bar
static constexpr uint32_t kRoundShowMs = 1500;

// Shelf grid: 6 × 4 slots.
static constexpr int32_t kSlot = 48, kSlotGap = 6, kCols = 6;

static const char *const kItemIcons[pet::kItems] = {
    ICON_KEY,          ICON_ANCHOR,     ICON_SETTINGS,     ICON_NOTIFICATIONS, ICON_HOURGLASS_TOP,
    ICON_LIGHTBULB,    ICON_KETTLE,     ICON_BUILD,        ICON_TOKEN,         ICON_BOLT,
    ICON_SHIELD,       ICON_SWORDS,     ICON_CHESS_KNIGHT, ICON_EXPLORE,       ICON_WATCH,
    ICON_ROCKET,       ICON_CASTLE,     ICON_DIAMOND,      ICON_CROWN,         ICON_WORKSPACE_PREMIUM,
    ICON_EMOJI_EVENTS, ICON_WAND_STARS, ICON_OWL,          ICON_FLARE,
};

static uint32_t rarityColor(pet::Rarity r) {
  switch (r) {
    case pet::Rarity::Common: return 0xB0B0B0;
    case pet::Rarity::Rare: return 0x4DA3FF;
    case pet::Rarity::Epic: return 0xB388FF;
    case pet::Rarity::Legendary: return 0xFFC93C;
  }
  return ui::col::text;
}

static const char *rarityName(pet::Rarity r) {
  switch (r) {
    case pet::Rarity::Common: return "Common";
    case pet::Rarity::Rare: return "Rare";
    case pet::Rarity::Epic: return "Epic";
    case pet::Rarity::Legendary: return "Legendary";
  }
  return "";
}

// Button ids (event user data).
enum : intptr_t { kCoal, kStrike, kForge, kShelf };

HephyApp::HephyApp(bool (*isDimmed)()) : isDimmed_(isDimmed), face_(1, 0) {}

const AppInfo &HephyApp::info() const { return kInfo; }
uint32_t HephyApp::idleDimMs() const { return HEPHY_IDLE_DIM_MS; }

int64_t HephyApp::nowSec() const { return core::clock::wallMs() / 1000; }

// ---------------------------------------------------------------- state

void HephyApp::load() {
  pet::State s;
  if (core::store::load(kKey, &s, sizeof s) && pet::valid(s))
    s_ = s;
  else
    s_ = pet::fresh(nowSec(), esp_random());
  loaded_ = true;
}

void HephyApp::save() {
  if (loaded_) core::store::save(kKey, &s_, sizeof s_);
}

// NVS has every action; RTC memory has what changed since (petting, the
// time he was last seen) after a sleep.
void HephyApp::onWake(Wake wake) {
  load();
  pet::State s;
  if (wake != Wake::Cold && core::store::unpark(kKey, &s, sizeof s) && pet::valid(s)) s_ = s;
}

// RTC memory for a sleep; NVS only at a power-off (flash wear: never at
// every sleep, ARCHITECTURE §8.5).
void HephyApp::onSleep(sleepplan::Kind kind) {
  if (!loaded_) return;
  const int64_t now = nowSec();
  pet::advance(s_, now);
  if (root_) pet::seen(s_, now);
  core::store::park(kKey, &s_, sizeof s_);
  if (kind == sleepplan::Kind::PowerOff) save();
}

void HephyApp::say(const char *msg, uint32_t forMs) {
  strlcpy(msg_, msg, sizeof msg_);
  msgUntilMs_ = millis() + forMs;
  if (status_) ui::setText(status_, status());
}

static void lower(char *dst, size_t n, const char *src) {
  strlcpy(dst, src, n);
  if (dst[0] >= 'A' && dst[0] <= 'Z') dst[0] = char(dst[0] - 'A' + 'a');
}

const char *HephyApp::status() const {
  static char buf[64];
  if (asleep_) return "Zzz\xE2\x80\xA6";
  if (msg_[0] && int32_t(millis() - msgUntilMs_) < 0) return msg_;
  if (s_.gift != pet::kNone) {
    char name[24];
    lower(name, sizeof name, pet::item(s_.gift).name);
    snprintf(buf, sizeof buf, "Your %s is ready!", name);
    return buf;
  }
  if (s_.forging != pet::kNone) {
    const int64_t left = pet::forgeLeftSec(s_, nowSec());
    const int h = int(left / 3600), m = int(left % 3600 / 60);
    if (h)
      snprintf(buf, sizeof buf, "%s on the anvil \xC2\xB7 %d h %d m", pet::item(s_.forging).name, h, m);
    else if (m)
      snprintf(buf, sizeof buf, "%s on the anvil \xC2\xB7 %d m", pet::item(s_.forging).name, m);
    else
      snprintf(buf, sizeof buf, "%s on the anvil \xC2\xB7 almost", pet::item(s_.forging).name);
    return buf;
  }
  if (s_.energy < 30) return "Hungry for coal";
  if (s_.joy < 35) return "A little lonely";
  if (pet::canForge(s_) == pet::Result::Ok) return "Ready to forge";
  return "All good";
}

bot::Mood HephyApp::faceMood() const {
  if (asleep_) return bot::Mood::Asleep;
  if (view_ == View::Strike) return bot::Mood::Focused;
  switch (pet::mood(s_)) {
    case pet::Mood::Content: return bot::Mood::Normal;
    case pet::Mood::Happy: return bot::Mood::Happy;
    case pet::Mood::Sleepy: return bot::Mood::Sleepy;
    case pet::Mood::Lonely: return bot::Mood::Sad;
    case pet::Mood::Focused: return bot::Mood::Focused;
  }
  return bot::Mood::Normal;
}

// Brings the pet up to now; a forge that finished meanwhile is revealed.
void HephyApp::refresh(uint32_t nowMs) {
  pet::advance(s_, nowSec());
  if (s_.gift != pet::kNone && !asleep_ && !reveal_ && !revealDue_ && view_ == View::Main) {
    revealDue_ = true;
    revealAtMs_ = nowMs + 900;
  }
  face_.setMood(faceMood());
  if (view_ == View::Main) syncMain();
}

// ---------------------------------------------------------------- lifecycle

void HephyApp::onCreate(lv_obj_t *root) {
  if (!loaded_) load();
  root_ = root;
  const uint32_t now = millis();
  const int64_t t = nowSec();
  pet::advance(s_, t);
  const pet::Greeting g = pet::visit(s_, t);
  asleep_ = g == pet::Greeting::Asleep;
  face_ = bot::Face(esp_random(), now);
  face_.setLively(true, now);
  msg_[0] = 0;
  if (g == pet::Greeting::MissedYou) say("Missed you!", 4000);
  if (g == pet::Greeting::Hello) say("Hi again!", 3000);
  levelShown_ = pet::level(s_.xp);
  revealDue_ = keepDue_ = viewDue_ = false;
  coalFlying_ = false;
  for (Spark &sp : sparks_) sp.life = 0;

  lv_obj_set_clickable(root, true);
  lv_obj_add_event_cb(root, onFacePress, LV_EVENT_PRESSED, this);
  view_ = View::Main;
  buildFace(root);
  content_ = nullptr;
  setView(View::Main);
  refresh(now);
  save();
  nextFrameMs_ = nextRefreshMs_ = lastTickMs_ = now;
}

void HephyApp::onDestroy() {
  if (loaded_) {
    pet::seen(s_, nowSec());
    save();
  }
  root_ = content_ = eyeL_ = eyeR_ = mouth_ = mouthO_ = coal_ = nullptr;
  for (auto &o : sparkObj_) o = nullptr;
  for (auto &o : zObj_) o = nullptr;
  level_ = status_ = energyFill_ = joyFill_ = forgeBtn_ = strikeBtn_ = nullptr;
  strikeCount_ = strikeResult_ = zoneObj_ = marker_ = shelfName_ = shelfInfo_ = reveal_ = nullptr;
  for (auto &o : slots_) o = nullptr;
}

bool HephyApp::onBack() {
  if (reveal_) {
    keepReveal();
    return true;
  }
  if (view_ != View::Main) {
    if (view_ == View::Strike && strikes_ > 0 && !roundOver_) endRound();  // what was struck counts
    setView(View::Main);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- views

static lv_obj_t *shape(lv_obj_t *parent) {
  lv_obj_t *o = ui::box(parent);
  lv_obj_set_style_bg_color(o, ui::c(HEPHY_COLOR), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

void HephyApp::buildFace(lv_obj_t *parent) {
  eyeL_ = shape(parent);
  eyeR_ = shape(parent);
  mouthO_ = shape(parent);
  lv_obj_set_style_radius(mouthO_, LV_RADIUS_CIRCLE, 0);
  mouth_ = lv_line_create(parent);
  lv_obj_set_style_line_color(mouth_, ui::c(HEPHY_COLOR), 0);
  lv_obj_set_style_line_width(mouth_, kStroke, 0);
  lv_obj_set_style_line_rounded(mouth_, true, 0);
  for (auto &o : sparkObj_) {
    o = ui::box(parent);
    lv_obj_set_size(o, 6, 6);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, ui::c(0xFFC24A), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_hidden(o, true);
  }
  static const lv_font_t *const kZFonts[kZs] = {&font_body_medium, &font_title, &font_title};
  for (int i = 0; i < kZs; i++) {
    zObj_[i] = ui::label(parent, kZFonts[i], ui::col::textDim, i == 0 ? "z" : "Z");
    lv_obj_set_hidden(zObj_[i], true);
  }
  coal_ = ui::box(parent);
  lv_obj_set_size(coal_, 24, 24);
  lv_obj_set_style_radius(coal_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(coal_, ui::c(0x2A2422), 0);
  lv_obj_set_style_bg_opa(coal_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(coal_, ui::c(HEPHY_COLOR), 0);
  lv_obj_set_style_border_width(coal_, 2, 0);
  lv_obj_set_hidden(coal_, true);
}

void HephyApp::setView(View v) {
  if (content_) lv_obj_delete(content_);
  level_ = status_ = energyFill_ = joyFill_ = forgeBtn_ = strikeBtn_ = nullptr;
  strikeCount_ = strikeResult_ = zoneObj_ = marker_ = shelfName_ = shelfInfo_ = nullptr;
  for (auto &o : slots_) o = nullptr;
  view_ = v;
  content_ = ui::box(root_);
  lv_obj_set_size(content_, LCD_WIDTH, LCD_HEIGHT);
  const bool faceShown = v != View::Shelf;
  for (lv_obj_t *o : {eyeL_, eyeR_, mouth_, mouthO_}) lv_obj_set_hidden(o, !faceShown);
  if (v != View::Main) coalFlying_ = false;
  lv_obj_set_hidden(coal_, true);
  switch (v) {
    case View::Main: buildMain(); break;
    case View::Strike: buildStrike(); break;
    case View::Shelf: buildShelf(); break;
  }
  face_.setMood(faceMood());
  if (faceShown) drawFace(millis());
}

static lv_obj_t *meter(lv_obj_t *parent, const char *icon, uint32_t color, int32_t x, lv_obj_t **fill) {
  lv_obj_t *i = ui::label(parent, &font_icon_18, color, icon);
  lv_obj_set_pos(i, x, kMeterY - 9);
  lv_obj_t *track = ui::box(parent);
  lv_obj_set_size(track, 112, 8);
  lv_obj_set_pos(track, x + 26, kMeterY - 4);
  lv_obj_set_style_radius(track, 4, 0);
  lv_obj_set_style_bg_color(track, ui::c(ui::col::track), 0);
  lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
  *fill = ui::box(track);
  lv_obj_set_height(*fill, 8);
  lv_obj_set_style_radius(*fill, 4, 0);
  lv_obj_set_style_bg_color(*fill, ui::c(color), 0);
  lv_obj_set_style_bg_opa(*fill, LV_OPA_COVER, 0);
  return track;
}

void HephyApp::buildMain() {
  using namespace ui;
  lv_obj_t *name = label(content_, &font_title, col::text, "Hephy");
  lv_obj_set_pos(name, kEdge, 20);
  level_ = label(content_, &font_caption, HEPHY_COLOR, "");
  lv_obj_set_style_bg_color(level_, c(mixBlack(HEPHY_COLOR, 18)), 0);
  lv_obj_set_style_bg_opa(level_, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(level_, 10, 0);
  lv_obj_set_style_pad_hor(level_, 10, 0);
  lv_obj_set_style_pad_ver(level_, 3, 0);
  lv_obj_set_pos(level_, kEdge + 98, 27);

  lv_obj_t *shelf = roundButton(content_, ICON_SHELVES, col::text);
  lv_obj_set_pos(shelf, LCD_WIDTH - kEdge - 56, 14);
  lv_obj_add_event_cb(shelf, onButton, LV_EVENT_CLICKED, this);
  lv_obj_set_user_data(shelf, reinterpret_cast<void *>(kShelf));

  status_ = label(content_, &font_body, col::textDim, "");
  lv_obj_set_width(status_, LCD_WIDTH - 2 * kEdge);
  lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(status_, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_pos(status_, kEdge, kStatusY - 12);

  meter(content_, ICON_LOCAL_FIRE_DEPARTMENT, HEPHY_COLOR, 36, &energyFill_);
  meter(content_, ICON_FAVORITE, HEPHY_JOY_COLOR, LCD_WIDTH / 2 + 12, &joyFill_);

  struct Btn {
    const char *icon, *caption;
    intptr_t id;
  };
  static const Btn kBtns[3] = {{ICON_LOCAL_FIRE_DEPARTMENT, "Coal", kCoal},
                               {ICON_HARDWARE, "Strike", kStrike},
                               {ICON_CONSTRUCTION, "Forge", kForge}};
  for (int i = 0; i < 3; i++) {
    const int32_t cx = LCD_WIDTH / 2 + (i - 1) * 104;
    lv_obj_t *b = roundButton(content_, kBtns[i].icon, i == 0 ? HEPHY_COLOR : col::text);
    lv_obj_set_pos(b, cx - 28, kButtonsY);
    lv_obj_add_event_cb(b, onButton, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(b, reinterpret_cast<void *>(kBtns[i].id));
    lv_obj_t *cap = label(content_, &font_caption, col::textDim, kBtns[i].caption);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, cx - LCD_WIDTH / 2, kButtonsY + 60);
    if (kBtns[i].id == kStrike) strikeBtn_ = b;
    if (kBtns[i].id == kForge) forgeBtn_ = b;
  }
  syncMain();
}

void HephyApp::syncMain() {
  if (!status_) return;
  ui::setTextFmt(level_, "Lv %d", pet::level(s_.xp));
  ui::setText(status_, status());
  const int32_t e = int32_t(112 * s_.energy / 100 + 0.5f), j = int32_t(112 * s_.joy / 100 + 0.5f);
  if (lv_obj_get_width(energyFill_) != e) lv_obj_set_width(energyFill_, e);
  if (lv_obj_get_width(joyFill_) != j) lv_obj_set_width(joyFill_, j);
  // Dimmed, but still tappable: a tap says why not.
  const uint32_t strikeCol = pet::canStrike(s_) == pet::Result::Ok ? ui::col::text : ui::col::textOff;
  const uint32_t forgeCol = pet::canForge(s_) == pet::Result::Ok ? ui::col::text : ui::col::textOff;
  if (ui::hex(lv_obj_get_style_text_color(strikeBtn_, LV_PART_MAIN)) != strikeCol)
    lv_obj_set_style_text_color(strikeBtn_, ui::c(strikeCol), 0);
  if (ui::hex(lv_obj_get_style_text_color(forgeBtn_, LV_PART_MAIN)) != forgeCol)
    lv_obj_set_style_text_color(forgeBtn_, ui::c(forgeCol), 0);
}

void HephyApp::buildStrike() {
  using namespace ui;
  lv_obj_t *t = label(content_, &font_title, col::text, "Strike");
  lv_obj_set_pos(t, kEdge, 20);
  strikeCount_ = label(content_, &font_body_medium, col::textDim, "");
  lv_obj_align(strikeCount_, LV_ALIGN_TOP_RIGHT, -kEdge, 28);
  strikeResult_ = label(content_, &font_title, HEPHY_COLOR, "");
  lv_obj_align(strikeResult_, LV_ALIGN_TOP_MID, 0, kStatusY - 16);

  lv_obj_t *bar = box(content_);
  lv_obj_set_size(bar, kBarW, kBarH);
  lv_obj_set_pos(bar, (LCD_WIDTH - kBarW) / 2, kBarY);
  lv_obj_set_style_radius(bar, kBarH / 2, 0);
  lv_obj_set_style_bg_color(bar, c(col::track), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  zoneObj_ = box(bar);
  lv_obj_set_size(zoneObj_, int32_t(2 * kZoneHalf * kBarW), kBarH);
  lv_obj_set_style_radius(zoneObj_, kBarH / 2, 0);
  lv_obj_set_style_bg_color(zoneObj_, c(HEPHY_COLOR), 0);
  lv_obj_set_style_bg_opa(zoneObj_, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_color(zoneObj_, c(HEPHY_COLOR), 0);
  lv_obj_set_style_shadow_width(zoneObj_, 18, 0);
  lv_obj_set_style_shadow_opa(zoneObj_, LV_OPA_60, 0);
  marker_ = box(content_);
  lv_obj_set_size(marker_, 6, kBarH + 20);
  lv_obj_set_style_radius(marker_, 3, 0);
  lv_obj_set_style_bg_color(marker_, c(col::text), 0);
  lv_obj_set_style_bg_opa(marker_, LV_OPA_COVER, 0);
  lv_obj_t *hint = label(content_, &font_caption, col::textDim, "Tap to strike");
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, kBarY + kBarH + 30);

  // The whole screen is the anvil: a tap anywhere strikes.
  lv_obj_t *catcher = box(content_);
  lv_obj_set_size(catcher, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_clickable(catcher, true);
  lv_obj_add_event_cb(catcher, onCatcher, LV_EVENT_PRESSED, this);
}

void HephyApp::buildShelf() {
  using namespace ui;
  lv_obj_t *t = label(content_, &font_title, col::text, "Shelf");
  lv_obj_set_pos(t, kEdge, 20);
  lv_obj_t *n = label(content_, &font_body_medium, col::textDim, "");
  ui::setTextFmt(n, "%d / %d", pet::owned(s_), pet::kItems);
  lv_obj_align(n, LV_ALIGN_TOP_RIGHT, -kEdge, 28);
  const int32_t x0 = (LCD_WIDTH - (kCols * kSlot + (kCols - 1) * kSlotGap)) / 2, y0 = 76;
  for (int i = 0; i < pet::kItems; i++) {
    lv_obj_t *s = box(content_);
    lv_obj_set_size(s, kSlot, kSlot);
    lv_obj_set_pos(s, x0 + (i % kCols) * (kSlot + kSlotGap), y0 + (i / kCols) * (kSlot + kSlotGap));
    lv_obj_set_style_radius(s, 12, 0);
    lv_obj_set_style_bg_color(s, c(col::surface), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s, c(col::outline), 0);
    lv_obj_set_style_border_width(s, 0, 0);
    const bool own = pet::has(s_, i);
    lv_obj_t *l = own ? label(s, &font_icon_28, rarityColor(pet::item(i).rarity), kItemIcons[i])
                      : label(s, &font_body_medium, col::textOff, "?");
    lv_obj_center(l);
    lv_obj_set_clickable(s, true);
    lv_obj_add_event_cb(s, onSlot, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(s, reinterpret_cast<void *>(intptr_t(i)));
    slots_[i] = s;
  }
  shelfName_ = label(content_, &font_body_medium, col::text, "");
  lv_obj_align(shelfName_, LV_ALIGN_TOP_MID, 0, 312);
  shelfInfo_ = label(content_, &font_caption, col::textDim, "");
  lv_obj_align(shelfInfo_, LV_ALIGN_TOP_MID, 0, 346);
  if (selected_ < 0 || !pet::has(s_, selected_)) {
    selected_ = -1;
    for (int i = 0; i < pet::kItems && selected_ < 0; i++)
      if (pet::has(s_, i)) selected_ = i;
  }
  syncShelfDetail();
}

void HephyApp::syncShelfDetail() {
  if (!shelfName_) return;
  for (int i = 0; i < pet::kItems; i++) lv_obj_set_style_border_width(slots_[i], i == selected_ ? 2 : 0, 0);
  if (selected_ < 0) {
    ui::setText(shelfName_, "Nothing forged yet");
    lv_obj_set_style_text_color(shelfName_, ui::c(ui::col::textDim), 0);
  } else if (!pet::has(s_, selected_)) {
    ui::setText(shelfName_, "Not forged yet");
    lv_obj_set_style_text_color(shelfName_, ui::c(ui::col::textDim), 0);
  } else {
    const pet::Item &it = pet::item(selected_);
    ui::setTextFmt(shelfName_, "%s \xC2\xB7 %s", it.name, rarityName(it.rarity));
    lv_obj_set_style_text_color(shelfName_, ui::c(rarityColor(it.rarity)), 0);
  }
  lv_obj_align(shelfName_, LV_ALIGN_TOP_MID, 0, 312);
  ui::setTextFmt(shelfInfo_, "Forged %u \xC2\xB7 Lv %d", unsigned(s_.forged), pet::level(s_.xp));
  lv_obj_align(shelfInfo_, LV_ALIGN_TOP_MID, 0, 346);
}

void HephyApp::showReveal() {
  using namespace ui;
  const uint8_t it = s_.gift;
  if (it == pet::kNone || reveal_) return;
  const pet::Rarity r = pet::item(it).rarity;
  const uint32_t col = rarityColor(r);
  const bool isNew = !pet::has(s_, it);
  reveal_ = box(root_);
  lv_obj_set_size(reveal_, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_style_bg_color(reveal_, c(col::bg), 0);
  lv_obj_set_style_bg_opa(reveal_, LV_OPA_COVER, 0);
  lv_obj_set_clickable(reveal_, true);
  lv_obj_add_event_cb(reveal_, onCatcher, LV_EVENT_CLICKED, this);

  lv_obj_t *glow = box(reveal_);
  lv_obj_set_size(glow, 132, 132);
  lv_obj_align(glow, LV_ALIGN_TOP_MID, 0, 96);
  lv_obj_set_style_radius(glow, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(glow, c(mixBlack(col, 16)), 0);
  lv_obj_set_style_bg_opa(glow, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_color(glow, c(col), 0);
  lv_obj_set_style_shadow_width(glow, 48, 0);
  lv_obj_set_style_shadow_opa(glow, LV_OPA_40, 0);
  lv_obj_t *icon = label(glow, &font_icon_64, col, kItemIcons[it]);
  lv_obj_center(icon);

  lv_obj_t *name = label(reveal_, &font_title, col::text, pet::item(it).name);
  lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 256);
  char buf[40];
  if (isNew)
    snprintf(buf, sizeof buf, "New \xC2\xB7 %s", rarityName(r));
  else
    snprintf(buf, sizeof buf, "Duplicate \xC2\xB7 +%lu xp", static_cast<unsigned long>(pet::newItemXp(r) / 2));
  lv_obj_t *kind = label(reveal_, &font_body_medium, col, buf);
  lv_obj_align(kind, LV_ALIGN_TOP_MID, 0, 300);
  lv_obj_t *hint = label(reveal_, &font_caption, col::textDim, "Tap to keep");
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 392);
  face_.react(bot::Reaction::Startle, millis());
}

void HephyApp::keepReveal() {
  if (!reveal_) return;
  lv_obj_delete(reveal_);
  reveal_ = nullptr;
  const pet::Collected c = pet::collect(s_);
  save();
  if (c.item == pet::kNone) return;
  face_.tap(millis());
  char buf[64];
  const int lv = pet::level(s_.xp);
  if (lv > levelShown_)
    snprintf(buf, sizeof buf, "Level %d! \xC2\xB7 +%lu xp", lv, static_cast<unsigned long>(c.xp));
  else
    snprintf(buf, sizeof buf, "On the shelf \xC2\xB7 +%lu xp", static_cast<unsigned long>(c.xp));
  levelShown_ = lv;
  say(buf, 3000);
  refresh(millis());
}

// ---------------------------------------------------------------- actions

void HephyApp::petFace() {
  if (view_ != View::Main || reveal_) return;
  const uint32_t now = millis();
  if (asleep_) {
    asleep_ = false;
    face_.react(bot::Reaction::Startle, now);
    say("Missed you!", 3000);
    refresh(now);
    return;
  }
  face_.tap(now);
  pet::pet(s_);
}

void HephyApp::feedCoal() {
  if (asleep_) return petFace();
  const uint32_t now = millis();
  if (pet::feed(s_) == pet::Result::Full) {
    face_.react(bot::Reaction::ShakeHead, now);
    say("I'm full!");
    return;
  }
  save();
  // The coal flies from the button into his mouth; he chews when it lands.
  coalFlying_ = true;
  coalMs_ = now;
  coalFromX_ = LCD_WIDTH / 2.0f - 104;
  lv_obj_set_hidden(coal_, false);
  lv_obj_move_foreground(coal_);
  say("+25 energy");
  refresh(now);
}

void HephyApp::startStrike() {
  if (asleep_) return petFace();
  if (pet::canStrike(s_) != pet::Result::Ok) {
    face_.react(bot::Reaction::ShakeHead, millis());
    say("Too tired. Coal?");
    return;
  }
  strikes_ = 0;
  roundOver_ = false;
  memset(scores_, 0, sizeof scores_);
  zone_ = 0.25f + float(esp_random() % 1000) / 1000.0f * 0.5f;
  strikeMs_ = millis();
  pendingView_ = View::Strike;  // a button's event: the view changes on the next tick
  viewDue_ = true;
}

// The marker sweeps back and forth, a little faster with every strike.
static float markerPos(uint32_t sinceMs, uint8_t strikes) {
  const uint32_t period = 1400 - strikes * 125;
  const float ph = float(sinceMs % period) / period;
  return ph < 0.5f ? ph * 2 : 2 - ph * 2;
}

void HephyApp::strike() {
  if (view_ != View::Strike || roundOver_) return;
  const uint32_t now = millis();
  const float pos = markerPos(now - strikeMs_, strikes_);
  const uint8_t sc = pet::strikeScore((pos - zone_) / kZoneHalf);
  scores_[strikes_++] = sc;
  static const char *const kWords[3] = {"Miss", "Good", "Perfect!"};
  ui::setText(strikeResult_, kWords[sc]);
  lv_obj_set_style_text_color(strikeResult_, ui::c(sc ? HEPHY_COLOR : ui::col::textDim), 0);
  if (sc) {
    face_.tap(now);
    burst((LCD_WIDTH - kBarW) / 2.0f + pos * kBarW, kBarY, sc == 2 ? 10 : 5, sc == 2 ? 0.28f : 0.18f);
  } else {
    face_.react(bot::Reaction::ShakeHead, now);
  }
  zone_ = 0.2f + float(esp_random() % 1000) / 1000.0f * 0.6f;
  strikeMs_ = now;
  if (strikes_ >= pet::kStrikes) endRound();
  tickStrike(now);
}

void HephyApp::endRound() {
  const pet::Round r = pet::finishRound(s_, scores_, nowSec());
  save();
  roundOver_ = true;
  roundEndMs_ = millis();
  char buf[72];
  if (r.forgeCutSec >= 60)
    snprintf(buf, sizeof buf, "%u perfect \xC2\xB7 +%lu xp \xC2\xB7 forge \xE2\x88\x92%lu min", r.perfect,
             static_cast<unsigned long>(r.xp), static_cast<unsigned long>(r.forgeCutSec / 60));
  else
    snprintf(buf, sizeof buf, "%u perfect \xC2\xB7 +%lu xp", r.perfect, static_cast<unsigned long>(r.xp));
  say(buf, 4000);
  if (strikeResult_) {
    ui::setText(strikeResult_, r.perfect >= 3 ? "Masterful!" : r.perfect + r.good >= 3 ? "Nice work" : "Done");
    lv_obj_set_style_text_color(strikeResult_, ui::c(HEPHY_COLOR), 0);
  }
}

void HephyApp::forge() {
  if (asleep_) return petFace();
  const uint32_t now = millis();
  if (s_.gift != pet::kNone) {
    showReveal();
    return;
  }
  switch (pet::canForge(s_)) {
    case pet::Result::Busy: say("One thing at a time"); return;
    case pet::Result::Tired:
      face_.react(bot::Reaction::ShakeHead, now);
      say("Too tired to forge. Coal?");
      return;
    default: break;
  }
  pet::startForge(s_, nowSec());
  save();
  face_.tap(now);
  burst(kFaceCx, kAnvilY, 10, 0.25f);
  refresh(now);
}

void HephyApp::openShelf() {
  if (asleep_) return petFace();
  pendingView_ = View::Shelf;
  viewDue_ = true;
}

// ---------------------------------------------------------------- events

void HephyApp::onFacePress(lv_event_t *e) {
  auto *self = static_cast<HephyApp *>(lv_event_get_user_data(e));
  if (lv_event_get_target(e) == lv_event_get_current_target(e)) self->petFace();
}

void HephyApp::onButton(lv_event_t *e) {
  auto *self = static_cast<HephyApp *>(lv_event_get_user_data(e));
  switch (intptr_t(lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_current_target(e))))) {
    case kCoal: self->feedCoal(); break;
    case kStrike: self->startStrike(); break;
    case kForge: self->forge(); break;
    case kShelf: self->openShelf(); break;
  }
}

// The Strike view's tap catcher and the reveal overlay. The overlay is
// closed on the next tick: it's deleted, and LVGL is still in its event.
void HephyApp::onCatcher(lv_event_t *e) {
  auto *self = static_cast<HephyApp *>(lv_event_get_user_data(e));
  if (self->reveal_ && lv_event_get_current_target(e) == self->reveal_)
    self->keepDue_ = true;
  else
    self->strike();
}

void HephyApp::onSlot(lv_event_t *e) {
  auto *self = static_cast<HephyApp *>(lv_event_get_user_data(e));
  self->selected_ = ui::eventIndex(e);
  self->syncShelfDetail();
}

// ---------------------------------------------------------------- animation

void HephyApp::burst(float x, float y, int n, float speed) {
  for (Spark &sp : sparks_) {
    if (n <= 0) break;
    if (sp.life) continue;
    const float a = -3.14159f * (0.15f + 0.7f * float(esp_random() % 1000) / 1000.0f);  // upwards fan
    const float v = speed * (0.6f + 0.8f * float(esp_random() % 1000) / 1000.0f);
    sp = {x, y, v * cosf(a), v * sinf(a), uint16_t(450 + esp_random() % 350), 0};
    n--;
  }
}

void HephyApp::tickSparks(uint32_t nowMs, uint32_t dt) {
  if (view_ == View::Main && s_.forging != pet::kNone && !asleep_ && core::reached(nowMs, nextForgeSparkMs_)) {
    nextForgeSparkMs_ = nowMs + 220 + esp_random() % 260;
    burst(kFaceCx + float(int(esp_random() % 60) - 30), kAnvilY, 2, 0.16f);
  }
  for (int i = 0; i < kSparks; i++) {
    Spark &sp = sparks_[i];
    lv_obj_t *o = sparkObj_[i];
    if (!sp.life) {
      if (!lv_obj_is_hidden(o)) lv_obj_set_hidden(o, true);
      continue;
    }
    sp.age = uint16_t(sp.age + dt);
    if (sp.age >= sp.life) {
      sp.life = 0;
      lv_obj_set_hidden(o, true);
      continue;
    }
    sp.vy += 0.0006f * float(dt);  // gravity, px/ms²
    sp.x += sp.vx * float(dt);
    sp.y += sp.vy * float(dt);
    lv_obj_set_hidden(o, false);
    lv_obj_set_pos(o, int32_t(sp.x) - 3, int32_t(sp.y) - 3);
    lv_obj_set_style_opa(o, lv_opa_t(255 - 255 * sp.age / sp.life), 0);
  }
}

// Asleep: three z's float up from the right of his head, one after another.
void HephyApp::tickZs(uint32_t nowMs) {
  for (int i = 0; i < kZs; i++) {
    lv_obj_t *z = zObj_[i];
    if (!asleep_ || view_ == View::Shelf) {
      if (!lv_obj_is_hidden(z)) lv_obj_set_hidden(z, true);
      continue;
    }
    const uint32_t period = 2700;
    const float p = float((nowMs + i * period / kZs) % period) / period;
    lv_obj_set_hidden(z, false);
    lv_obj_set_pos(z, int32_t(kFaceCx + 70 + 26 * p + 6 * sinf(p * 9)), int32_t(kFaceCy - 40 - 70 * p));
    lv_obj_set_style_opa(z, lv_opa_t(255 * (p < 0.2f ? p / 0.2f : 1 - (p - 0.2f) / 0.8f)), 0);
  }
}

void HephyApp::tickCoal(uint32_t nowMs) {
  if (!coalFlying_) return;
  constexpr uint32_t kFlightMs = 480;
  const uint32_t e = nowMs - coalMs_;
  if (e >= kFlightMs) {
    coalFlying_ = false;
    lv_obj_set_hidden(coal_, true);
    face_.react(bot::Reaction::Chew, nowMs);
    burst(kFaceCx, kFaceCy + 50, 4, 0.12f);
    return;
  }
  const float p = float(e) / kFlightMs;
  const float x0 = coalFromX_, y0 = kButtonsY + 16.0f, x1 = kFaceCx, y1 = kFaceCy + 48;
  const float x = x0 + (x1 - x0) * p, y = y0 + (y1 - y0) * p - 120 * 4 * p * (1 - p);  // an arc
  lv_obj_set_pos(coal_, int32_t(x) - 12, int32_t(y) - 12);
}

void HephyApp::tickStrike(uint32_t nowMs) {
  if (view_ != View::Strike || !marker_) return;
  ui::setTextFmt(strikeCount_, "%u / %d", unsigned(strikes_ < pet::kStrikes ? strikes_ + 1 : pet::kStrikes),
                 pet::kStrikes);
  lv_obj_set_x(zoneObj_, int32_t((zone_ - kZoneHalf) * kBarW));
  const float pos = roundOver_ ? zone_ : markerPos(nowMs - strikeMs_, strikes_);
  lv_obj_set_pos(marker_, (LCD_WIDTH - kBarW) / 2 + int32_t(pos * kBarW) - 3, kBarY - 10);
  if (roundOver_ && nowMs - roundEndMs_ >= kRoundShowMs) setView(View::Main);
}

void HephyApp::drawFace(uint32_t nowMs) {
  const bot::Geometry g = bot::scaled(face_.geometry(nowMs), kFaceScale, kFaceCx, kFaceCy);
  auto place = [](lv_obj_t *o, const bot::Rect &r) {
    lv_obj_set_pos(o, lroundf(r.x), lroundf(r.y));
    lv_obj_set_size(o, lroundf(r.w), lroundf(r.h));
    const int32_t radius = lroundf(r.r);
    if (lv_obj_get_style_radius(o, LV_PART_MAIN) != radius) lv_obj_set_style_radius(o, radius, 0);
  };
  place(eyeL_, g.eyeL);
  place(eyeR_, g.eyeR);
  if (g.mouthIsO) {
    lv_obj_set_hidden(mouth_, true);
    lv_obj_set_hidden(mouthO_, false);
    lv_obj_set_pos(mouthO_, lroundf(g.oCx - g.oRx), lroundf(g.oCy - g.oRy));
    lv_obj_set_size(mouthO_, lroundf(2 * g.oRx), lroundf(2 * g.oRy));
    return;
  }
  lv_obj_set_hidden(mouthO_, true);
  lv_obj_set_hidden(mouth_, false);
  // The Bot face's Bézier (endpoints mx ± 44, control mx, my + 56·curve), scaled.
  const float hw = 44 * kFaceScale;
  const float x0 = g.mx - hw, x2 = g.mx + hw, y0 = g.my, y1 = g.my + 56 * kFaceScale * g.curve;
  const int32_t left = lroundf(x0) - kStroke, top = lroundf(fminf(y0, y1)) - kStroke;
  lv_obj_set_pos(mouth_, left, top);
  lv_obj_set_size(mouth_, lroundf(x2 - x0) + 2 * kStroke, lroundf(fabsf(y1 - y0)) + 2 * kStroke);
  for (int i = 0; i < kMouthPts; i++) {
    const float t = float(i) / (kMouthPts - 1), u = 1 - t;
    mouthPts_[i].x = u * u * x0 + 2 * u * t * g.mx + t * t * x2 - left;
    mouthPts_[i].y = u * u * y0 + 2 * u * t * y1 + t * t * y0 - top;
  }
  lv_line_set_points(mouth_, mouthPts_, kMouthPts);
}

void HephyApp::onTick(uint32_t nowMs) {
  if (!root_) return;
  if (viewDue_) {
    viewDue_ = false;
    setView(pendingView_);
    if (view_ == View::Strike) strikeMs_ = nowMs;
  }
  if (keepDue_) {
    keepDue_ = false;
    keepReveal();
  }
  if (revealDue_ && core::reached(nowMs, revealAtMs_)) {
    revealDue_ = false;
    if (view_ == View::Main && !asleep_) showReveal();
  }
  if (core::reached(nowMs, nextRefreshMs_)) {
    nextRefreshMs_ = nowMs + 1000;
    refresh(nowMs);
  }
  if (!core::reached(nowMs, nextFrameMs_)) return;
  const bool dim = isDimmed_ && isDimmed_();
  nextFrameMs_ = nowMs + 1000 / (dim ? BOT_FACE_FPS_DIM : HEPHY_FPS);
  const uint32_t dt = nowMs - lastTickMs_ > 100 ? 100 : nowMs - lastTickMs_;
  lastTickMs_ = nowMs;
  face_.update(nowMs, dim);
  if (view_ != View::Shelf) drawFace(nowMs);
  tickSparks(nowMs, dt);
  tickZs(nowMs);
  tickCoal(nowMs);
  tickStrike(nowMs);
}

}  // namespace apps
