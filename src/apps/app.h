#pragma once
#include <lvgl.h>
#include <power_fsm.h>
#include <sleep_plan.h>
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

// How the device came up, for onWake() (ARCHITECTURE §8.4).
enum class Wake : uint8_t {
  Cold,          // power-on, reset, flashing: RTC memory is empty
  FromSleep,     // a deep sleep ended (power button or a deadline): carry on
  FromPowerOff,  // a power-off ended: what onSleep(PowerOff) parked, paused
};

class App {
 public:
  virtual const AppInfo &info() const = 0;

  // Build widgets inside `root` (a full-screen container that the manager
  // moves for pixel shift). Called on every open.
  virtual void onCreate(lv_obj_t *root) = 0;
  // The widgets are about to be deleted: drop pointers to them.
  virtual void onDestroy() {}
  virtual void onTick(uint32_t nowMs) { (void)nowMs; }   // every loop while visible and the screen is on
  virtual void service(uint32_t nowMs) { (void)nowMs; }  // every loop, visible or not, screen on or off
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

  // ---- the screen off (ARCHITECTURE §8): every app's answer is merged ----
  // What this app needs while the screen is off, open or not: `awake` keeps
  // the chip running (sound), `wakeAtMs` (core::mono() time) wakes it from
  // deep sleep for a deadline. service() then runs as usual, and an app that
  // needs the user calls manager::attention().
  virtual sleepplan::Need sleepNeed(uint64_t monoNow) const {
    (void)monoNow;
    return {false, 0};
  }
  // Deep sleep (Sleep) or power-off is coming: park what must outlive it
  // with core::store::park() (RTC memory, no flash wear). PowerOff stops
  // everything: timers pause, sound stops.
  virtual void onSleep(sleepplan::Kind kind) { (void)kind; }
  // Once per boot, before any onCreate(): load settings (NVS) and, unless
  // Cold, unpark what onSleep() left.
  virtual void onWake(Wake wake) { (void)wake; }
  // The screen went off (on) while the chip keeps running (dozing): stop
  // (restart) what only matters on screen, e.g. a sensor. Open app only.
  virtual void onScreen(bool on) { (void)on; }
  // Reopened when the power button wakes the device from a sleep with it open.
  virtual bool resumable() const { return true; }
  virtual ~App() = default;
};

}  // namespace apps
