// Hearth Channel — lv2.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <new>

#include <lv2/core/lv2.h>

#include "channel_strip.hpp"
#include "param_info.hpp"
#include "params.hpp"

namespace hearth {

class HearthInstance {
public:
  float* ports[PORT_COUNT] = {};
  float sampleRate = 48000.0f;
  ChannelStrip strip;
  Params params;
};

static const ParamInfo& infoFor(uint32_t index) noexcept {
  for (int i = 0; i < kParamInfoCount; ++i) {
    if (kParams[i].index == index) {
      return kParams[i];
    }
  }
  static ParamInfo fallback;
  fallback.index = index;
  return fallback;
}

static float ctrlValue(float* const* ports, uint32_t index, float fallback) noexcept {
  const ParamInfo& info = infoFor(index);
  float v = fallback;
  if (index < PORT_COUNT && ports[index]) {
    v = ports[index][0];
  }
  if (!std::isfinite(v)) {
    v = fallback;
  }
  const float lo = std::min(info.min, info.max);
  const float hi = std::max(info.min, info.max);
  if (hi > lo) {
    v = std::min(std::max(v, lo), hi);
  }
  return v;
}

static void readParams(HearthInstance* self) noexcept {
  self->params.inputGainDb = ctrlValue(self->ports, PORT_INPUT_GAIN, 0.0f);
  self->params.driveDb = ctrlValue(self->ports, PORT_DRIVE, 0.0f);
  self->params.character =
      ctrlValue(self->ports, PORT_CHARACTER, 25.0f) / 100.0f;
  self->params.hpfFreq = ctrlValue(self->ports, PORT_HPF_FREQ, 0.0f);
  self->params.lowFreq = ctrlValue(self->ports, PORT_LOW_FREQ, 110.0f);
  self->params.lowGainDb = ctrlValue(self->ports, PORT_LOW_GAIN, 0.0f);
  self->params.midFreq = ctrlValue(self->ports, PORT_MID_FREQ, 1600.0f);
  self->params.midGainDb = ctrlValue(self->ports, PORT_MID_GAIN, 0.0f);
  self->params.midQ = ctrlValue(self->ports, PORT_MID_Q, 0.7f);
  self->params.highFreq = ctrlValue(self->ports, PORT_HIGH_FREQ, 12000.0f);
  self->params.highGainDb = ctrlValue(self->ports, PORT_HIGH_GAIN, 0.0f);

  self->params.compOn =
      ctrlValue(self->ports, PORT_COMP_ON, 1.0f) > 0.5f;
  self->params.compFeedback =
      ctrlValue(self->ports, PORT_COMP_MODE, 0.0f) > 0.5f;
  self->params.compMix =
      ctrlValue(self->ports, PORT_COMP_MIX, 100.0f) / 100.0f;
  self->params.compThresholdDb = ctrlValue(self->ports, PORT_COMP_THRESHOLD, -18.0f);
  self->params.compRatio = ctrlValue(self->ports, PORT_COMP_RATIO, 4.0f);
  self->params.compAttackMs = ctrlValue(self->ports, PORT_COMP_ATTACK, 10.0f);
  self->params.compReleaseMs = ctrlValue(self->ports, PORT_COMP_RELEASE, 250.0f);
  self->params.compKneeDb = ctrlValue(self->ports, PORT_COMP_KNEE, 6.0f);
  self->params.compMakeupDb = ctrlValue(self->ports, PORT_COMP_MAKEUP, 0.0f);

  self->params.outputDb = ctrlValue(self->ports, PORT_OUTPUT_GAIN, 0.0f);
  self->params.enabled = ctrlValue(self->ports, PORT_ENABLE, 1.0f) > 0.5f;
}

} // namespace hearth

using namespace hearth;

extern "C" LV2_Handle lv2_hearth_instantiate(const LV2_Descriptor* descriptor,
                                           double rate, const char* bundle_path,
                                           const LV2_Feature* const* features) {
  (void)descriptor;
  (void)bundle_path;
  (void)features;

  hearth::HearthInstance* self = new (std::nothrow) hearth::HearthInstance();
  if (!self) {
    return nullptr;
  }
  self->sampleRate = std::max(8000.0f, static_cast<float>(rate));
  self->strip.prepare(self->sampleRate);

  hearth::readParams(self);
  self->strip.setTargets(self->params);
  self->strip.reset();
  return self;
}

extern "C" void lv2_hearth_connect_port(LV2_Handle handle, uint32_t port,
                                      void* data_location) {
  hearth::HearthInstance* self = static_cast<hearth::HearthInstance*>(handle);
  if (!self) {
    return;
  }
  if (port < hearth::PORT_COUNT) {
    self->ports[port] = static_cast<float*>(data_location);
  }
}

extern "C" void lv2_hearth_activate(LV2_Handle handle) {
  hearth::HearthInstance* self = static_cast<hearth::HearthInstance*>(handle);
  if (!self) {
    return;
  }
  hearth::readParams(self);
  self->strip.setTargets(self->params);
  self->strip.reset();
}

extern "C" void lv2_hearth_run(LV2_Handle handle, uint32_t sampleCount) {
  hearth::HearthInstance* self = static_cast<hearth::HearthInstance*>(handle);
  if (!self) {
    return;
  }

  const bool audioValid = self->ports[PORT_AUDIO_IN_L] && self->ports[PORT_AUDIO_IN_R] &&
                           self->ports[PORT_AUDIO_OUT_L] &&
                           self->ports[PORT_AUDIO_OUT_R];

  float grDb = 0.0f;
  float outDb = -60.0f;
  if (audioValid) {
    hearth::readParams(self);
    self->strip.setTargets(self->params);
    self->strip.process(self->ports[PORT_AUDIO_IN_L], self->ports[PORT_AUDIO_IN_R],
                        self->ports[PORT_AUDIO_OUT_L], self->ports[PORT_AUDIO_OUT_R],
                        sampleCount, grDb, outDb);
  }

  float* meterGr = self->ports[PORT_METER_GR];
  float* meterOut = self->ports[PORT_METER_OUT];
  if (meterGr) {
    *meterGr = grDb;
  }
  if (meterOut) {
    *meterOut = outDb;
  }
}

extern "C" void lv2_hearth_deactivate(LV2_Handle handle) {
  hearth::HearthInstance* self = static_cast<hearth::HearthInstance*>(handle);
  if (!self) {
    return;
  }
  self->strip.reset();
}

extern "C" void lv2_hearth_cleanup(LV2_Handle handle) {
  delete static_cast<hearth::HearthInstance*>(handle);
}

extern "C" const void* lv2_hearth_extension_data(const char* uri) {
  (void)uri;
  return nullptr;
}

static const LV2_Descriptor hearth_descriptor = {
    "https://github.com/tylorismusic-commits/hearth",
    &lv2_hearth_instantiate,
    &lv2_hearth_connect_port,
    &lv2_hearth_activate,
    &lv2_hearth_run,
    &lv2_hearth_deactivate,
    &lv2_hearth_cleanup,
    &lv2_hearth_extension_data,
};

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
  if (index == 0) {
    return &hearth_descriptor;
  }
  return nullptr;
}

