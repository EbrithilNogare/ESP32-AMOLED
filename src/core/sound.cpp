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
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <math.h>

#include <atomic>

#include "board.h"
#include "config.h"
#include "i2c_bus.h"
#include "settings.h"

namespace core::sound {

[[maybe_unused]] static const char *TAG = "sound";

static_assert(SOUND_SAMPLE_RATE == 16000, "initCodec() hard-codes the ES8311 dividers for fs = 16 kHz");
static_assert(SOUND_VOLUME_RANGE_DB > 0 && SOUND_VOLUME_RANGE_DB <= 95, "the DAC spans -95.5..0 dB");

// DAC volume register 0x32: 0x00 = -95.5 dB ... 0xBF = 0 dB, 0.5 dB per step
// (above 0xBF is digital gain up to +32 dB, which would clip).
static constexpr uint8_t kDacVolume0dB = 0xBF;
static constexpr uint32_t kChunk = 256;  // frames per I2S write: 16 ms
static constexpr uint8_t kNone = 0xFF;   // no noise requested

enum class Cmd : uint8_t { Beep, Noise };
struct Msg {
  Cmd cmd;
  uint8_t value;  // Pattern, or noise::Kind / kNone
};

static QueueHandle_t queue;
static I2SClass i2s;
static bool codecReady;
static std::atomic<bool> volumeDirty{true};
static std::atomic<uint8_t> noiseWanted{kNone};  // what the app asked for
static std::atomic<bool> outputOpen{false};

static bool reg(uint8_t r, uint8_t v) { return i2c::writeReg(I2C_ADDR_ES8311, r, v); }
static uint8_t get(uint8_t r) {
  int v = i2c::readReg(I2C_ADDR_ES8311, r);
  return v < 0 ? 0 : uint8_t(v);
}

static uint8_t dacVolume() {
  const uint8_t pct = settings::volumePct();
  if (settings::muted() || pct == 0) return 0;
  return uint8_t(kDacVolume0dB - (100 - pct) * (2 * SOUND_VOLUME_RANGE_DB) / 100);
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
  reg(0x45, 0x00);  // undo powerDownCodec()
  return true;
}

// The analog side stays powered after playback, also through deep sleep, so
// power it down whenever the output closes (es8311_suspend() in the same
// driver); openOutput() runs initCodec() again. Needs MCLK, so before i2s.end().
static void powerDownCodec() {
  reg(0x32, 0x00);  // DAC volume
  reg(0x17, 0x00);  // ADC volume
  reg(0x0E, 0xFF);  // PGA and ADC modulator off
  reg(0x12, 0x02);  // DAC off
  reg(0x14, 0x00);  // mic PGA off
  reg(0x0D, 0xFA);  // analog, bias and references off
  reg(0x15, 0x00);
  reg(0x37, 0x08);
  reg(0x45, 0x01);
  codecReady = false;
}

// Volume and the amplifier follow the settings; checked before every buffer.
static void syncVolume() {
  if (!volumeDirty.exchange(false)) return;
  const uint8_t v = dacVolume();
  reg(0x32, v);
  digitalWrite(PIN_PA_EN, v ? HIGH : LOW);  // muted: amp off, no hiss
}

static bool openOutput() {
  if (outputOpen) return true;
  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_DIN, PIN_I2S_MCLK);
  if (!i2s.begin(I2S_MODE_STD, SOUND_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    ESP_LOGE(TAG, "I2S init failed");
    return false;
  }
  if (!codecReady) codecReady = initCodec();  // needs MCLK running
  if (!codecReady) {
    ESP_LOGE(TAG, "ES8311 not responding");
    i2s.end();
    return false;
  }
  volumeDirty = true;
  syncVolume();
  delay(10);
  outputOpen = true;
  return true;
}

static void writeMono(const int16_t *mono, uint32_t frames) {
  static int16_t stereo[kChunk * 2];
  for (uint32_t i = 0; i < frames; i++) stereo[2 * i] = stereo[2 * i + 1] = mono[i];
  i2s.write(reinterpret_cast<uint8_t *>(stereo), frames * 4);
}

struct Tone {
  uint16_t hz;
  uint16_t ms;
};

static void playTones(const Tone *tones, size_t n) {
  static int16_t buf[kChunk];
  for (size_t t = 0; t < n; t++) {
    const uint32_t total = uint32_t(SOUND_SAMPLE_RATE) * tones[t].ms / 1000;
    const float step = 2.0f * float(M_PI) * tones[t].hz / SOUND_SAMPLE_RATE;
    uint32_t done = 0;
    while (done < total) {
      const uint32_t frames = min<uint32_t>(kChunk, total - done);
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
        buf[i] = int16_t(sinf(step * pos) * 12000.0f * env);
      }
      writeMono(buf, frames);
      done += frames;
    }
  }
}

