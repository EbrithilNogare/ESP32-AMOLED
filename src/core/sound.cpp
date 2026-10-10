// ES8311 register setup ported from Espressif's es8311 driver (Apache-2.0,
// as shipped in Waveshare's 15_ES8311 example): DAC playback, I2S slave,
// 16-bit, MCLK = 256 x fs from the ESP. Ported onto our locked I2C bus
// because the original uses the legacy I2C driver, which can't coexist with
// Arduino's Wire (new i2c_master driver) in the same firmware.
#include "sound.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <math.h>

#include "board.h"
#include "config.h"
#include "i2c_bus.h"

namespace core::sound {

[[maybe_unused]] static const char *TAG = "sound";

static_assert(SOUND_SAMPLE_RATE == 16000, "initCodec() hard-codes the ES8311 dividers for fs = 16 kHz");
static_assert(SOUND_VOLUME >= 0 && SOUND_VOLUME <= 100, "SOUND_VOLUME is 0-100");

// DAC volume register 0x32: 0x00 = -95.5 dB ... 0xBF = 0 dB, 0.5 dB per step
// (above 0xBF is digital gain up to +32 dB, which would clip the tones).
static constexpr uint8_t kDacVolume0dB = 0xBF;
static constexpr uint8_t kDacVolume = SOUND_VOLUME == 0 ? 0 : uint8_t(SOUND_VOLUME * kDacVolume0dB / 100);
static QueueHandle_t queue;
static I2SClass i2s;
static bool codecReady;

static bool reg(uint8_t r, uint8_t v) { return i2c::writeReg(I2C_ADDR_ES8311, r, v); }
static uint8_t get(uint8_t r) {
  int v = i2c::readReg(I2C_ADDR_ES8311, r);
  return v < 0 ? 0 : uint8_t(v);
}

static bool initCodec() {
  // Reset, then power on.
  if (!reg(0x00, 0x1F)) return false;
  reg(0x00, 0x00);
  reg(0x00, 0x80);
  // Clocks: MCLK from the MCLK pin, all clocks on, SCLK not inverted.
  reg(0x01, 0x3F);
  reg(0x06, get(0x06) & ~0x20);
  // Dividers for MCLK 4.096 MHz / fs 16 kHz (coeff table row):
  // pre_div 1, pre_multi 1x, adc/dac div 1, single speed, lrck 0x00FF, bclk div 4, osr 0x10.
  reg(0x02, get(0x02) & 0x07);
  reg(0x03, 0x10);
  reg(0x04, 0x10);
  reg(0x05, 0x00);
  reg(0x06, uint8_t((get(0x06) & 0xE0) | (4 - 1)));
  reg(0x07, get(0x07) & 0xC0);
  reg(0x08, 0xFF);
  // Serial port: slave, I2S, 16-bit in and out.
  reg(0x00, get(0x00) & 0xBF);
  reg(0x09, 3 << 2);
  reg(0x0A, 3 << 2);
  // Power up analog, DAC and the output driver; bypass the DAC equaliser.
  reg(0x0D, 0x01);
  reg(0x0E, 0x02);
  reg(0x12, 0x00);
  reg(0x13, 0x10);
  reg(0x1C, 0x6A);
  reg(0x37, 0x08);
  reg(0x32, kDacVolume);
  return true;
}

struct Tone {
  uint16_t hz;
  uint16_t ms;
};

static void playTones(const Tone *tones, size_t n) {
  static int16_t buf[256 * 2];  // stereo frames
  for (size_t t = 0; t < n; t++) {
    const uint32_t total = uint32_t(SOUND_SAMPLE_RATE) * tones[t].ms / 1000;
    const float step = 2.0f * float(M_PI) * tones[t].hz / SOUND_SAMPLE_RATE;
    uint32_t done = 0;
    while (done < total) {
      const uint32_t frames = min<uint32_t>(256, total - done);
      for (uint32_t i = 0; i < frames; i++) {
        // 5 ms fade in/out avoids clicks.
        const uint32_t pos = done + i, edge = SOUND_SAMPLE_RATE / 200;
        float env = 1.0f;
        if (tones[t].hz == 0)
          env = 0;
        else if (pos < edge)
          env = float(pos) / edge;
        else if (total - pos < edge)
          env = float(total - pos) / edge;
        const int16_t s = int16_t(sinf(step * pos) * 12000.0f * env);
        buf[2 * i] = s;
        buf[2 * i + 1] = s;
      }
      i2s.write(reinterpret_cast<uint8_t *>(buf), frames * 4);
      done += frames;
    }
  }
}

static void task(void *) {
  Pattern p;
  for (;;) {
    if (xQueueReceive(queue, &p, portMAX_DELAY) != pdTRUE) continue;
    i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_DIN, PIN_I2S_MCLK);
    if (!i2s.begin(I2S_MODE_STD, SOUND_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
      ESP_LOGE(TAG, "I2S init failed");
      continue;
    }
    if (!codecReady) codecReady = initCodec();  // needs MCLK running
    if (!codecReady) {
      ESP_LOGE(TAG, "ES8311 not responding");
      i2s.end();
      continue;
    }
    digitalWrite(PIN_PA_EN, HIGH);
    delay(10);
    if (p == Pattern::PhaseEnd) {
      static const Tone t[] = {{880, 120}, {0, 60}, {1175, 120}, {0, 60}, {1568, 220}};
      playTones(t, sizeof t / sizeof t[0]);
    } else {
      static const Tone t[] = {{1320, 40}};
      playTones(t, 1);
    }
    static const Tone silence[] = {{0, 40}};  // flush the DMA buffers before muting
    playTones(silence, 1);
    digitalWrite(PIN_PA_EN, LOW);
    i2s.end();
  }
}

void begin() {
  gpio_hold_dis(gpio_num_t(PIN_PA_EN));  // held LOW through deep sleep (power.cpp)
  pinMode(PIN_PA_EN, OUTPUT);
  digitalWrite(PIN_PA_EN, LOW);
  queue = xQueueCreate(4, sizeof(Pattern));
  xTaskCreatePinnedToCore(task, "sound", 4096, nullptr, 5, nullptr, 0);
}

void play(Pattern p) {
  if (queue) xQueueSend(queue, &p, 0);
}

}  // namespace core::sound
