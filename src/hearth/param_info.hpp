// Hearth Channel — param_info.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace hearth {

enum class Unit {
  Unitless = 0,
  Db = 1,
  Hz = 2,
  Ms = 3,
  Percent = 4,
};

// Control ports, in ascending port index order, interleaved with the stereo
// audio ports as defined in the plugin RDF.
enum PortIndex : uint32_t {
  PORT_AUDIO_IN_L = 0,
  PORT_AUDIO_IN_R = 1,
  PORT_AUDIO_OUT_L = 2,
  PORT_AUDIO_OUT_R = 3,
  PORT_INPUT_GAIN = 4,
  PORT_DRIVE = 5,
  PORT_CHARACTER = 6,
  PORT_HPF_FREQ = 7,
  PORT_LOW_FREQ = 8,
  PORT_LOW_GAIN = 9,
  PORT_MID_FREQ = 10,
  PORT_MID_GAIN = 11,
  PORT_MID_Q = 12,
  PORT_HIGH_FREQ = 13,
  PORT_HIGH_GAIN = 14,
  PORT_COMP_ON = 15,
  PORT_COMP_THRESHOLD = 16,
  PORT_COMP_RATIO = 17,
  PORT_COMP_ATTACK = 18,
  PORT_COMP_RELEASE = 19,
  PORT_COMP_KNEE = 20,
  PORT_COMP_MAKEUP = 21,
  PORT_COMP_MIX = 22,
  PORT_COMP_MODE = 23,
  PORT_OUTPUT_GAIN = 24,
  PORT_ENABLE = 25,
  PORT_METER_GR = 26,
  PORT_METER_OUT = 27,
  PORT_COUNT = 28
};

inline constexpr uint32_t kHeart_PortCount = PORT_COUNT;

enum ParamGroup : uint32_t {
  GROUP_PREAMP = 0,
  GROUP_EQ = 1,
  GROUP_COMP = 2,
  GROUP_OUTPUT = 3,
  GROUP_GLOBAL = 4,
};

struct ScalePoint {
  float value = 0.0f;
  const char* label = "";
};

struct ParamInfo {
  uint32_t index = 0;
  const char* symbol = "";
  const char* name = "";
  float min = 0.0f;
  float max = 1.0f;
  float def = 0.0f;
  Unit unit = Unit::Unitless;
  bool logarithmic = false;
  bool toggle = false;
  bool enumeration = false;
  int group = GROUP_PREAMP;
  const ScalePoint* points = nullptr;
  int pointCount = 0;
};

inline constexpr ScalePoint kScaleOff[] = {
    {0.0f, "OFF"}, {50.0f, "50"}, {80.0f, "80"}, {160.0f, "160"}, {300.0f, "300"}};

inline constexpr ScalePoint kScaleLow[] = {
    {35.0f, "35"}, {60.0f, "60"}, {110.0f, "110"}, {220.0f, "220"}};

inline constexpr ScalePoint kScaleMid[] = {
    {360.0f, "360"}, {700.0f, "700"}, {1600.0f, "1.6k"}, {3200.0f, "3.2k"},
    {4800.0f, "4.8k"}, {7200.0f, "7.2k"}};

inline constexpr ScalePoint kScaleHigh[] = {
    {10000.0f, "10k"}, {12000.0f, "12k"}, {16000.0f, "16k"}};

inline constexpr ScalePoint kScaleMode[] = {
    {0.0f, "Feed-forward"}, {1.0f, "Feedback"}};