static void playPattern(Pattern p) {
  syncVolume();
  switch (p) {
    case Pattern::PhaseEnd: {
      static const Tone t[] = {{880, 120}, {0, 60}, {1175, 120}, {0, 60}, {1568, 220}};
      playTones(t, sizeof t / sizeof t[0]);
      break;
    }
    case Pattern::Alarm: {
      static const Tone t[] = {{1760, 90}, {0, 70}, {1760, 90}, {0, 70}, {1760, 90}, {0, 70}, {1760, 90}};
      playTones(t, sizeof t / sizeof t[0]);
      break;
    }
    case Pattern::Confirm: {
      static const Tone t[] = {{1320, 40}};
      playTones(t, 1);
      break;
    }
  }
}

static void closeOutput() {
  static const Tone silence[] = {{0, 40}};  // flush the DMA buffers before muting
  playTones(silence, 1);
  digitalWrite(PIN_PA_EN, LOW);
  powerDownCodec();
  i2s.end();
  outputOpen = false;
}

// Plays queued beeps; while noise is wanted (or fading out) it streams noise
// in 16 ms buffers and checks the queue between them.
static void task(void *) {
  noise::Generator gen(esp_random(), SOUND_SAMPLE_RATE);
  static int16_t mono[kChunk];
  const float fadeStep = 1.0f / (float(SOUND_SAMPLE_RATE) * NOISE_FADE_MS / 1000);
  uint8_t playing = kNone;  // the kind being streamed
  float gain = 0;
  for (;;) {
    Msg m;
    const bool streaming = playing != kNone || noiseWanted != kNone;
    if (!streaming && outputOpen) closeOutput();  // before waiting: amp off while idle
    if (xQueueReceive(queue, &m, streaming ? 0 : portMAX_DELAY) == pdTRUE && m.cmd == Cmd::Beep && openOutput())
      playPattern(Pattern(m.value));
    if (playing == kNone) playing = noiseWanted;
    if (playing == kNone) {  // nothing (more) to stream, e.g. after a beep
      if (outputOpen) closeOutput();
      continue;
    }
    if (!openOutput()) {
      noiseWanted = kNone;  // no codec: don't spin on it
      playing = kNone;
      continue;
    }
    syncVolume();

    const uint8_t want = noiseWanted;
    // Fade toward 1 while the wanted kind plays; toward 0 to stop or switch.
    gen.fill(noise::Kind(playing), mono, kChunk);
    const float target = want == playing ? 1.0f : 0.0f;
    for (uint32_t i = 0; i < kChunk; i++) {
      gain += gain < target ? fadeStep : gain > target ? -fadeStep * 2 : 0;
      gain = gain < 0 ? 0 : gain > 1 ? 1 : gain;
      mono[i] = int16_t(mono[i] * gain * gain);  // squared: a smoother, more even fade
    }
    writeMono(mono, kChunk);
    if (gain == 0 && want != playing) playing = want;  // switch kinds, or stop
  }
}

void begin() {
  gpio_hold_dis(gpio_num_t(PIN_PA_EN));  // held LOW through deep sleep (power.cpp)
  pinMode(PIN_PA_EN, OUTPUT);
  digitalWrite(PIN_PA_EN, LOW);
  queue = xQueueCreate(4, sizeof(Msg));
  xTaskCreatePinnedToCore(task, "sound", 4096, nullptr, 5, nullptr, 0);
}

void play(Pattern p) {
  if (!queue || settings::muted()) return;
  const Msg m{Cmd::Beep, uint8_t(p)};
  xQueueSend(queue, &m, 0);
}

void applyVolume() { volumeDirty = true; }

// Noise requests only set the wanted kind; the message wakes an idle task.
static void wantNoise(uint8_t kind) {
  noiseWanted = kind;
  const Msg m{Cmd::Noise, kind};
  if (queue) xQueueSend(queue, &m, 0);
}

void startNoise(noise::Kind kind) { wantNoise(uint8_t(kind)); }
void stopNoise() { wantNoise(kNone); }
bool noisePlaying() { return noiseWanted != kNone; }
bool busy() { return outputOpen; }

}  // namespace core::sound
