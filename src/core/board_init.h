#pragma once
#include <stdint.h>

// Board bring-up: I2C, the TCA9554 expander (LCD/touch reset) and detecting
// the board revision by which touch controller answers. The microSD slot
// isn't used (no card, ever): its lines stay untouched.
// Pin numbers and addresses live in include/board.h.
namespace core::board {

bool begin();
// The reset pulse the panel and the touch controller need to come out of
// their sleep (the only way out of the touch controller's): both restart,
// the panel with a black RAM. Blocks ~80 ms.
void resetPanelAndTouch();
const char *revisionName();  // "V1" / "V2"
uint8_t touchAddress();      // 0 if no touch controller answered
uint16_t lcdColumnOffset();  // LCD_COL_OFFSET_* for the detected panel driver

}  // namespace core::board
