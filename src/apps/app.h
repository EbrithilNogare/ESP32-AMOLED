#pragma once
#include <lvgl.h>
#include <power_fsm.h>
#include <stdint.h>

// The contract every app implements (ARCHITECTURE §6.1). An app object lives
// for the whole runtime; its widgets exist only while it is open.
namespace apps {

struct AppInfo {
  const char *id;    // stable key for NVS ("pomodoro")
  const char *name;  // label in the menu
  const char *icon;  // ICON_* glyph from the icon font
  uint32_t accent;   // 0xRRGGBB
};

class App {
 public:
  virtual const AppInfo &info() const = 0;

  // Build widgets inside `root` (a full-screen container that the manager
  // moves for pixel shift). Called on every open.
  virtual void onCreate(lv_obj_t *root) = 0;
  // The widgets are about to be deleted: drop pointers to them.
  virtual void onDestroy() {}
  virtual void onTick(uint32_t nowMs) { (void)nowMs; }   // every loop while visible
  virtual void service(uint32_t nowMs) { (void)nowMs; }  // every loop, visible or not
  // Back button: true if handled inside the app; false returns to the menu.
  virtual bool onBack() { return false; }
  // Home/Back are ignored while this returns true (e.g. OTA transfer running).
  virtual bool blocksNavigation() const { return false; }

  virtual power::Policy powerPolicy() const { return power::Policy::Normal; }       // while open
  virtual power::Policy backgroundPolicy() const { return power::Policy::Normal; }  // while closed
  virtual uint32_t idleDimMs() const { return 0; }                                  // 0 = profile default

  // Landscape: the manager turns the whole screen (and touch) while this
  // returns true, so the board's left edge is down, resizes `root` to
  // 448 × 368 and calls onRotated() to lay the widgets out again.
  virtual bool landscape() const { return false; }
  virtual void onRotated() {}

  // LowPowerDisplay: draw a sparse, dim view into `layer` (a black overlay)
  // and say when the next redraw is due; the CPU light-sleeps in between.
  virtual void enterMinimal(lv_obj_t *layer) { (void)layer; }
  virtual void exitMinimal() {}
  virtual void minimalTick(uint32_t nowMs) { (void)nowMs; }
  virtual uint32_t minimalRefreshMs(uint32_t nowMs) const {
    (void)nowMs;
    return 60000;
  }
  virtual uint8_t minimalBrightness() const { return 0; }  // panel level; 0 = BRIGHTNESS_MINIMAL

  virtual void onSleep() {}  // deep sleep is coming: persist what matters
  virtual ~App() = default;
};

}  // namespace apps
