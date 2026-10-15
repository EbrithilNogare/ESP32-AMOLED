#include "wifi_app.h"

#include <Arduino.h>
#include <string.h>

#include "apps/app_manager.h"
#include "board.h"
#include "config.h"
#include "ui/draw.h"
#include "ui/theme.h"

namespace apps {

using core::net::Join;

static constexpr uint32_t kAccent = ui::col::info;
static const AppInfo kInfo = {"wifi", "Wi-Fi", ICON_WIFI, kAccent};

// Keypad: 3 × 4 keys as on a phone, then Backspace / Connect.
static const char kKeys[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#'};
static constexpr int32_t kKeyGap = 8, kKeyW = (LCD_WIDTH - 2 * ui::kPad - 2 * kKeyGap) / 3, kKeyH = 54;
static constexpr int32_t kFieldY = 72, kFieldH = 52, kKeysY = 136, kRowGap = 6;
static constexpr size_t kMinWpaLen = 8;

const AppInfo &WifiApp::info() const { return kInfo; }
uint32_t WifiApp::idleDimMs() const { return 60000; }  // typing a password takes a while

static const char *signalIcon(int rssi) {
  return rssi >= -60 ? ICON_NETWORK_WIFI_3_BAR : rssi >= -72 ? ICON_NETWORK_WIFI_2_BAR : ICON_NETWORK_WIFI_1_BAR;
}

void WifiApp::onCreate(lv_obj_t *root) {
  root_ = root;
  core::net::acquire();  // radio on while the screen is open (scan, join)
  join_ = Join::Idle;
  build(View::List);
}

void WifiApp::onDestroy() {
  core::net::release();
  root_ = status_ = list_ = field_ = mode_ = hint_ = nullptr;
  joinIcon_ = joinTitle_ = joinSub_ = joinBtn_ = spinner_ = nullptr;
  for (auto &l : keyLetters_) l = nullptr;
  for (auto &t : targets_) t = nullptr;
}

bool WifiApp::onBack() {
  switch (view_) {
    case View::List: return false;  // to the menu
    case View::Password: go(View::List); return true;
    case View::Joining:
      if (join_ == Join::Joined) return false;
      go(join_ == Join::Failed ? View::Password : View::List);  // a join in progress carries on
      return true;
  }
  return false;
}

void WifiApp::build(View v) {
  lv_obj_clean(root_);
  status_ = list_ = field_ = mode_ = hint_ = nullptr;
  joinIcon_ = joinTitle_ = joinSub_ = joinBtn_ = spinner_ = nullptr;
  for (auto &l : keyLetters_) l = nullptr;
  for (auto &t : targets_) t = nullptr;
  pressed_ = -1;
  view_ = next_ = v;
  switch (v) {
    case View::List: buildList(); break;
    case View::Password: buildPassword(); break;
    case View::Joining: buildJoining(); break;
  }
}

void WifiApp::onTick(uint32_t nowMs) {
  if (!root_) return;
  if (next_ != view_) build(next_);
  switch (view_) {
    case View::List:
      if (nNets_ < 0) {
        const core::net::Network *list;
        const int n = core::net::scanResults(list);
        if (n >= 0) {
          nNets_ = n;
          memcpy(nets_, list, sizeof(core::net::Network) * size_t(n));
          renderList();
        }
      }
      break;
    case View::Password:
      if (typer_.update(nowMs)) lv_obj_invalidate(field_);
      break;
    case View::Joining:
      if (core::net::joinState() != join_) renderJoin();
      break;
  }
}

// ---------------------------------------------------------------- list

void WifiApp::buildList() {
  using namespace ui;
  header(root_, "WI-FI", col::textDim);
  status_ = header(root_, "", col::textDim);
  lv_obj_align(status_, LV_ALIGN_TOP_RIGHT, -kEdge, kEdge);

  // A plain lv_obj (not ui::box): it scrolls, so it has to stay clickable.
  list_ = lv_obj_create(root_);
  lv_obj_set_style_bg_opa(list_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list_, 0, 0);
  lv_obj_set_style_pad_all(list_, 0, 0);
  lv_obj_set_style_pad_row(list_, 6, 0);
  lv_obj_set_pos(list_, kPad, 60);
  lv_obj_set_size(list_, LCD_WIDTH - 2 * kPad, LCD_HEIGHT - 60 - kBarBottom - kBtnSmall - 12);
  lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list_, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t *rescan = pill(root_, ICON_REFRESH, "Scan again", col::text);
  lv_obj_set_height(rescan, kBtnSmall);
  lv_obj_align(rescan, LV_ALIGN_BOTTOM_MID, 0, -kBarBottom);
  lv_obj_add_event_cb(rescan, onRescan, LV_EVENT_CLICKED, this);

  nNets_ = -1;
  core::net::startScan();
  renderList();
}

void WifiApp::renderList() {
  using namespace ui;
  lv_obj_clean(list_);
  if (nNets_ < 0) {
    setText(status_, "Scanning…");
    return;
  }
  setText(status_, nNets_ ? "" : "No networks found");
  const char *current = core::net::ssid();
  for (int i = 0; i < nNets_; i++) {
    const core::net::Network &n = nets_[i];
    const bool cur = strcmp(n.ssid, current) == 0;
    lv_obj_t *row = box(list_);
    lv_obj_set_size(row, LV_PCT(100), 56);
    lv_obj_set_style_radius(row, 16, 0);
    lv_obj_set_style_bg_color(row, c(col::surface), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(row, 14, 0);
    lv_obj_set_clickable(row, true);
    pressable(row);
    lv_obj_set_user_data(row, reinterpret_cast<void *>(intptr_t(i)));
    lv_obj_add_event_cb(row, onNetwork, LV_EVENT_CLICKED, this);

    lv_obj_t *icon = label(row, &font_icon_28, cur ? kAccent : col::textDim, signalIcon(n.rssi));
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *name = label(row, &font_body, cur ? kAccent : col::text, n.ssid);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(name, LCD_WIDTH - 2 * kPad - 28 - 120);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 40, cur ? -9 : 0);
    if (cur) {
      lv_obj_t *s = label(row, &font_caption, col::textDim, core::net::connected() ? "Connected" : "Saved");
      lv_obj_align(s, LV_ALIGN_LEFT_MID, 40, 12);
    }
    if (n.secure) lv_obj_align(label(row, &font_icon_18, col::textOff, ICON_LOCK), LV_ALIGN_RIGHT_MID, 0, 0);
  }
}

void WifiApp::onNetwork(lv_event_t *e) {
  auto *self = static_cast<WifiApp *>(lv_event_get_user_data(e));
  const core::net::Network &n = self->nets_[ui::eventIndex(e)];
  strlcpy(self->ssid_, n.ssid, sizeof self->ssid_);
  self->secure_ = n.secure;
  self->typer_.clear();
  if (n.secure) {
    self->go(View::Password);
  } else {
    self->connect();
  }
}

void WifiApp::onRescan(lv_event_t *e) {
  auto *self = static_cast<WifiApp *>(lv_event_get_user_data(e));
  self->nNets_ = -1;
  core::net::startScan();
  self->renderList();
}

// ---------------------------------------------------------------- password

void WifiApp::buildPassword() {
  using namespace ui;
  header(root_, "PASSWORD", col::textDim);
  mode_ = header(root_, "", kAccent);
  lv_obj_align(mode_, LV_ALIGN_TOP_RIGHT, -kEdge, kEdge);
  lv_obj_t *name = label(root_, &font_list, col::text, ssid_);
  lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(name, LCD_WIDTH - 2 * kEdge);
  lv_obj_set_pos(name, kEdge, 44);

  field_ = box(root_);
  lv_obj_set_pos(field_, kPad, kFieldY);
  lv_obj_set_size(field_, LCD_WIDTH - 2 * kPad, kFieldH);
  lv_obj_set_style_radius(field_, 14, 0);
  lv_obj_set_style_bg_color(field_, c(col::surfaceHi), 0);
  lv_obj_set_style_bg_opa(field_, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(field_, onFieldDraw, LV_EVENT_DRAW_MAIN_END, this);

  for (int i = 0; i < 12; i++) {
    lv_obj_t *k = box(root_);
    lv_obj_set_size(k, kKeyW, kKeyH);
    lv_obj_set_pos(k, kPad + (i % 3) * (kKeyW + kKeyGap), kKeysY + (i / 3) * (kKeyH + kRowGap));
    lv_obj_set_style_radius(k, 14, 0);
    lv_obj_set_style_bg_color(k, c(col::surface), 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    pressable(k);  // pressed by the pad below, not clickable itself
    targets_[i] = k;
    const char digit[2] = {kKeys[i], 0};
    lv_obj_align(label(k, &font_title, col::text, digit), LV_ALIGN_LEFT_MID, 14, 0);
    keyLetters_[i] = label(k, &font_caption, col::textDim, "");
    lv_obj_align(keyLetters_[i], LV_ALIGN_RIGHT_MID, -12, 1);
  }

  const int32_t barY = LCD_HEIGHT - kBarBottom - kBtnSmall;
  lv_obj_t *del = textButton(root_, ICON_BACKSPACE, col::text, kBtnSmall);
  lv_obj_set_style_text_font(lv_obj_get_child(del, 0), &font_icon_28, 0);
  lv_obj_set_width(del, kKeyW);
  lv_obj_set_pos(del, kPad, barY);
  lv_obj_set_clickable(del, false);
  targets_[12] = del;
  lv_obj_t *ok = textButton(root_, "Connect", mixBlack(kAccent, 15), kBtnSmall);
  lv_obj_set_style_bg_color(ok, c(kAccent), 0);
  lv_obj_set_width(ok, LCD_WIDTH - 2 * kPad - kKeyW - kKeyGap);
  lv_obj_set_pos(ok, kPad + kKeyW + kKeyGap, barY);
  lv_obj_set_clickable(ok, false);
  targets_[13] = ok;

  // One transparent pad over everything below the field, out to the screen
  // edges: a touch goes to the nearest key, so the gaps between keys and the
  // margins along the bezel (where the touch panel is least accurate) still
  // land somewhere instead of nowhere.
  lv_obj_t *pad = box(root_);
  lv_obj_set_pos(pad, 0, kFieldY + kFieldH);
  lv_obj_set_size(pad, LCD_WIDTH, LCD_HEIGHT - kFieldY - kFieldH);
  lv_obj_set_clickable(pad, true);
  lv_obj_add_event_cb(pad, onPad, LV_EVENT_ALL, this);

  hint_ = toast(root_, "At least 8 characters");
  lv_obj_align(hint_, LV_ALIGN_TOP_MID, 0, kFieldY + kFieldH + 8);
  lv_obj_set_hidden(hint_, true);
  renderKeys();
}

// The letters on each key follow the mode (abc / ABC / none in 123).
void WifiApp::renderKeys() {
  const multitap::Mode m = typer_.mode();
  ui::setText(mode_, multitap::modeName(m));
  for (int i = 0; i < 12; i++) {
    const char k = kKeys[i];
    char buf[12] = "";
    if (k == '*')
      strcpy(buf, "#$%");
    else if (k == '#')
      strcpy(buf, m == multitap::Mode::Lower ? "ABC" : m == multitap::Mode::Upper ? "123" : "abc");
    else if (k == '0')
      strcpy(buf, m == multitap::Mode::Digits ? "" : "space");
    else if (m != multitap::Mode::Digits) {
      const char *chars = multitap::charsFor(k, m);  // ends in the digit itself
      const size_t n = strlen(chars) - 1;
      memcpy(buf, chars, n < 4 ? n : 4);
      buf[n < 4 ? n : 4] = '\0';
    }
    ui::setText(keyLetters_[i], buf);
  }
}

// The typed text, the character still cycling in the accent colour with a
// bar under it, then the cursor. A long password scrolls: the tail stays in view.
void WifiApp::drawField(lv_layer_t *layer) {
  lv_area_t a;
  lv_obj_get_coords(field_, &a);
  const lv_font_t *font = &font_body_medium;
  const int32_t pad = 16, cy = (a.y1 + a.y2) / 2, maxW = lv_area_get_width(&a) - 2 * pad - 6;
  const char *t = typer_.text();
  const size_t len = strlen(t);
  if (!len) {
    ui::draw::text(layer, font, ui::col::textOff, LV_OPA_COVER, "Type with the keys below", a.x1 + pad, cy, 0,
                   LV_TEXT_ALIGN_LEFT);
    return;
  }
  auto w = [&](char ch) { return int32_t(lv_font_get_glyph_width(font, uint8_t(ch), 0)); };
  size_t start = len;
  int32_t width = 0;
  while (start > 0 && width + w(t[start - 1]) <= maxW) width += w(t[--start]);

  const size_t committed = typer_.pending() ? len - 1 : len;
  char buf[multitap::kMaxLen + 1];
  int32_t x = a.x1 + pad;
  if (committed > start) {
    memcpy(buf, t + start, committed - start);
    buf[committed - start] = '\0';
    ui::draw::text(layer, font, ui::col::text, LV_OPA_COVER, buf, x, cy, 0, LV_TEXT_ALIGN_LEFT);
    for (size_t i = start; i < committed; i++) x += w(t[i]);
  }
  if (typer_.pending()) {
    const char p[2] = {t[len - 1], 0};
    ui::draw::text(layer, font, kAccent, LV_OPA_COVER, p, x, cy, 0, LV_TEXT_ALIGN_LEFT);
    ui::draw::line(layer, x, cy + 13, x + w(t[len - 1]), cy + 13, kAccent, 2, LV_OPA_COVER);
    x += w(t[len - 1]);
  }
  ui::draw::line(layer, x + 2, cy - 12, x + 2, cy + 12, ui::col::text, 2, LV_OPA_COVER);
}

void WifiApp::onFieldDraw(lv_event_t *e) {
  static_cast<WifiApp *>(lv_event_get_user_data(e))->drawField(lv_event_get_layer(e));
}

// The key under `p`: rows and columns split halfway across the gaps, the
// outer ones run to the screen edge, and the bottom bar takes the rest.
int WifiApp::padTarget(const lv_point_t &p) const {
  const int32_t keysBottom = kKeysY + 4 * (kKeyH + kRowGap) - kRowGap;
  const int32_t barY = LCD_HEIGHT - ui::kBarBottom - ui::kBtnSmall;
  if (p.y >= (keysBottom + barY) / 2) return p.x < ui::kPad + kKeyW + kKeyGap / 2 ? 12 : 13;
  int32_t col = (p.x - ui::kPad + kKeyGap / 2) / (kKeyW + kKeyGap);
  int32_t row = (p.y - kKeysY + kRowGap / 2) / (kKeyH + kRowGap);
  col = col < 0 ? 0 : col > 2 ? 2 : col;
  row = row < 0 ? 0 : row > 3 ? 3 : row;
  return int(row * 3 + col);
}

// The key that was touched first gets the press, wherever the finger slides.
void WifiApp::onPad(lv_event_t *e) {
  auto *self = static_cast<WifiApp *>(lv_event_get_user_data(e));
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    lv_point_t p{0, 0};
    lv_indev_get_point(lv_indev_active(), &p);
    self->pressed_ = self->padTarget(p);
    lv_obj_add_state(self->targets_[self->pressed_], LV_STATE_PRESSED);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (self->pressed_ >= 0) lv_obj_remove_state(self->targets_[self->pressed_], LV_STATE_PRESSED);
  } else if (code == LV_EVENT_CLICKED && self->pressed_ >= 0) {
    const int t = self->pressed_;
    self->pressed_ = -1;
    if (t < 12)
      self->pressKey(kKeys[t]);
    else if (t == 12)
      self->backspace();
    else
      self->connectPressed();
  }
}

void WifiApp::pressKey(char key) {
  if (!typer_.press(key, millis())) return;
  if (key == '#') renderKeys();
  lv_obj_set_hidden(hint_, true);
  lv_obj_invalidate(field_);
}

void WifiApp::backspace() {
  if (typer_.backspace()) lv_obj_invalidate(field_);
}

void WifiApp::connectPressed() {
  if (typer_.length() < kMinWpaLen) {  // WPA2 refuses shorter passphrases
    lv_obj_set_hidden(hint_, false);
    return;
  }
  connect();
}

void WifiApp::connect() {
  core::net::join(ssid_, secure_ ? typer_.text() : "");
  join_ = Join::Idle;  // renderJoin() picks up Trying
  go(View::Joining);
}

// ---------------------------------------------------------------- joining

void WifiApp::buildJoining() {
  using namespace ui;
  header(root_, "WI-FI", col::textDim);
  spinner_ = lv_spinner_create(root_);
  lv_spinner_set_anim_params(spinner_, 1000, 270);
  lv_obj_set_size(spinner_, 132, 132);
  lv_obj_align(spinner_, LV_ALIGN_TOP_MID, 0, 92);
  lv_obj_set_style_arc_width(spinner_, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_color(spinner_, c(col::track), LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner_, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(spinner_, c(kAccent), LV_PART_INDICATOR);
  joinIcon_ = label(root_, &font_icon_56, kAccent, ICON_WIFI);
  lv_obj_align(joinIcon_, LV_ALIGN_TOP_MID, 0, 130);
  joinTitle_ = label(root_, &font_title, col::text, "");
  lv_obj_align(joinTitle_, LV_ALIGN_TOP_MID, 0, 250);
  joinSub_ = label(root_, &font_body, col::textDim, "");
  lv_obj_set_width(joinSub_, LCD_WIDTH - 2 * kEdge);
  lv_obj_set_style_text_align(joinSub_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(joinSub_, LV_ALIGN_TOP_MID, 0, 292);
  joinBtn_ = textButton(root_, "", col::text, kBtn);
  lv_obj_set_width(joinBtn_, 200);
  lv_obj_align(joinBtn_, LV_ALIGN_BOTTOM_MID, 0, -kBarBottom);
  lv_obj_add_event_cb(joinBtn_, onJoinButton, LV_EVENT_CLICKED, this);
  join_ = Join::Idle;
  renderJoin();
}

void WifiApp::renderJoin() {
  join_ = core::net::joinState();
  const bool trying = join_ == Join::Trying || join_ == Join::Idle;
  lv_obj_set_hidden(spinner_, !trying);
  lv_obj_set_hidden(joinBtn_, trying);
  if (trying) {
    ui::setText(joinTitle_, "Connecting…");
    ui::setText(joinSub_, ssid_);
    return;
  }
  const bool ok = join_ == Join::Joined;
  ui::setText(joinIcon_, ok ? ICON_CHECK_CIRCLE : ICON_ERROR);
  lv_obj_set_style_text_color(joinIcon_, ui::c(ok ? ui::col::success : ui::col::error), 0);
  ui::setText(joinTitle_, ok ? "Connected" : "Couldn't connect");
  if (ok)
    ui::setTextFmt(joinSub_, "%s · %s\nSaved on this device", ssid_, core::net::ip());
  else
    switch (core::net::joinFail()) {
      case core::net::JoinFail::Password: ui::setText(joinSub_, "Wrong password — check upper and lower case"); break;
      case core::net::JoinFail::NotFound: ui::setText(joinSub_, "Network not found — move closer to the router"); break;
      case core::net::JoinFail::Security: ui::setText(joinSub_, "This network's security type isn't supported"); break;
      case core::net::JoinFail::Timeout: ui::setText(joinSub_, "The router didn't answer — try again"); break;
      default: ui::setTextFmt(joinSub_, "Error %u — try again", core::net::joinReason()); break;
    }
  ui::setText(lv_obj_get_child(joinBtn_, 0), ok ? "Done" : "Try again");
}

void WifiApp::onJoinButton(lv_event_t *) { manager::back(); }  // onBack() decides where to

}  // namespace apps
