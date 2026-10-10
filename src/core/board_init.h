#pragma once
#include <stdint.h>

// Board bring-up: I2C, the TCA9554 expander (LCD/touch reset, SD line) and
// detecting the board revision by which touch controller answers.
// Pin numbers and addresses live in include/board.h.
namespace core::board {

bool begin();
const char *revisionName();  // "V1" / "V2"
uint8_t touchAddress();      // 0 if no touch controller answered
uint16_t lcdColumnOffset();  // LCD_COL_OFFSET_* for the detected panel driver

}  // namespace core::board