inline constexpr ParamInfo kParams[] = {
    // Audio ports (indices 0-3) are handled in the LV2 wrapper; the table
    // below lists the control ports only, in ascending order.
    {4, "input_gain", "Input Gain", -20.0f, 30.0f, 0.0f, Unit::Db, false, false,
     false, GROUP_PREAMP, nullptr, 0},
    {5, "drive", "Drive", 0.0f, 12.0f, 0.0f, Unit::Db, false, false, false,
     GROUP_PREAMP, nullptr, 0},
    {6, "character", "Character", 0.0f, 100.0f, 25.0f, Unit::Percent, false,
     false, false, GROUP_PREAMP, nullptr, 0},
    {7, "hpf", "High-Pass", 0.0f, 300.0f, 0.0f, Unit::Hz, false, false, false,
     GROUP_EQ, kScaleOff, 5},
    {8, "low_freq", "Low Freq", 30.0f, 250.0f, 110.0f, Unit::Hz, true, false, false,
     GROUP_EQ, kScaleLow, 4},
    {9, "low_gain", "Low Gain", -8.0f, 8.0f, 0.0f, Unit::Db, false, false, false,
     GROUP_EQ, nullptr, 0},
    {10, "mid_freq", "Mid Freq", 200.0f, 8000.0f, 1600.0f, Unit::Hz, true, false,
     false, GROUP_EQ, kScaleMid, 6},
    {11, "mid_gain", "Mid Gain", -9.0f, 9.0f, 0.0f, Unit::Db, false, false, false,
     GROUP_EQ, nullptr, 0},
    {12, "mid_q", "Mid Q", 0.2f, 2.0f, 0.4f, Unit::Unitless, true, false, false, GROUP_EQ,
     nullptr, 0},
    {13, "high_freq", "High Freq", 6000.0f, 16000.0f, 12000.0f, Unit::Hz, true,
     false, false, GROUP_EQ, kScaleHigh, 3},
    {14, "high_gain", "High Gain", -8.0f, 8.0f, 0.0f, Unit::Db, false, false, false,
     GROUP_EQ, nullptr, 0},
    {15, "comp_on", "Compressor", 0.0f, 1.0f, 1.0f, Unit::Unitless, false, true, false,
     GROUP_COMP, nullptr, 0},
    {16, "comp_threshold", "Threshold", -60.0f, 0.0f, -18.0f, Unit::Db, false, false,
     false, GROUP_COMP, nullptr, 0},
    {17, "comp_ratio", "Ratio", 1.0f, 20.0f, 4.0f, Unit::Unitless, true, false, false,
     GROUP_COMP, nullptr, 0},
    {18, "comp_attack", "Attack", 0.1f, 100.0f, 10.0f, Unit::Ms, true, false, false,
     GROUP_COMP, nullptr, 0},
    {19, "comp_release", "Release", 20.0f, 2000.0f, 250.0f, Unit::Ms, true, false,
     false, GROUP_COMP, nullptr, 0},
    {20, "comp_knee", "Knee", 0.0f, 24.0f, 6.0f, Unit::Db, false, false, false, GROUP_COMP,
     nullptr, 0},
    {21, "comp_makeup", "Makeup", 0.0f, 30.0f, 0.0f, Unit::Db, false, false, false,
     GROUP_COMP, nullptr, 0},
    {22, "comp_mix", "Mix", 0.0f, 100.0f, 100.0f, Unit::Percent, false, false, false,
     GROUP_COMP, nullptr, 0},
    {23, "comp_mode", "Topology", 0.0f, 1.0f, 0.0f, Unit::Unitless, false, false, true,
     GROUP_COMP, kScaleMode, 2},
    {24, "output_gain", "Output", -60.0f, 12.0f, 0.0f, Unit::Db, false, false, false,
     GROUP_OUTPUT, nullptr, 0},
    {25, "enable", "Enabled", 0.0f, 1.0f, 1.0f, Unit::Unitless, false, true, false, GROUP_GLOBAL,
     nullptr, 0},
};

inline constexpr int kParamInfoCount =
    static_cast<int>(sizeof(kParams) / sizeof(kParams[0]));

// The two meter ports are control outputs used by the host for display.
inline constexpr uint32_t kHeart_MeterGrPort = PORT_METER_GR;
inline constexpr uint32_t kHeart_MeterOutPort = PORT_METER_OUT;

} // namespace hearth
