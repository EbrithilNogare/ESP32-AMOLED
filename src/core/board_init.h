#pragma once
#include <stdint.h>

// Board bring-up: I2C, the TCA9554 expander (LCD/touch reset, SD line) and
// detecting the board revision by which touch controller answers.
// Pin numbers and addresses live in include/board.h.
namespace core::board {

enum class Revision : uint8_t { V1, V2 };

bool begin();
Revision revision();
const char *revisionName();
uint8_t touchAddress();      // 0 if no touch controller answered
uint16_t lcdColumnOffset();  // CO5300 (V2) sits 16 columns into the driver RAM

// TCA9554 pins (EXIO0..7)
bool expanderRead(uint8_t pin);

}  // namespace core::board
