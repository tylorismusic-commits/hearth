// Hearth Channel — X11/Cairo console-strip UI
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <cairo/cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <lv2/atom/atom.h>
#include <lv2/options/options.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>

#include "param_info.hpp"

namespace hearth {
namespace ui {

// Logical layout.  The UI is rendered at this size and scaled to whatever the
// host gives us, so it stays sharp and fits small and large displays.
constexpr int UI_W = 620;
constexpr int UI_H = 900;
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Palette — a vintage console faceplate rather than a modern dark rack.
// ---------------------------------------------------------------------------
struct Color {
  double r, g, b, a;
};

constexpr Color kBg{0.050, 0.060, 0.072, 1.0};
constexpr Color kHeaderTop{0.105, 0.135, 0.165, 1.0};
constexpr Color kHeaderBottom{0.060, 0.080, 0.100, 1.0};
constexpr Color kPanelTop{0.285, 0.355, 0.425, 1.0};
constexpr Color kPanelBottom{0.195, 0.255, 0.315, 1.0};
constexpr Color kPanelEdge{0.430, 0.505, 0.580, 1.0};
constexpr Color kPanelInner{0.560, 0.630, 0.690, 1.0};
constexpr Color kText{0.945, 0.915, 0.835, 1.0};
constexpr Color kCream{0.960, 0.915, 0.795, 1.0};
constexpr Color kMuted{0.740, 0.710, 0.620, 1.0};
constexpr Color kRed{0.720, 0.250, 0.215, 1.0};
constexpr Color kRedDark{0.330, 0.100, 0.090, 1.0};
constexpr Color kAmber{0.850, 0.650, 0.350, 1.0};
constexpr Color kKnobTop{0.200, 0.220, 0.245, 1.0};
constexpr Color kKnobBottom{0.045, 0.052, 0.060, 1.0};
constexpr Color kKnobRing{0.560, 0.610, 0.660, 1.0};
constexpr Color kSilver{0.720, 0.740, 0.750, 1.0};
constexpr Color kTrack{0.035, 0.042, 0.050, 1.0};
constexpr Color kMeterFace{0.930, 0.890, 0.780, 1.0};
constexpr Color kMeterInk{0.090, 0.080, 0.065, 1.0};
constexpr Color kGreen{0.400, 0.680, 0.430, 1.0};

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
enum class Kind { Knob, Switch, Toggle, Fader, Vu, Meter };

struct Widget {
  Kind kind;
  uint32_t port;
  bool bipolar;
  float cx, cy, r;
  float x, y, w, h;
};

constexpr Widget K(uint32_t port, float cx, float cy, float r,
                   bool bipolar = false) {
  return Widget{Kind::Knob, port, bipolar, cx, cy, r, 0, 0, 0, 0};
}
constexpr Widget S(uint32_t port, float cx, float cy, float r) {
  return Widget{Kind::Switch, port, false, cx, cy, r, 0, 0, 0, 0};
}
constexpr Widget T(uint32_t port, float x, float y, float w, float h) {
  return Widget{Kind::Toggle, port, false, 0, 0, 0, x, y, w, h};
}
constexpr Widget F(uint32_t port, float x, float y, float w, float h) {
  return Widget{Kind::Fader, port, false, 0, 0, 0, x, y, w, h};
}
constexpr Widget V(uint32_t port, float x, float y, float w, float h) {
  return Widget{Kind::Vu, port, false, 0, 0, 0, x, y, w, h};
}
constexpr Widget M(uint32_t port, float x, float y, float w, float h) {
  return Widget{Kind::Meter, port, false, 0, 0, 0, x, y, w, h};
}

constexpr Widget kWidgets[] = {
    // Preamp
    K(PORT_INPUT_GAIN, 100.0f, 168.0f, 36.0f),
    K(PORT_DRIVE, 230.0f, 168.0f, 28.0f),
    K(PORT_CHARACTER, 340.0f, 168.0f, 28.0f),

    // EQ: high-pass, low, mid, high.  Frequency selectors are rotary switches.
    S(PORT_HPF_FREQ, 78.0f, 352.0f, 27.0f),
    S(PORT_LOW_FREQ, 210.0f, 352.0f, 27.0f),
    K(PORT_LOW_GAIN, 342.0f, 352.0f, 28.0f, true),
    S(PORT_MID_FREQ, 78.0f, 454.0f, 27.0f),
    K(PORT_MID_GAIN, 210.0f, 454.0f, 28.0f, true),
    K(PORT_MID_Q, 342.0f, 454.0f, 27.0f),
    S(PORT_HIGH_FREQ, 78.0f, 544.0f, 27.0f),
    K(PORT_HIGH_GAIN, 210.0f, 544.0f, 28.0f, true),

    // Compressor
    K(PORT_COMP_THRESHOLD, 70.0f, 706.0f, 25.0f),
    K(PORT_COMP_RATIO, 175.0f, 706.0f, 25.0f),
    K(PORT_COMP_ATTACK, 280.0f, 706.0f, 25.0f),
    K(PORT_COMP_RELEASE, 385.0f, 706.0f, 25.0f),
    K(PORT_COMP_KNEE, 70.0f, 806.0f, 25.0f),
    K(PORT_COMP_MAKEUP, 175.0f, 806.0f, 25.0f),
    K(PORT_COMP_MIX, 280.0f, 806.0f, 25.0f),
    T(PORT_COMP_MODE, 330.0f, 784.0f, 100.0f, 26.0f),
    T(PORT_COMP_ON, 360.0f, 628.0f, 84.0f, 26.0f),

    // Output column
    V(PORT_METER_OUT, 472.0f, 120.0f, 130.0f, 130.0f),
    F(PORT_OUTPUT_GAIN, 480.0f, 270.0f, 64.0f, 520.0f),
    M(PORT_METER_GR, 572.0f, 270.0f, 14.0f, 520.0f),

    // Global
    T(PORT_ENABLE, 512.0f, 22.0f, 88.0f, 32.0f),
};

constexpr int kNumWidgets = static_cast<int>(sizeof(kWidgets) / sizeof(kWidgets[0]));

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
struct State {
  float values[PORT_COUNT] = {};
  float meterGr = 0.0f;
  float meterOut = -60.0f;
  float meterGrSmooth = 0.0f;
  float vuSmooth = -60.0f;
  int hover = -1;
  int active = -1;
  double dragY = 0.0;
  float dragValue = 0.0f;
  int lastClickWidget = -1;
  unsigned long lastClickTime = 0;
  int presetIndex = -1;
  bool presetDirty = false;
  int presetHover = 0;
  bool dirty = true;
};

static const ParamInfo& infoFor(uint32_t port) {
  for (int i = 0; i < kParamInfoCount; ++i) {
    if (kParams[i].index == port) {
      return kParams[i];
    }
  }
  static ParamInfo fallback;
  return fallback;
}

static void initState(State& s) {
  for (int i = 0; i < kParamInfoCount; ++i) {
    s.values[kParams[i].index] = kParams[i].def;
  }
  s.meterGr = 0.0f;
  s.meterOut = -60.0f;
  s.meterGrSmooth = 0.0f;
  s.vuSmooth = -60.0f;
}

static float normOf(const ParamInfo& p, float v) {
  if (p.toggle || p.enumeration) {
    return v >= 0.5f ? 1.0f : 0.0f;
  }
  if (p.logarithmic && p.min > 0.0f && p.max > p.min) {
    return std::log(std::max(v, p.min) / p.min) / std::log(p.max / p.min);
  }
  if (p.max <= p.min) {
    return 0.0f;
  }
  return (v - p.min) / (p.max - p.min);
}

static float valueOf(const ParamInfo& p, float n) {
  n = std::min(std::max(n, 0.0f), 1.0f);
  if (p.toggle || p.enumeration) {
    return n >= 0.5f ? 1.0f : 0.0f;
  }
  if (p.logarithmic && p.min > 0.0f && p.max > p.min) {
    return p.min * std::pow(p.max / p.min, n);
  }
  return p.min + n * (p.max - p.min);
}

static int switchIndex(const ParamInfo& p, float value) {
  if (!p.points || p.pointCount <= 0) {
    return 0;
  }
  int best = 0;
  float bestDistance = 1.0e30f;
  for (int i = 0; i < p.pointCount; ++i) {
    const float d = std::fabs(value - p.points[i].value);
    if (d < bestDistance) {
      bestDistance = d;
      best = i;
    }
  }
  return best;
}

static const char* shortLabel(uint32_t port) {
  switch (port) {
    case PORT_INPUT_GAIN: return "INPUT";
    case PORT_DRIVE: return "DRIVE";
    case PORT_CHARACTER: return "CHARACTER";
    case PORT_HPF_FREQ: return "HIGH PASS";
    case PORT_LOW_FREQ: return "LOW FREQ";
    case PORT_LOW_GAIN: return "LOW GAIN";
    case PORT_MID_FREQ: return "MID FREQ";
    case PORT_MID_GAIN: return "MID GAIN";
    case PORT_MID_Q: return "MID Q";
    case PORT_HIGH_FREQ: return "HIGH FREQ";
    case PORT_HIGH_GAIN: return "HIGH GAIN";
    case PORT_COMP_THRESHOLD: return "THRESH";
    case PORT_COMP_RATIO: return "RATIO";
    case PORT_COMP_ATTACK: return "ATTACK";
    case PORT_COMP_RELEASE: return "RELEASE";
    case PORT_COMP_KNEE: return "KNEE";
    case PORT_COMP_MAKEUP: return "MAKEUP";
    case PORT_COMP_MIX: return "MIX";
    default: return "";
  }
}

static const char* switchCaption(uint32_t port) {
  switch (port) {
    case PORT_HPF_FREQ: return "HIGH PASS";
    case PORT_LOW_FREQ: return "LOW";
    case PORT_MID_FREQ: return "MID";
    case PORT_HIGH_FREQ: return "HIGH";
    default: return "";
  }
}

static std::string formatValue(uint32_t port, float v) {
  const ParamInfo& p = infoFor(port);
  char buf[64];
  if (p.toggle) {
    return v >= 0.5f ? "ON" : "OFF";
  }
  if (p.enumeration) {
    if (port == PORT_COMP_MODE) {
      return v >= 0.5f ? "FEEDBACK" : "FEED-FWD";
    }
    return v >= 0.5f ? "1" : "0";
  }
  if (p.points && p.pointCount > 0) {
    const int idx = switchIndex(p, v);
    return p.points[idx].label;
  }
  switch (p.unit) {
    case Unit::Db:
      std::snprintf(buf, sizeof(buf), "%+.1f dB", v);
      return buf;
    case Unit::Hz:
      if (port == PORT_HPF_FREQ && v < 1.0f) return "OFF";
      if (v >= 1000.0f) {
        std::snprintf(buf, sizeof(buf), "%.2f kHz", v / 1000.0f);
      } else {
        std::snprintf(buf, sizeof(buf), "%.0f Hz", v);
      }
      return buf;
    case Unit::Ms:
      if (v >= 100.0f) {
        std::snprintf(buf, sizeof(buf), "%.0f ms", v);
      } else {
        std::snprintf(buf, sizeof(buf), "%.1f ms", v);
      }
      return buf;
    case Unit::Percent:
      std::snprintf(buf, sizeof(buf), "%.0f%%", v);
      return buf;
    case Unit::Unitless:
    default:
      break;
  }
  if (port == PORT_COMP_RATIO) {
    std::snprintf(buf, sizeof(buf), "%.1f:1", v);
  } else if (port == PORT_MID_Q) {
    std::snprintf(buf, sizeof(buf), "%.2f", v);
  } else {
    std::snprintf(buf, sizeof(buf), "%.1f", v);
  }
  return buf;
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------
struct PresetValue {
  uint32_t port;
  float value;
};

struct Preset {
  const char* name;
  const char* shortName;
  const PresetValue* values;
  int count;
};

#define PV(port_, value_) PresetValue{port_, value_}

constexpr PresetValue kPresetVocal[] = {
    PV(PORT_INPUT_GAIN, 4.0f), PV(PORT_DRIVE, 2.0f), PV(PORT_CHARACTER, 30.0f),
    PV(PORT_HPF_FREQ, 80.0f), PV(PORT_LOW_FREQ, 110.0f), PV(PORT_LOW_GAIN, -1.0f),
    PV(PORT_MID_FREQ, 3200.0f), PV(PORT_MID_GAIN, 2.0f), PV(PORT_MID_Q, 0.5f),
    PV(PORT_HIGH_FREQ, 12000.0f), PV(PORT_HIGH_GAIN, 2.0f),
    PV(PORT_COMP_ON, 1.0f), PV(PORT_COMP_THRESHOLD, -24.0f),
    PV(PORT_COMP_RATIO, 3.0f), PV(PORT_COMP_ATTACK, 5.0f),
    PV(PORT_COMP_RELEASE, 150.0f), PV(PORT_COMP_KNEE, 6.0f),
    PV(PORT_COMP_MAKEUP, 3.0f), PV(PORT_COMP_MIX, 100.0f),
    PV(PORT_COMP_MODE, 0.0f), PV(PORT_OUTPUT_GAIN, -2.0f), PV(PORT_ENABLE, 1.0f)};

constexpr PresetValue kPresetDrumBus[] = {
    PV(PORT_INPUT_GAIN, 2.0f), PV(PORT_DRIVE, 3.0f), PV(PORT_CHARACTER, 40.0f),
    PV(PORT_HPF_FREQ, 0.0f), PV(PORT_LOW_FREQ, 60.0f), PV(PORT_LOW_GAIN, 1.5f),
    PV(PORT_MID_FREQ, 700.0f), PV(PORT_MID_GAIN, -2.0f), PV(PORT_MID_Q, 0.45f),
    PV(PORT_HIGH_FREQ, 12000.0f), PV(PORT_HIGH_GAIN, 1.0f),
    PV(PORT_COMP_ON, 1.0f), PV(PORT_COMP_THRESHOLD, -20.0f),
    PV(PORT_COMP_RATIO, 4.0f), PV(PORT_COMP_ATTACK, 20.0f),
    PV(PORT_COMP_RELEASE, 200.0f), PV(PORT_COMP_KNEE, 8.0f),
    PV(PORT_COMP_MAKEUP, 3.0f), PV(PORT_COMP_MIX, 70.0f),
    PV(PORT_COMP_MODE, 1.0f), PV(PORT_OUTPUT_GAIN, -3.0f), PV(PORT_ENABLE, 1.0f)};

constexpr PresetValue kPresetBass[] = {
    PV(PORT_INPUT_GAIN, 6.0f), PV(PORT_DRIVE, 4.0f), PV(PORT_CHARACTER, 45.0f),
    PV(PORT_HPF_FREQ, 30.0f), PV(PORT_LOW_FREQ, 60.0f), PV(PORT_LOW_GAIN, 3.0f),
    PV(PORT_MID_FREQ, 700.0f), PV(PORT_MID_GAIN, -2.5f), PV(PORT_MID_Q, 0.45f),
    PV(PORT_HIGH_FREQ, 10000.0f), PV(PORT_HIGH_GAIN, 1.0f),
    PV(PORT_COMP_ON, 1.0f), PV(PORT_COMP_THRESHOLD, -26.0f),
    PV(PORT_COMP_RATIO, 3.0f), PV(PORT_COMP_ATTACK, 15.0f),
    PV(PORT_COMP_RELEASE, 300.0f), PV(PORT_COMP_KNEE, 6.0f),
    PV(PORT_COMP_MAKEUP, 4.0f), PV(PORT_COMP_MIX, 100.0f),
    PV(PORT_COMP_MODE, 0.0f), PV(PORT_OUTPUT_GAIN, -2.0f), PV(PORT_ENABLE, 1.0f)};

constexpr PresetValue kPresetMixBus[] = {
    PV(PORT_INPUT_GAIN, 0.0f), PV(PORT_DRIVE, 1.0f), PV(PORT_CHARACTER, 25.0f),
    PV(PORT_HPF_FREQ, 20.0f), PV(PORT_LOW_FREQ, 110.0f), PV(PORT_LOW_GAIN, 0.5f),
    PV(PORT_MID_FREQ, 1600.0f), PV(PORT_MID_GAIN, -0.5f), PV(PORT_MID_Q, 0.4f),
    PV(PORT_HIGH_FREQ, 12000.0f), PV(PORT_HIGH_GAIN, 0.5f),
    PV(PORT_COMP_ON, 1.0f), PV(PORT_COMP_THRESHOLD, -16.0f),
    PV(PORT_COMP_RATIO, 2.0f), PV(PORT_COMP_ATTACK, 30.0f),
    PV(PORT_COMP_RELEASE, 400.0f), PV(PORT_COMP_KNEE, 10.0f),
    PV(PORT_COMP_MAKEUP, 2.0f), PV(PORT_COMP_MIX, 100.0f),
    PV(PORT_COMP_MODE, 1.0f), PV(PORT_OUTPUT_GAIN, -1.0f), PV(PORT_ENABLE, 1.0f)};

constexpr PresetValue kPresetAcoustic[] = {
    PV(PORT_INPUT_GAIN, 3.0f), PV(PORT_DRIVE, 2.0f), PV(PORT_CHARACTER, 35.0f),
    PV(PORT_HPF_FREQ, 80.0f), PV(PORT_LOW_FREQ, 110.0f), PV(PORT_LOW_GAIN, -1.0f),
    PV(PORT_MID_FREQ, 1600.0f), PV(PORT_MID_GAIN, 1.5f), PV(PORT_MID_Q, 0.5f),
    PV(PORT_HIGH_FREQ, 12000.0f), PV(PORT_HIGH_GAIN, 1.5f),
    PV(PORT_COMP_ON, 1.0f), PV(PORT_COMP_THRESHOLD, -22.0f),
    PV(PORT_COMP_RATIO, 2.5f), PV(PORT_COMP_ATTACK, 8.0f),
    PV(PORT_COMP_RELEASE, 180.0f), PV(PORT_COMP_KNEE, 6.0f),
    PV(PORT_COMP_MAKEUP, 2.0f), PV(PORT_COMP_MIX, 100.0f),
    PV(PORT_COMP_MODE, 0.0f), PV(PORT_OUTPUT_GAIN, -1.5f), PV(PORT_ENABLE, 1.0f)};

constexpr PresetValue kPresetBroken[] = {
    PV(PORT_INPUT_GAIN, 10.0f), PV(PORT_DRIVE, 12.0f), PV(PORT_CHARACTER, 100.0f),
    PV(PORT_HPF_FREQ, 160.0f), PV(PORT_LOW_FREQ, 60.0f), PV(PORT_LOW_GAIN, 8.0f),
    PV(PORT_MID_FREQ, 700.0f), PV(PORT_MID_GAIN, 9.0f), PV(PORT_MID_Q, 1.5f),
    PV(PORT_HIGH_FREQ, 16000.0f), PV(PORT_HIGH_GAIN, 8.0f),
    PV(PORT_COMP_ON, 1.0f), PV(PORT_COMP_THRESHOLD, -50.0f),
    PV(PORT_COMP_RATIO, 20.0f), PV(PORT_COMP_ATTACK, 0.1f),
    PV(PORT_COMP_RELEASE, 20.0f), PV(PORT_COMP_KNEE, 0.0f),
    PV(PORT_COMP_MAKEUP, 15.0f), PV(PORT_COMP_MIX, 100.0f),
    PV(PORT_COMP_MODE, 1.0f), PV(PORT_OUTPUT_GAIN, 6.0f), PV(PORT_ENABLE, 1.0f)};

#undef PV

constexpr Preset kPresets[] = {
    {"Vocal - Air & Control", "VOCAL", kPresetVocal,
     static_cast<int>(sizeof(kPresetVocal) / sizeof(kPresetVocal[0]))},
    {"Drum Bus - Glue", "DRUM BUS", kPresetDrumBus,
     static_cast<int>(sizeof(kPresetDrumBus) / sizeof(kPresetDrumBus[0]))},
    {"Bass DI - Weight", "BASS DI", kPresetBass,
     static_cast<int>(sizeof(kPresetBass) / sizeof(kPresetBass[0]))},
    {"Mix Bus - British Glue", "MIX BUS", kPresetMixBus,
     static_cast<int>(sizeof(kPresetMixBus) / sizeof(kPresetMixBus[0]))},
    {"Acoustic - Natural", "ACOUSTIC", kPresetAcoustic,
     static_cast<int>(sizeof(kPresetAcoustic) / sizeof(kPresetAcoustic[0]))},
    {"Broken Console", "BROKEN", kPresetBroken,
     static_cast<int>(sizeof(kPresetBroken) / sizeof(kPresetBroken[0]))},
};

constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

struct Rect {
  double x, y, w, h;
};

constexpr Rect kPresetLcd{170.0, 24.0, 190.0, 36.0};
constexpr Rect kPresetPrev{366.0, 24.0, 26.0, 36.0};
constexpr Rect kPresetNext{396.0, 24.0, 26.0, 36.0};

static bool inRect(double x, double y, const Rect& r) {
  return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
static void setColor(cairo_t* cr, const Color& c) {
  cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
}

static void setColorA(cairo_t* cr, const Color& c, double alpha) {
  cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha);
}

static void roundedRect(cairo_t* cr, double x, double y, double w, double h,
                        double r) {
  r = std::min(r, std::min(w, h) * 0.5);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -kPi * 0.5, 0.0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0.0, kPi * 0.5);
  cairo_arc(cr, x + r, y + h - r, r, kPi * 0.5, kPi);
  cairo_arc(cr, x + r, y + r, r, kPi, kPi * 1.5);
  cairo_close_path(cr);
}

static void textAt(cairo_t* cr, double x, double y, const char* text,
                   double size, bool bold, const Color& c,
                   const char* family = "DejaVu Sans") {
  cairo_select_font_face(cr, family, CAIRO_FONT_SLANT_NORMAL,
                         bold ? CAIRO_FONT_WEIGHT_BOLD
                              : CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, size);
  setColor(cr, c);
  cairo_move_to(cr, x, y);
  cairo_show_text(cr, text);
}

static void textCentered(cairo_t* cr, double cx, double y, const char* text,
                         double size, bool bold, const Color& c,
                         const char* family = "DejaVu Sans") {
  cairo_select_font_face(cr, family, CAIRO_FONT_SLANT_NORMAL,
                         bold ? CAIRO_FONT_WEIGHT_BOLD
                              : CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, size);
  cairo_text_extents_t ext;
  cairo_text_extents(cr, text, &ext);
  setColor(cr, c);
  cairo_move_to(cr, cx - (ext.width * 0.5 + ext.x_bearing), y);
  cairo_show_text(cr, text);
}

static void paintTexture(cairo_t* cr, double x, double y, double w, double h,
                         double alpha);

static void drawScrew(cairo_t* cr, double x, double y) {
  cairo_arc(cr, x, y + 0.8, 5.2, 0.0, kPi * 2.0);
  setColorA(cr, kBg, 0.55);
  cairo_fill(cr);

  cairo_pattern_t* head = cairo_pattern_create_radial(
      x - 1.8, y - 1.8, 0.5, x, y, 4.8);
  cairo_pattern_add_color_stop_rgb(head, 0.0, 0.74, 0.75, 0.72);
  cairo_pattern_add_color_stop_rgb(head, 0.55, 0.40, 0.42, 0.42);
  cairo_pattern_add_color_stop_rgb(head, 1.0, 0.13, 0.15, 0.17);
  cairo_arc(cr, x, y, 4.2, 0.0, kPi * 2.0);
  cairo_set_source(cr, head);
  cairo_fill(cr);
  cairo_pattern_destroy(head);

  cairo_arc(cr, x, y, 4.2, 0.0, kPi * 2.0);
  setColorA(cr, kBg, 0.85);
  cairo_set_line_width(cr, 0.8);
  cairo_stroke(cr);

  setColorA(cr, kBg, 0.8);
  cairo_set_line_width(cr, 1.2);
  cairo_move_to(cr, x - 2.2, y - 2.2);
  cairo_line_to(cr, x + 2.2, y + 2.2);
  cairo_stroke(cr);

  setColorA(cr, Color{1.0, 1.0, 1.0, 1.0}, 0.30);
  cairo_set_line_width(cr, 0.7);
  cairo_move_to(cr, x - 2.0, y - 2.6);
  cairo_line_to(cr, x + 1.6, y - 1.0);
  cairo_stroke(cr);
}

static void drawPanel(cairo_t* cr, double x, double y, double w, double h,
                      const char* title) {
  // raised chassis shadow
  roundedRect(cr, x + 1.0, y + 4.0, w, h, 8.0);
  setColorA(cr, kBg, 0.38);
  cairo_fill(cr);
  roundedRect(cr, x + 0.5, y + 2.0, w, h, 8.0);
  setColorA(cr, kBg, 0.24);
  cairo_fill(cr);

  cairo_pattern_t* pat = cairo_pattern_create_linear(x, y, x, y + h);
  cairo_pattern_add_color_stop_rgb(pat, 0.0, kPanelTop.r, kPanelTop.g,
                                   kPanelTop.b);
  cairo_pattern_add_color_stop_rgb(pat, 1.0, kPanelBottom.r, kPanelBottom.g,
                                   kPanelBottom.b);
  roundedRect(cr, x, y, w, h, 7.0);
  cairo_set_source(cr, pat);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(pat);
  setColor(cr, kPanelEdge);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  // brushed texture and recessed top shadow inside the panel
  cairo_save(cr);
  roundedRect(cr, x, y, w, h, 7.0);
  cairo_clip(cr);
  paintTexture(cr, x, y, w, h, 0.065);
  cairo_pattern_t* inner = cairo_pattern_create_linear(x, y, x, y + 18.0);
  cairo_pattern_add_color_stop_rgba(inner, 0.0, 0.0, 0.0, 0.0, 0.28);
  cairo_pattern_add_color_stop_rgba(inner, 1.0, 0.0, 0.0, 0.0, 0.0);
  cairo_rectangle(cr, x, y, w, 18.0);
  cairo_set_source(cr, inner);
  cairo_fill(cr);
  cairo_pattern_destroy(inner);
  cairo_restore(cr);

  // top bevel and bottom shadow
  setColorA(cr, kPanelInner, 0.75);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, x + 9.0, y + 1.0);
  cairo_line_to(cr, x + w - 9.0, y + 1.0);
  cairo_stroke(cr);
  setColorA(cr, kBg, 0.55);
  cairo_move_to(cr, x + 9.0, y + h - 1.0);
  cairo_line_to(cr, x + w - 9.0, y + h - 1.0);
  cairo_stroke(cr);

  // recessed title plate
  roundedRect(cr, x + 8.0, y + 8.0, w - 16.0, 27.0, 3.5);
  cairo_pattern_t* plate = cairo_pattern_create_linear(
      x, y + 8.0, x, y + 35.0);
  cairo_pattern_add_color_stop_rgb(plate, 0.0, 0.070, 0.090, 0.110);
  cairo_pattern_add_color_stop_rgb(plate, 1.0, 0.030, 0.040, 0.050);
  cairo_set_source(cr, plate);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(plate);
  setColorA(cr, kPanelEdge, 0.8);
  cairo_set_line_width(cr, 0.8);
  cairo_stroke(cr);

  textAt(cr, x + 16.0, y + 26.0, title, 11.0, true, kText);
  setColor(cr, kRed);
  cairo_rectangle(cr, x + 16.0, y + 33.0, 24.0, 2.0);
  cairo_fill(cr);

  cairo_arc(cr, x + w - 18.0, y + 21.0, 2.2, 0.0, kPi * 2.0);
  setColorA(cr, kRed, 0.85);
  cairo_fill(cr);
  cairo_arc(cr, x + w - 18.0, y + 21.0, 4.5, 0.0, kPi * 2.0);
  setColorA(cr, kRed, 0.15);
  cairo_fill(cr);

  drawScrew(cr, x + 12.0, y + h - 12.0);
  drawScrew(cr, x + w - 12.0, y + h - 12.0);
}

static void debugLog(const char* fmt, ...);

static cairo_surface_t* gKnobImage = nullptr;
static cairo_surface_t* gMetalTexture = nullptr;
static FT_Library gFtLibrary = nullptr;
static FT_Face gLogoFace = nullptr;
static cairo_font_face_t* gLogoFont = nullptr;
static bool gLogoTried = false;
static bool gKnobTried = false;

static void loadKnobAsset(const char* bundlePath) {
  if (gKnobTried || !bundlePath || !*bundlePath) {
    return;
  }
  gKnobTried = true;
  std::string dir(bundlePath);
  if (!dir.empty() && dir.back() != '/') {
    dir += '/';
  }
  const std::string path = dir + "knob.png";
  cairo_surface_t* surface = cairo_image_surface_create_from_png(path.c_str());
  if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
    gKnobImage = surface;
    debugLog("loaded knob asset %s", path.c_str());
  } else {
    cairo_surface_destroy(surface);
    debugLog("could not load knob asset %s", path.c_str());
  }
}

static void loadMetalTexture(const char* bundlePath) {
  if (gMetalTexture || !bundlePath || !*bundlePath) {
    return;
  }
  std::string dir(bundlePath);
  if (!dir.empty() && dir.back() != '/') {
    dir += '/';
  }
  const std::string path = dir + "metal.png";
  cairo_surface_t* surface = cairo_image_surface_create_from_png(path.c_str());
  if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
    gMetalTexture = surface;
  } else {
    cairo_surface_destroy(surface);
  }
}

static void paintTexture(cairo_t* cr, double x, double y, double w, double h,
                         double alpha) {
  if (!gMetalTexture) {
    return;
  }
  cairo_pattern_t* pattern = cairo_pattern_create_for_surface(gMetalTexture);
  cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
  cairo_rectangle(cr, x, y, w, h);
  cairo_set_source(cr, pattern);
  cairo_paint_with_alpha(cr, alpha);
  cairo_pattern_destroy(pattern);
}

static void loadLogoFont(const char* bundlePath) {
  if (gLogoTried || !bundlePath || !*bundlePath) {
    return;
  }
  gLogoTried = true;
  std::string dir(bundlePath);
  if (!dir.empty() && dir.back() != '/') {
    dir += '/';
  }
  const std::string path = dir + "Jost-ExtraBold.ttf";
  if (FT_Init_FreeType(&gFtLibrary) != 0) {
    gFtLibrary = nullptr;
    return;
  }
  if (FT_New_Face(gFtLibrary, path.c_str(), 0, &gLogoFace) != 0) {
    FT_Done_FreeType(gFtLibrary);
    gFtLibrary = nullptr;
    gLogoFace = nullptr;
    return;
  }
  gLogoFont = cairo_ft_font_face_create_for_ft_face(gLogoFace, 0);
  if (cairo_font_face_status(gLogoFont) != CAIRO_STATUS_SUCCESS) {
    cairo_font_face_destroy(gLogoFont);
    gLogoFont = nullptr;
  }
  debugLog("loaded logo font %s", path.c_str());
}

static void drawWordmark(cairo_t* cr, double x, double baseline, double size) {
  if (!gLogoFont) {
    textAt(cr, x, baseline, "Hearth", size, true, kCream);
    return;
  }
  cairo_save(cr);
  cairo_set_font_face(cr, gLogoFont);
  cairo_set_font_size(cr, size);
  setColor(cr, kCream);
  const char* text = "Hearth";
  const double tracking = size * 0.025;
  double px = x;
  for (const char* p = text; *p; ++p) {
    char glyph[2] = {*p, 0};
    cairo_text_extents_t ext;
    cairo_text_extents(cr, glyph, &ext);
    cairo_move_to(cr, px - ext.x_bearing, baseline);
    cairo_show_text(cr, glyph);
    px += ext.x_advance - tracking;
  }
  cairo_restore(cr);
}

static void drawKnobBody(cairo_t* cr, double cx, double cy, double r,
                         bool hovered) {
  // soft contact shadow
  cairo_pattern_t* shadow = cairo_pattern_create_radial(
      cx, cy + 3.0, r * 0.15, cx, cy + 3.0, r * 1.55);
  cairo_pattern_add_color_stop_rgba(shadow, 0.0, 0.0, 0.0, 0.0, 0.55);
  cairo_pattern_add_color_stop_rgba(shadow, 1.0, 0.0, 0.0, 0.0, 0.0);
  cairo_arc(cr, cx, cy + 3.0, r * 1.55, 0.0, kPi * 2.0);
  cairo_set_source(cr, shadow);
  cairo_fill(cr);
  cairo_pattern_destroy(shadow);

  if (gKnobImage) {
    cairo_save(cr);
    cairo_translate(cr, cx - r, cy - r);
    const double scale = (2.0 * r) / 512.0;
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, gKnobImage, 0.0, 0.0);
    cairo_paint(cr);
    cairo_restore(cr);
  } else {
    // procedural fallback
    cairo_pattern_t* pat = cairo_pattern_create_radial(
        cx - r * 0.35, cy - r * 0.40, r * 0.10, cx, cy, r * 1.15);
    cairo_pattern_add_color_stop_rgb(pat, 0.0, kKnobTop.r, kKnobTop.g,
                                     kKnobTop.b);
    cairo_pattern_add_color_stop_rgb(pat, 1.0, kKnobBottom.r, kKnobBottom.g,
                                     kKnobBottom.b);
    cairo_arc(cr, cx, cy, r, 0.0, kPi * 2.0);
    cairo_set_source(cr, pat);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);

    cairo_arc(cr, cx, cy, r, 0.0, kPi * 2.0);
    setColor(cr, kKnobRing);
    cairo_set_line_width(cr, 1.1);
    cairo_stroke(cr);
  }

  // top rim light
  setColorA(cr, Color{1.0, 1.0, 1.0, 1.0}, 0.10);
  cairo_set_line_width(cr, 1.2);
  cairo_arc(cr, cx, cy, r * 0.95, kPi * 1.12, kPi * 1.72);
  cairo_stroke(cr);

  if (hovered) {
    cairo_arc(cr, cx, cy, r, 0.0, kPi * 2.0);
    setColor(cr, kAmber);
    cairo_set_line_width(cr, 1.8);
    cairo_stroke(cr);
  }
}

static void drawKnob(cairo_t* cr, const Widget& w, float value, bool hovered,
                     bool enabled) {
  const ParamInfo& p = infoFor(w.port);
  const float n = normOf(p, value);
  const double start = kPi * 0.75;
  const double sweep = kPi * 1.5;
  const double cx = w.cx;
  const double cy = w.cy;
  const double r = w.r;

  for (int i = 0; i <= 4; ++i) {
    const double a = start + sweep * (i / 4.0);
    setColor(cr, (i == 0 || i == 4) ? kMuted : kPanelEdge);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, cx + std::cos(a) * (r + 3.0),
                  cy + std::sin(a) * (r + 3.0));
    cairo_line_to(cr, cx + std::cos(a) * (r + 7.0),
                  cy + std::sin(a) * (r + 7.0));
    cairo_stroke(cr);
  }

  drawKnobBody(cr, cx, cy, r, hovered);

  // track and value arcs
  cairo_set_line_width(cr, 2.6);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  setColorA(cr, kBg, 0.9);
  cairo_arc(cr, cx, cy, r - 5.0, start, start + sweep);
  cairo_stroke(cr);

  const double pos = start + sweep * n;
  setColorA(cr, enabled ? kAmber : kMuted, 0.38);
  cairo_set_line_width(cr, 1.8);
  if (w.bipolar) {
    const double mid = start + sweep * 0.5;
    cairo_arc(cr, cx, cy, r - 5.0, std::min(mid, pos), std::max(mid, pos));
  } else {
    cairo_arc(cr, cx, cy, r - 5.0, start, pos);
  }
  cairo_stroke(cr);

  const double a = pos;
  setColor(cr, kCream);
  cairo_set_line_width(cr, 2.3);
  cairo_move_to(cr, cx + std::cos(a) * (r * 0.22),
                cy + std::sin(a) * (r * 0.22));
  cairo_line_to(cr, cx + std::cos(a) * (r * 0.84),
                cy + std::sin(a) * (r * 0.84));
  cairo_stroke(cr);

  if (!gKnobImage) {
    cairo_arc(cr, cx, cy, r * 0.20, 0.0, kPi * 2.0);
    setColor(cr, kSilver);
    cairo_fill(cr);
    cairo_arc(cr, cx, cy, r * 0.20, 0.0, kPi * 2.0);
    setColorA(cr, kBg, 0.7);
    cairo_set_line_width(cr, 0.8);
    cairo_stroke(cr);
  }

  textCentered(cr, cx, cy + r + 19.0, shortLabel(w.port), 8.0, true, kMuted);
  const std::string valueText = formatValue(w.port, value);
  textCentered(cr, cx, cy + r + 32.0, valueText.c_str(), 8.5, false, kText);
}

static void drawSwitch(cairo_t* cr, const Widget& w, float value, bool hovered,
                       bool enabled) {
  (void)enabled;
  const ParamInfo& p = infoFor(w.port);
  const int count = (p.points && p.pointCount > 0) ? p.pointCount : 1;
  const int selected = switchIndex(p, value);
  const double start = kPi * 0.75;
  const double sweep = kPi * 1.5;
  const double cx = w.cx;
  const double cy = w.cy;
  const double r = w.r;

  textCentered(cr, cx, cy - r - 13.0, switchCaption(w.port), 7.5, true, kMuted);

  // ticks only; the numbers live in a compact strip below the knob
  for (int i = 0; i < count; ++i) {
    const double t = (count > 1) ? (i / static_cast<double>(count - 1)) : 0.5;
    const double a = start + sweep * t;
    setColor(cr, i == selected ? kAmber : kPanelEdge);
    cairo_set_line_width(cr, i == selected ? 1.8 : 1.0);
    cairo_move_to(cr, cx + std::cos(a) * (r + 3.0),
                  cy + std::sin(a) * (r + 3.0));
    cairo_line_to(cr, cx + std::cos(a) * (r + 7.0),
                  cy + std::sin(a) * (r + 7.0));
    cairo_stroke(cr);
  }

  drawKnobBody(cr, cx, cy, r, hovered);

  const double t = (count > 1) ? (selected / static_cast<double>(count - 1)) : 0.5;
  const double a = start + sweep * t;
  setColor(cr, kCream);
  cairo_set_line_width(cr, 2.4);
  cairo_move_to(cr, cx + std::cos(a) * (r * 0.22),
                cy + std::sin(a) * (r * 0.22));
  cairo_line_to(cr, cx + std::cos(a) * (r * 0.84),
                cy + std::sin(a) * (r * 0.84));
  cairo_stroke(cr);

  if (!gKnobImage) {
    cairo_arc(cr, cx, cy, r * 0.18, 0.0, kPi * 2.0);
    setColor(cr, kSilver);
    cairo_fill(cr);
  }

  // compact horizontal legend under the switch
  const double fontSize = 7.0;
  double total = 0.0;
  for (int i = 0; i < count; ++i) {
    cairo_text_extents_t ext;
    cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
                           i == selected ? CAIRO_FONT_WEIGHT_BOLD
                                        : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, fontSize);
    cairo_text_extents(cr, p.points[i].label, &ext);
    total += ext.width;
    if (i + 1 < count) total += 4.0;
  }
  double lx = cx - total * 0.5;
  const double ly = cy + r + 14.0;
  for (int i = 0; i < count; ++i) {
    cairo_text_extents_t ext;
    cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
                           i == selected ? CAIRO_FONT_WEIGHT_BOLD
                                        : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, fontSize);
    cairo_text_extents(cr, p.points[i].label, &ext);
    setColor(cr, i == selected ? kAmber : kMuted);
    cairo_move_to(cr, lx, ly);
    cairo_show_text(cr, p.points[i].label);
    lx += ext.width + 4.0;
  }
}

static void drawPowerButton(cairo_t* cr, const Widget& w, bool on,
                            const char* onLabel, const char* offLabel) {
  const double r = 6.0;
  roundedRect(cr, w.x + 1.0, w.y + 3.0, w.w, w.h, r);
  setColorA(cr, kBg, 0.45);
  cairo_fill(cr);

  roundedRect(cr, w.x, w.y, w.w, w.h, r);
  cairo_pattern_t* pat = cairo_pattern_create_linear(w.x, w.y, w.x,
                                                     w.y + w.h);
  if (on) {
    cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.86, 0.35, 0.30);
    cairo_pattern_add_color_stop_rgb(pat, 0.55, 0.62, 0.18, 0.15);
    cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.34, 0.08, 0.07);
  } else {
    cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.21, 0.18, 0.18);
    cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.06, 0.05, 0.05);
  }
  cairo_set_source(cr, pat);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(pat);
  setColorA(cr, kBg, 0.9);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  cairo_save(cr);
  roundedRect(cr, w.x, w.y, w.w, w.h, r);
  cairo_clip(cr);
  cairo_pattern_t* gloss = cairo_pattern_create_linear(
      w.x, w.y, w.x, w.y + w.h * 0.62);
  cairo_pattern_add_color_stop_rgba(gloss, 0.0, 1.0, 1.0, 1.0, 0.22);
  cairo_pattern_add_color_stop_rgba(gloss, 1.0, 1.0, 1.0, 1.0, 0.0);
  cairo_rectangle(cr, w.x, w.y, w.w, w.h * 0.62);
  cairo_set_source(cr, gloss);
  cairo_fill(cr);
  cairo_pattern_destroy(gloss);
  cairo_restore(cr);

  setColorA(cr, Color{1.0, 1.0, 1.0, 1.0}, 0.18);
  cairo_set_line_width(cr, 0.8);
  cairo_move_to(cr, w.x + 9.0, w.y + 1.0);
  cairo_line_to(cr, w.x + w.w - 9.0, w.y + 1.0);
  cairo_stroke(cr);

  const double ledX = w.x + 16.0;
  const double ledY = w.y + w.h * 0.5;
  if (on) {
    cairo_pattern_t* glow = cairo_pattern_create_radial(
        ledX, ledY, 1.0, ledX, ledY, 10.0);
    cairo_pattern_add_color_stop_rgba(glow, 0.0, 1.0, 0.35, 0.20, 0.45);
    cairo_pattern_add_color_stop_rgba(glow, 1.0, 1.0, 0.20, 0.10, 0.0);
    cairo_arc(cr, ledX, ledY, 10.0, 0.0, kPi * 2.0);
    cairo_set_source(cr, glow);
    cairo_fill(cr);
    cairo_pattern_destroy(glow);
  }
  cairo_arc(cr, ledX, ledY, 3.4, 0.0, kPi * 2.0);
  setColor(cr, on ? Color{1.0, 0.60, 0.45, 1.0}
                  : Color{0.25, 0.08, 0.06, 1.0});
  cairo_fill(cr);
  cairo_arc(cr, ledX - 1.0, ledY - 1.0, 1.1, 0.0, kPi * 2.0);
  setColorA(cr, Color{1.0, 1.0, 1.0, 1.0}, on ? 0.7 : 0.15);
  cairo_fill(cr);

  textCentered(cr, w.x + w.w * 0.5 + 7.0, w.y + w.h * 0.5 + 3.5,
               on ? onLabel : offLabel, 9.0, true, on ? kCream : kMuted);
}

static void drawPillToggle(cairo_t* cr, const Widget& w, bool on,
                           const char* label) {
  const double r = w.h * 0.5;
  roundedRect(cr, w.x + 1.0, w.y + 2.0, w.w, w.h, r);
  setColorA(cr, kBg, 0.45);
  cairo_fill(cr);

  roundedRect(cr, w.x, w.y, w.w, w.h, r);
  setColor(cr, on ? kAmber : kTrack);
  cairo_fill_preserve(cr);
  setColor(cr, on ? kAmber : kPanelEdge);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  cairo_save(cr);
  roundedRect(cr, w.x, w.y, w.w, w.h, r);
  cairo_clip(cr);
  cairo_pattern_t* gloss = cairo_pattern_create_linear(
      w.x, w.y, w.x, w.y + w.h * 0.7);
  cairo_pattern_add_color_stop_rgba(gloss, 0.0, 1.0, 1.0, 1.0, 0.20);
  cairo_pattern_add_color_stop_rgba(gloss, 1.0, 1.0, 1.0, 1.0, 0.0);
  cairo_rectangle(cr, w.x, w.y, w.w, w.h * 0.7);
  cairo_set_source(cr, gloss);
  cairo_fill(cr);
  cairo_pattern_destroy(gloss);
  cairo_restore(cr);

  const double knobX = on ? (w.x + w.w - r) : (w.x + r);
  cairo_arc(cr, knobX + 1.0, w.y + r + 2.0, std::max(2.0, r - 4.0), 0.0,
            kPi * 2.0);
  setColorA(cr, kBg, 0.5);
  cairo_fill(cr);
  cairo_arc(cr, knobX, w.y + r, std::max(2.0, r - 4.0), 0.0, kPi * 2.0);
  setColor(cr, on ? kBg : kMuted);
  cairo_fill(cr);
  cairo_arc(cr, knobX - 1.0, w.y + r - 1.0, std::max(1.0, r - 6.0), 0.0,
            kPi * 2.0);
  setColorA(cr, Color{1.0, 1.0, 1.0, 1.0}, 0.15);
  cairo_fill(cr);

  textCentered(cr, w.x + w.w * 0.5 + (on ? -r * 0.45 : r * 0.45),
               w.y + w.h * 0.5 + 3.0, label, 8.0, true,
               on ? kBg : kText);
}

static void drawFader(cairo_t* cr, const Widget& w, float value, bool hovered,
                      bool enabled) {
  const ParamInfo& p = infoFor(w.port);
  const float n = normOf(p, value);
  const double cx = w.x + w.w * 0.5;
  const double top = w.y;
  const double bottom = w.y + w.h;
  const double capY = bottom - n * w.h;

  // dB scale (range is -60 .. +12 dB)
  const double dbMarks[] = {12.0, 0.0, -12.0, -24.0, -36.0, -48.0, -60.0};
  for (double db : dbMarks) {
    const double t = (db + 60.0) / 72.0;
    const double y = bottom - t * w.h;
    setColorA(cr, kPanelEdge, 0.85);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, cx - 27.0, y);
    cairo_line_to(cr, cx - 21.0, y);
    cairo_stroke(cr);

    char label[16];
    if (db > 0.0) {
      std::snprintf(label, sizeof(label), "+%.0f", db);
    } else {
      std::snprintf(label, sizeof(label), "%.0f", db);
    }
    cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 6.8);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, label, &ext);
    setColor(cr, kMuted);
    cairo_move_to(cr, cx - 30.0 - ext.width, y + 2.2);
    cairo_show_text(cr, label);
  }

  // recessed slot with a metal edge
  roundedRect(cr, cx - 10.0, top - 2.0, 20.0, w.h + 4.0, 6.0);
  setColorA(cr, kBg, 0.55);
  cairo_fill(cr);
  roundedRect(cr, cx - 6.0, top, 12.0, w.h, 5.0);
  cairo_pattern_t* slot = cairo_pattern_create_linear(cx - 6.0, top,
                                                      cx + 6.0, top);
  cairo_pattern_add_color_stop_rgb(slot, 0.0, 0.020, 0.025, 0.030);
  cairo_pattern_add_color_stop_rgb(slot, 0.5, 0.120, 0.145, 0.170);
  cairo_pattern_add_color_stop_rgb(slot, 1.0, 0.020, 0.025, 0.030);
  cairo_set_source(cr, slot);
  cairo_fill(cr);
  cairo_pattern_destroy(slot);
  setColorA(cr, kBg, 0.9);
  cairo_set_line_width(cr, 0.8);
  roundedRect(cr, cx - 6.0, top, 12.0, w.h, 5.0);
  cairo_stroke(cr);

  // filled part
  cairo_rectangle(cr, cx - 1.5, capY, 3.0, bottom - capY);
  setColor(cr, enabled ? kAmber : kMuted);
  cairo_fill(cr);

  // cap shadow, then a brushed-metal cap with a glass highlight
  roundedRect(cr, cx - 26.0, capY - 8.0, 54.0, 20.0, 3.0);
  setColorA(cr, kBg, 0.45);
  cairo_fill(cr);

  roundedRect(cr, cx - 27.0, capY - 10.0, 54.0, 20.0, 3.0);
  cairo_pattern_t* pat = cairo_pattern_create_linear(cx - 27.0, capY,
                                                     cx + 27.0, capY);
  cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.38, 0.41, 0.43);
  cairo_pattern_add_color_stop_rgb(pat, 0.42, 0.84, 0.86, 0.86);
  cairo_pattern_add_color_stop_rgb(pat, 0.58, 0.72, 0.74, 0.75);
  cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.31, 0.34, 0.36);
  cairo_set_source(cr, pat);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(pat);
  setColor(cr, hovered ? kAmber : kBg);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  cairo_save(cr);
  roundedRect(cr, cx - 27.0, capY - 10.0, 54.0, 20.0, 3.0);
  cairo_clip(cr);
  cairo_pattern_t* gloss = cairo_pattern_create_linear(
      cx, capY - 10.0, cx, capY + 2.0);
  cairo_pattern_add_color_stop_rgba(gloss, 0.0, 1.0, 1.0, 1.0, 0.35);
  cairo_pattern_add_color_stop_rgba(gloss, 1.0, 1.0, 1.0, 1.0, 0.0);
  cairo_rectangle(cr, cx - 27.0, capY - 10.0, 54.0, 12.0);
  cairo_set_source(cr, gloss);
  cairo_fill(cr);
  cairo_pattern_destroy(gloss);
  cairo_restore(cr);

  setColorA(cr, kBg, 0.95);
  cairo_rectangle(cr, cx - 18.0, capY - 1.5, 36.0, 3.0);
  cairo_fill(cr);

  const std::string valueText = formatValue(w.port, value);
  textCentered(cr, cx, bottom + 26.0, valueText.c_str(), 9.0, false, kText);
  textCentered(cr, cx, bottom + 42.0, "OUTPUT", 8.0, true, kMuted);
}

static void drawVu(cairo_t* cr, const Widget& w, float levelDb) {
  const double x = w.x;
  const double y = w.y;
  const double width = w.w;
  const double height = w.h;
  const double cx = x + width * 0.5;
  const double pivotY = y + height - 14.0;
  const double radius = std::min(width * 0.42, height * 0.74);

  // metal bezel
  roundedRect(cr, x - 4.0, y - 4.0, width + 8.0, height + 8.0, 7.0);
  cairo_pattern_t* bezel = cairo_pattern_create_linear(
      x - 4.0, y - 4.0, x - 4.0, y + height + 4.0);
  cairo_pattern_add_color_stop_rgb(bezel, 0.0, 0.68, 0.71, 0.73);
  cairo_pattern_add_color_stop_rgb(bezel, 0.45, 0.30, 0.34, 0.38);
  cairo_pattern_add_color_stop_rgb(bezel, 1.0, 0.09, 0.11, 0.13);
  cairo_set_source(cr, bezel);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(bezel);
  setColorA(cr, kBg, 0.85);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  roundedRect(cr, x, y, width, height, 4.0);
  setColor(cr, kMeterFace);
  cairo_fill(cr);
  roundedRect(cr, x, y, width, height, 4.0);
  setColorA(cr, kMeterInk, 0.7);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  const double start = kPi * 1.14;
  const double end = kPi * 1.86;
  const double sweep = end - start;
  cairo_set_line_width(cr, 1.0);
  setColor(cr, kMeterInk);
  cairo_arc(cr, cx, pivotY, radius, start, end);
  cairo_stroke(cr);

  const float marks[] = {-20.0f, -10.0f, -5.0f, 0.0f, 3.0f};
  const char* labels[] = {"-20", "-10", "-5", "0", "+3"};
  for (int i = 0; i < 5; ++i) {
    const float norm = (marks[i] + 20.0f) / 23.0f;
    const double a = start + sweep * norm;
    const double x0 = cx + std::cos(a) * (radius - 2.0);
    const double y0 = pivotY + std::sin(a) * (radius - 2.0);
    const double x1 = cx + std::cos(a) * (radius + 4.0);
    const double y1 = pivotY + std::sin(a) * (radius + 4.0);
    setColor(cr, kMeterInk);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x1, y1);
    cairo_stroke(cr);

    const double lx = cx + std::cos(a) * (radius - 12.0);
    const double ly = pivotY + std::sin(a) * (radius - 12.0) + 2.5;
    textCentered(cr, lx, ly, labels[i], 7.0, false, kMeterInk);
  }

  const float norm = std::min(std::max((levelDb + 20.0f) / 23.0f, 0.0f), 1.0f);
  const double a = start + sweep * norm;
  setColor(cr, kRed);
  cairo_set_line_width(cr, 1.5);
  cairo_move_to(cr, cx, pivotY);
  cairo_line_to(cr, cx + std::cos(a) * (radius - 6.0),
                pivotY + std::sin(a) * (radius - 6.0));
  cairo_stroke(cr);
  cairo_arc(cr, cx, pivotY, 3.0, 0.0, kPi * 2.0);
  setColor(cr, kMeterInk);
  cairo_fill(cr);

  // glass reflection over the meter face
  cairo_save(cr);
  roundedRect(cr, x, y, width, height, 4.0);
  cairo_clip(cr);
  cairo_pattern_t* glass = cairo_pattern_create_linear(
      x, y, x, y + height * 0.72);
  cairo_pattern_add_color_stop_rgba(glass, 0.0, 1.0, 1.0, 1.0, 0.20);
  cairo_pattern_add_color_stop_rgba(glass, 1.0, 1.0, 1.0, 1.0, 0.0);
  cairo_rectangle(cr, x, y, width, height * 0.72);
  cairo_set_source(cr, glass);
  cairo_fill(cr);
  cairo_pattern_destroy(glass);

  cairo_move_to(cr, x - 10.0, y + height * 0.18);
  cairo_line_to(cr, x + width * 0.48, y - 20.0);
  cairo_line_to(cr, x + width * 0.70, y - 20.0);
  cairo_line_to(cr, x + 2.0, y + height * 0.40);
  cairo_close_path(cr);
  setColorA(cr, Color{1.0, 1.0, 1.0, 1.0}, 0.06);
  cairo_fill(cr);
  cairo_restore(cr);

  setColorA(cr, Color{1.0, 1.0, 1.0, 1.0}, 0.16);
  roundedRect(cr, x, y, width, height, 4.0);
  cairo_set_line_width(cr, 0.8);
  cairo_stroke(cr);

  textAt(cr, x + 8.0, y + 14.0, "VU", 8.0, true, kMeterInk);
  textAt(cr, x + width - 28.0, y + height - 5.0, "dBFS", 5.8, false,
         kMeterInk);

  const bool peak = levelDb > -1.0f;
  if (peak) {
    cairo_pattern_t* glow = cairo_pattern_create_radial(
        x + width - 10.0, y + 10.0, 1.0, x + width - 10.0, y + 10.0, 11.0);
    cairo_pattern_add_color_stop_rgba(glow, 0.0, 1.0, 0.20, 0.12, 0.5);
    cairo_pattern_add_color_stop_rgba(glow, 1.0, 1.0, 0.10, 0.05, 0.0);
    cairo_arc(cr, x + width - 10.0, y + 10.0, 11.0, 0.0, kPi * 2.0);
    cairo_set_source(cr, glow);
    cairo_fill(cr);
    cairo_pattern_destroy(glow);
  }
  cairo_arc(cr, x + width - 10.0, y + 10.0, 3.2, 0.0, kPi * 2.0);
  setColor(cr, peak ? Color{1.0, 0.25, 0.18, 1.0}
                    : Color{0.25, 0.18, 0.15, 1.0});
  cairo_fill(cr);
}

static void drawGr(cairo_t* cr, const Widget& w, float grDb) {
  roundedRect(cr, w.x - 1.5, w.y - 1.5, w.w + 3.0, w.h + 3.0, 3.5);
  setColorA(cr, kBg, 0.55);
  cairo_fill(cr);

  roundedRect(cr, w.x, w.y, w.w, w.h, 2.5);
  cairo_pattern_t* bg = cairo_pattern_create_linear(
      w.x, w.y, w.x + w.w, w.y);
  cairo_pattern_add_color_stop_rgb(bg, 0.0, 0.055, 0.065, 0.075);
  cairo_pattern_add_color_stop_rgb(bg, 0.5, 0.015, 0.020, 0.025);
  cairo_pattern_add_color_stop_rgb(bg, 1.0, 0.045, 0.055, 0.065);
  cairo_set_source(cr, bg);
  cairo_fill(cr);
  cairo_pattern_destroy(bg);
  setColorA(cr, kPanelEdge, 0.8);
  cairo_set_line_width(cr, 0.8);
  roundedRect(cr, w.x, w.y, w.w, w.h, 2.5);
  cairo_stroke(cr);

  const float n = std::min(std::max(-grDb / 30.0f, 0.0f), 1.0f);
  if (n > 0.0f) {
    cairo_pattern_t* red = cairo_pattern_create_linear(
        w.x, w.y, w.x + w.w, w.y);
    cairo_pattern_add_color_stop_rgb(red, 0.0, 0.38, 0.045, 0.035);
    cairo_pattern_add_color_stop_rgb(red, 0.5, 0.82, 0.16, 0.10);
    cairo_pattern_add_color_stop_rgb(red, 1.0, 0.45, 0.055, 0.040);
    cairo_rectangle(cr, w.x + 1.0, w.y + 1.0, w.w - 2.0,
                    (w.h - 2.0) * n);
    cairo_set_source(cr, red);
    cairo_fill(cr);
    cairo_pattern_destroy(red);
  }

  setColorA(cr, Color{1.0, 1.0, 1.0, 1.0}, 0.16);
  cairo_rectangle(cr, w.x + 1.0, w.y + 1.0, 2.0, w.h - 2.0);
  cairo_fill(cr);

  for (int i = 1; i < 4; ++i) {
    const double y = w.y + w.h * (i / 4.0);
    setColorA(cr, kBg, 0.55);
    cairo_set_line_width(cr, 0.8);
    cairo_move_to(cr, w.x, y);
    cairo_line_to(cr, w.x + w.w, y);
    cairo_stroke(cr);
  }

  textCentered(cr, w.x + w.w * 0.5, w.y + w.h + 18.0, "GR", 7.5, true, kMuted);
}

static void drawPresetArrow(cairo_t* cr, const Rect& r, bool left,
                            bool hovered) {
  roundedRect(cr, r.x, r.y, r.w, r.h, 3.0);
  cairo_pattern_t* pat = cairo_pattern_create_linear(r.x, r.y, r.x,
                                                     r.y + r.h);
  cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.34, 0.40, 0.46);
  cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.13, 0.17, 0.21);
  cairo_set_source(cr, pat);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(pat);
  setColor(cr, hovered ? kAmber : kPanelEdge);
  cairo_set_line_width(cr, hovered ? 1.4 : 0.9);
  cairo_stroke(cr);

  const double cx = r.x + r.w * 0.5;
  const double cy = r.y + r.h * 0.5;
  const double d = 5.0;
  cairo_move_to(cr, cx + (left ? d : -d), cy - d);
  cairo_line_to(cr, cx + (left ? -d : d), cy);
  cairo_line_to(cr, cx + (left ? d : -d), cy + d);
  cairo_close_path(cr);
  setColor(cr, hovered ? kCream : kText);
  cairo_fill(cr);
}

static void drawPresetBar(cairo_t* cr, const State& s) {
  textAt(cr, kPresetLcd.x, 18.0, "PRESET", 5.8, true, kMuted);

  // recessed amber LCD
  roundedRect(cr, kPresetLcd.x - 2.0, kPresetLcd.y - 2.0,
              kPresetLcd.w + 4.0, kPresetLcd.h + 4.0, 4.0);
  setColor(cr, kPanelEdge);
  cairo_fill(cr);
  roundedRect(cr, kPresetLcd.x, kPresetLcd.y, kPresetLcd.w, kPresetLcd.h,
              2.5);
  cairo_pattern_t* lcd = cairo_pattern_create_linear(
      kPresetLcd.x, kPresetLcd.y, kPresetLcd.x, kPresetLcd.y + kPresetLcd.h);
  cairo_pattern_add_color_stop_rgb(lcd, 0.0, 0.075, 0.055, 0.025);
  cairo_pattern_add_color_stop_rgb(lcd, 1.0, 0.025, 0.020, 0.012);
  cairo_set_source(cr, lcd);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(lcd);
  setColor(cr, s.presetHover == 3 ? kAmber : kBg);
  cairo_set_line_width(cr, s.presetHover == 3 ? 1.2 : 1.0);
  cairo_stroke(cr);

  cairo_save(cr);
  roundedRect(cr, kPresetLcd.x, kPresetLcd.y, kPresetLcd.w, kPresetLcd.h,
              2.5);
  cairo_clip(cr);

  for (double y = kPresetLcd.y + 2.0; y < kPresetLcd.y + kPresetLcd.h;
       y += 2.0) {
    setColorA(cr, kBg, 0.18);
    cairo_rectangle(cr, kPresetLcd.x, y, kPresetLcd.w, 1.0);
    cairo_fill(cr);
  }

  cairo_pattern_t* glass = cairo_pattern_create_linear(
      kPresetLcd.x, kPresetLcd.y, kPresetLcd.x,
      kPresetLcd.y + kPresetLcd.h * 0.65);
  cairo_pattern_add_color_stop_rgba(glass, 0.0, 1.0, 1.0, 1.0, 0.10);
  cairo_pattern_add_color_stop_rgba(glass, 1.0, 1.0, 1.0, 1.0, 0.0);
  cairo_set_source(cr, glass);
  cairo_rectangle(cr, kPresetLcd.x, kPresetLcd.y, kPresetLcd.w,
                  kPresetLcd.h * 0.65);
  cairo_fill(cr);
  cairo_pattern_destroy(glass);
  cairo_restore(cr);

  const char* name =
      (s.presetIndex >= 0) ? kPresets[s.presetIndex].shortName : "INIT";
  char display[32];
  std::snprintf(display, sizeof(display), "%s%s", name,
                s.presetDirty ? "*" : "");
  textAt(cr, kPresetLcd.x + 14.0, kPresetLcd.y + 23.0, display, 10.5, true,
         kAmber, "DejaVu Sans Mono");

  char index[16];
  if (s.presetIndex >= 0) {
    std::snprintf(index, sizeof(index), "%d/%d", s.presetIndex + 1,
                  kPresetCount);
  } else {
    std::snprintf(index, sizeof(index), "--/%d", kPresetCount);
  }
  textAt(cr, kPresetLcd.x + kPresetLcd.w - 38.0,
         kPresetLcd.y + kPresetLcd.h - 5.0, index, 5.5, false, kMuted,
         "DejaVu Sans Mono");

  cairo_arc(cr, kPresetLcd.x + 7.0, kPresetLcd.y + 18.0, 2.2, 0.0,
            kPi * 2.0);
  setColor(cr, s.presetDirty ? kRed : kGreen);
  cairo_fill(cr);

  drawPresetArrow(cr, kPresetPrev, true, s.presetHover == 1);
  drawPresetArrow(cr, kPresetNext, false, s.presetHover == 2);
}

static void drawHeader(cairo_t* cr, const State& s) {
  cairo_pattern_t* pat = cairo_pattern_create_linear(0, 0, 0, 80);
  cairo_pattern_add_color_stop_rgb(pat, 0.0, kHeaderTop.r, kHeaderTop.g,
                                   kHeaderTop.b);
  cairo_pattern_add_color_stop_rgb(pat, 1.0, kHeaderBottom.r, kHeaderBottom.g,
                                   kHeaderBottom.b);
  cairo_rectangle(cr, 0, 0, UI_W, 80);
  cairo_set_source(cr, pat);
  cairo_fill(cr);
  cairo_pattern_destroy(pat);

  paintTexture(cr, 0.0, 0.0, UI_W, 80.0, 0.06);

  cairo_pattern_t* glass = cairo_pattern_create_linear(0, 0, 0, 26);
  cairo_pattern_add_color_stop_rgba(glass, 0.0, 1.0, 1.0, 1.0, 0.09);
  cairo_pattern_add_color_stop_rgba(glass, 1.0, 1.0, 1.0, 1.0, 0.0);
  cairo_rectangle(cr, 0, 0, UI_W, 26);
  cairo_set_source(cr, glass);
  cairo_fill(cr);
  cairo_pattern_destroy(glass);

  drawWordmark(cr, 22.0, 56.0, 38.0);
  textAt(cr, 24.0, 71.0, "CLASS-A CHANNEL STRIP", 8.5, true, kMuted);
  drawPresetBar(cr, s);
  textAt(cr, 430.0, 42.0, "CLASS A", 6.5, true, kMuted);
  textAt(cr, 430.0, 52.0, "CONSOLE", 6.5, false, kMuted);
  textAt(cr, 430.0, 62.0, "v0.2.2", 5.5, false, kMuted);

  // brushed-metal strip along the bottom of the header
  cairo_pattern_t* strip = cairo_pattern_create_linear(0, 76, 0, 80);
  cairo_pattern_add_color_stop_rgb(strip, 0.0, 0.34, 0.40, 0.46);
  cairo_pattern_add_color_stop_rgb(strip, 1.0, 0.06, 0.08, 0.10);
  cairo_rectangle(cr, 0, 76, UI_W, 4);
  cairo_set_source(cr, strip);
  cairo_fill(cr);
  cairo_pattern_destroy(strip);

  setColorA(cr, Color{1.0, 1.0, 1.0, 1.0}, 0.16);
  cairo_set_line_width(cr, 0.8);
  cairo_move_to(cr, 14.0, 76.0);
  cairo_line_to(cr, UI_W - 14.0, 76.0);
  cairo_stroke(cr);
  setColorA(cr, kBg, 0.65);
  cairo_move_to(cr, 14.0, 79.5);
  cairo_line_to(cr, UI_W - 14.0, 79.5);
  cairo_stroke(cr);

  setColor(cr, kRed);
  cairo_rectangle(cr, 14.0, 80.0, 24.0, 2.0);
  cairo_fill(cr);
  (void)s;
}

// ---------------------------------------------------------------------------
// Full render
// ---------------------------------------------------------------------------
static void drawChassis(cairo_t* cr) {
  cairo_save(cr);
  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
  roundedRect(cr, 4.0, 4.0, UI_W - 8.0, UI_H - 8.0, 14.0);
  roundedRect(cr, 12.0, 12.0, UI_W - 24.0, UI_H - 24.0, 10.0);
  cairo_pattern_t* metal = cairo_pattern_create_linear(0, 0, UI_W, UI_H);
  cairo_pattern_add_color_stop_rgb(metal, 0.0, 0.30, 0.35, 0.40);
  cairo_pattern_add_color_stop_rgb(metal, 0.45, 0.09, 0.12, 0.15);
  cairo_pattern_add_color_stop_rgb(metal, 0.55, 0.16, 0.20, 0.24);
  cairo_pattern_add_color_stop_rgb(metal, 1.0, 0.26, 0.31, 0.36);
  cairo_set_source(cr, metal);
  cairo_fill(cr);
  cairo_pattern_destroy(metal);
  cairo_restore(cr);

  roundedRect(cr, 4.0, 4.0, UI_W - 8.0, UI_H - 8.0, 14.0);
  setColorA(cr, kBg, 0.9);
  cairo_set_line_width(cr, 1.2);
  cairo_stroke(cr);
  roundedRect(cr, 12.0, 12.0, UI_W - 24.0, UI_H - 24.0, 10.0);
  setColorA(cr, kBg, 0.75);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  setColorA(cr, Color{1.0, 1.0, 1.0, 1.0}, 0.18);
  cairo_set_line_width(cr, 0.8);
  cairo_move_to(cr, 18.0, 5.5);
  cairo_line_to(cr, UI_W - 18.0, 5.5);
  cairo_stroke(cr);
  setColorA(cr, kBg, 0.75);
  cairo_move_to(cr, 18.0, UI_H - 5.5);
  cairo_line_to(cr, UI_W - 18.0, UI_H - 5.5);
  cairo_stroke(cr);

  drawScrew(cr, 16.0, 16.0);
  drawScrew(cr, UI_W - 16.0, 16.0);
  drawScrew(cr, 16.0, UI_H - 16.0);
  drawScrew(cr, UI_W - 16.0, UI_H - 16.0);
}

static void render(const State& s, cairo_t* cr) {
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

  cairo_pattern_t* bg = cairo_pattern_create_linear(0, 0, 0, UI_H);
  cairo_pattern_add_color_stop_rgb(bg, 0.0, 0.095, 0.115, 0.135);
  cairo_pattern_add_color_stop_rgb(bg, 1.0, 0.022, 0.028, 0.035);
  cairo_rectangle(cr, 0, 0, UI_W, UI_H);
  cairo_set_source(cr, bg);
  cairo_fill(cr);
  cairo_pattern_destroy(bg);

  cairo_pattern_t* vignette = cairo_pattern_create_radial(
      UI_W * 0.5, UI_H * 0.45, UI_H * 0.10, UI_W * 0.5, UI_H * 0.5,
      UI_H * 0.82);
  cairo_pattern_add_color_stop_rgba(vignette, 0.0, 0.0, 0.0, 0.0, 0.0);
  cairo_pattern_add_color_stop_rgba(vignette, 1.0, 0.0, 0.0, 0.0, 0.42);
  cairo_rectangle(cr, 0, 0, UI_W, UI_H);
  cairo_set_source(cr, vignette);
  cairo_fill(cr);
  cairo_pattern_destroy(vignette);

  paintTexture(cr, 0.0, 0.0, UI_W, UI_H, 0.035);

  drawHeader(cr, s);

  drawPanel(cr, 14.0, 84.0, 442.0, 162.0, "PREAMP");
  drawPanel(cr, 14.0, 254.0, 442.0, 360.0, "EQUALISER");
  drawPanel(cr, 14.0, 622.0, 442.0, 270.0, "COMPRESSOR");
  drawPanel(cr, 464.0, 84.0, 142.0, 808.0, "OUTPUT");

  textAt(cr, 408.0, 154.0, "CLASS A", 7.0, true, kAmber);
  textAt(cr, 408.0, 164.0, "TRANSFORMER", 6.3, false, kMuted);
  textAt(cr, 408.0, 174.0, "INPUT", 6.3, false, kMuted);

  textAt(cr, 30.0, 664.0, "DIODE-BRIDGE DETECTOR  /  STEREO LINKED", 6.5, false,
         kMuted);

  const bool enabled = s.values[PORT_ENABLE] >= 0.5f;

  for (int i = 0; i < kNumWidgets; ++i) {
    const Widget& w = kWidgets[i];
    const bool hovered = (s.hover == i);

    switch (w.kind) {
      case Kind::Knob:
        drawKnob(cr, w, s.values[w.port], hovered, enabled);
        break;
      case Kind::Switch:
        drawSwitch(cr, w, s.values[w.port], hovered, enabled);
        break;
      case Kind::Toggle:
        if (w.port == PORT_ENABLE) {
          drawPowerButton(cr, w, s.values[PORT_ENABLE] >= 0.5f, "IN", "BYPASS");
        } else if (w.port == PORT_COMP_ON) {
          const bool on = s.values[PORT_COMP_ON] >= 0.5f;
          drawPowerButton(cr, w, on, "COMP", "COMP");
        } else {
          const bool fb = s.values[PORT_COMP_MODE] >= 0.5f;
          drawPillToggle(cr, w, fb, fb ? "FEEDBACK" : "FEED-FWD");
        }
        break;
      case Kind::Fader:
        drawFader(cr, w, s.values[w.port], hovered, enabled);
        break;
      case Kind::Vu:
        drawVu(cr, w, s.vuSmooth);
        break;
      case Kind::Meter:
        drawGr(cr, w, s.meterGrSmooth);
        break;
    }
  }

  drawChassis(cr);
}

// ---------------------------------------------------------------------------
// Hit testing and interaction
// ---------------------------------------------------------------------------
static int hitTest(double x, double y) {
  for (int i = kNumWidgets - 1; i >= 0; --i) {
    const Widget& w = kWidgets[i];
    if (w.kind == Kind::Knob || w.kind == Kind::Switch) {
      const double dx = x - w.cx;
      const double dy = y - w.cy;
      const double rr = (w.kind == Kind::Switch ? w.r + 9.0 : w.r + 6.0);
      if (dx * dx + dy * dy <= rr * rr) {
        return i;
      }
    } else {
      if (x >= w.x && x <= w.x + w.w && y >= w.y && y <= w.y + w.h) {
        return i;
      }
    }
  }
  return -1;
}

struct Host {
  LV2UI_Write_Function write = nullptr;
  LV2UI_Controller controller = nullptr;
};

static void sendValue(const Host& host, uint32_t port, float value) {
  if (host.write) {
    host.write(host.controller, port, sizeof(float), 0, &value);
  }
}

static void setValue(State& s, const Host& host, uint32_t port, float value) {
  const ParamInfo& p = infoFor(port);
  value = std::min(std::max(value, std::min(p.min, p.max)),
                   std::max(p.min, p.max));
  s.values[port] = value;
  if (s.presetIndex >= 0) {
    s.presetDirty = true;
  }
  s.dirty = true;
  sendValue(host, port, value);
}

// ---------------------------------------------------------------------------
// X11 UI instance
// ---------------------------------------------------------------------------
struct X11UI {
  Display* display = nullptr;
  Window parent = 0;
  Window window = 0;
  bool topLevel = false;
  Visual* visual = nullptr;
  int depth = 24;
  cairo_surface_t* windowSurface = nullptr;
  cairo_surface_t* imageSurface = nullptr;
  cairo_t* imageCr = nullptr;
  State state;
  Host host;
  int windowW = 0;
  int windowH = 0;
  double scale = 1.0;
  double offsetX = 0.0;
  double offsetY = 0.0;
  const LV2UI_Resize* hostResize = nullptr;
  bool resizeRequested = false;
  float scaleFactor = 1.0f;
};

static X11UI* get(LV2UI_Handle handle) {
  return static_cast<X11UI*>(handle);
}

static void applyPreset(X11UI* ui, int index) {
  if (!ui || index < 0 || index >= kPresetCount) {
    return;
  }
  const Preset& preset = kPresets[index];
  for (int i = 0; i < preset.count; ++i) {
    setValue(ui->state, ui->host, preset.values[i].port,
             preset.values[i].value);
  }
  ui->state.presetIndex = index;
  ui->state.presetDirty = false;
  ui->state.dirty = true;
}

static void cyclePreset(X11UI* ui, int direction) {
  if (!ui) {
    return;
  }
  int index = ui->state.presetIndex;
  if (index < 0) {
    index = (direction >= 0) ? 0 : kPresetCount - 1;
  } else {
    index = (index + direction + kPresetCount) % kPresetCount;
  }
  applyPreset(ui, index);
}

static void debugLog(const char* fmt, ...) {
  if (!std::getenv("HEARTH_UI_DEBUG")) {
    return;
  }
  std::FILE* f = std::fopen("/tmp/hearth-ui.log", "a");
  if (!f) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  std::vfprintf(f, fmt, args);
  va_end(args);
  std::fputc('\n', f);
  std::fclose(f);
}

static void updateScale(X11UI* ui) {
  if (ui->windowW <= 0 || ui->windowH <= 0) {
    return;
  }
  ui->scale = std::min(static_cast<double>(ui->windowW) / UI_W,
                       static_cast<double>(ui->windowH) / UI_H);
  ui->scale = std::max(ui->scale, 0.1);
  ui->offsetX = (ui->windowW - UI_W * ui->scale) * 0.5;
  ui->offsetY = (ui->windowH - UI_H * ui->scale) * 0.5;
}

static void ensureSurfaces(X11UI* ui) {
  if (!ui->imageSurface) {
    ui->imageSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, UI_W, UI_H);
    ui->imageCr = cairo_create(ui->imageSurface);
  }
}

static void blit(X11UI* ui) {
  ensureSurfaces(ui);
  render(ui->state, ui->imageCr);
  cairo_surface_flush(ui->imageSurface);

  if (!ui->windowSurface) {
    ui->windowSurface = cairo_xlib_surface_create(
        ui->display, ui->window, ui->visual, ui->windowW, ui->windowH);
  }
  cairo_t* cr = cairo_create(ui->windowSurface);
  cairo_set_source_rgba(cr, kBg.r, kBg.g, kBg.b, 1.0);
  cairo_paint(cr);

  cairo_save(cr);
  cairo_translate(cr, ui->offsetX, ui->offsetY);
  cairo_scale(cr, ui->scale, ui->scale);
  cairo_set_source_surface(cr, ui->imageSurface, 0.0, 0.0);
  cairo_paint(cr);
  cairo_restore(cr);

  cairo_destroy(cr);
  cairo_surface_flush(ui->windowSurface);
  XFlush(ui->display);
}

static void toLogical(const X11UI* ui, int x, int y, double& lx, double& ly) {
  lx = (x - ui->offsetX) / ui->scale;
  ly = (y - ui->offsetY) / ui->scale;
}

static void handleButtonPress(X11UI* ui, XButtonEvent& ev) {
  double x = 0.0;
  double y = 0.0;
  toLogical(ui, ev.x, ev.y, x, y);

  if (ev.button == Button1) {
    if (inRect(x, y, kPresetPrev)) {
      cyclePreset(ui, -1);
      return;
    }
    if (inRect(x, y, kPresetNext)) {
      cyclePreset(ui, 1);
      return;
    }
    if (inRect(x, y, kPresetLcd)) {
      cyclePreset(ui, 1);
      return;
    }
  }

  if (ev.button == Button4 || ev.button == Button5) {
    const int idx = hitTest(x, y);
    if (idx >= 0 && (kWidgets[idx].kind == Kind::Knob ||
                     kWidgets[idx].kind == Kind::Switch)) {
      const Widget& w = kWidgets[idx];
      const ParamInfo& p = infoFor(w.port);
      if (w.kind == Kind::Switch) {
        const int count = p.pointCount;
        int si = switchIndex(p, ui->state.values[w.port]);
        si += (ev.button == Button4) ? 1 : -1;
        si = std::min(std::max(si, 0), count - 1);
        setValue(ui->state, ui->host, w.port, p.points[si].value);
      } else {
        float n = normOf(p, ui->state.values[w.port]);
        n += (ev.button == Button4) ? 0.02f : -0.02f;
        setValue(ui->state, ui->host, w.port, valueOf(p, n));
      }
    }
    return;
  }
  if (ev.button != Button1) {
    return;
  }

  const int idx = hitTest(x, y);
  if (idx < 0) {
    return;
  }
  const Widget& w = kWidgets[idx];
  ui->state.active = idx;

  if (w.kind == Kind::Toggle) {
    const float v = ui->state.values[w.port] >= 0.5f ? 0.0f : 1.0f;
    setValue(ui->state, ui->host, w.port, v);
    ui->state.active = -1;
    return;
  }

  if (w.kind == Kind::Vu || w.kind == Kind::Meter) {
    ui->state.active = -1;
    return;
  }

  if (ui->state.lastClickWidget == idx &&
      ev.time - ui->state.lastClickTime < 400) {
    setValue(ui->state, ui->host, w.port, infoFor(w.port).def);
    ui->state.lastClickWidget = -1;
    ui->state.active = -1;
    return;
  }
  ui->state.lastClickWidget = idx;
  ui->state.lastClickTime = ev.time;

  if (w.kind == Kind::Switch) {
    const ParamInfo& p = infoFor(w.port);
    const int count = p.pointCount;
    if (count > 0) {
      const double start = kPi * 0.75;
      const double sweep = kPi * 1.5;
      const double angle = std::atan2(y - w.cy, x - w.cx);
      double rel = std::fmod(angle - start, kPi * 2.0);
      if (rel < 0.0) rel += kPi * 2.0;
      double t;
      if (rel <= sweep) {
        t = rel / sweep;
      } else {
        const double dEnd = rel - sweep;
        const double dStart = kPi * 2.0 - rel;
        t = (dStart < dEnd) ? 0.0 : 1.0;
      }
      int si = static_cast<int>(std::lround(t * (count - 1)));
      si = std::min(std::max(si, 0), count - 1);
      setValue(ui->state, ui->host, w.port, p.points[si].value);
    }
    return;
  }

  ui->state.dragY = y;
  ui->state.dragValue = ui->state.values[w.port];

  if (w.kind == Kind::Fader) {
    const float n = 1.0f - static_cast<float>((y - w.y) / w.h);
    setValue(ui->state, ui->host, w.port, valueOf(infoFor(w.port), n));
    ui->state.dragValue = ui->state.values[w.port];
  }
}

static void handleMotion(X11UI* ui, XMotionEvent& ev) {
  double x = 0.0;
  double y = 0.0;
  toLogical(ui, ev.x, ev.y, x, y);

  int presetHover = 0;
  if (inRect(x, y, kPresetPrev)) {
    presetHover = 1;
  } else if (inRect(x, y, kPresetNext)) {
    presetHover = 2;
  } else if (inRect(x, y, kPresetLcd)) {
    presetHover = 3;
  }
  if (presetHover != ui->state.presetHover) {
    ui->state.presetHover = presetHover;
    ui->state.dirty = true;
  }

  const int hover = presetHover ? -1 : hitTest(x, y);
  if (hover != ui->state.hover) {
    ui->state.hover = hover;
    ui->state.dirty = true;
  }
  if (ui->state.active < 0) {
    return;
  }
  const Widget& w = kWidgets[ui->state.active];
  if (w.kind == Kind::Fader) {
    const float n = 1.0f - static_cast<float>((y - w.y) / w.h);
    setValue(ui->state, ui->host, w.port, valueOf(infoFor(w.port), n));
    return;
  }
  if (w.kind == Kind::Switch) {
    const ParamInfo& p = infoFor(w.port);
    const int count = p.pointCount;
    if (count > 0) {
      const double start = kPi * 0.75;
      const double sweep = kPi * 1.5;
      const double angle = std::atan2(y - w.cy, x - w.cx);
      double rel = std::fmod(angle - start, kPi * 2.0);
      if (rel < 0.0) rel += kPi * 2.0;
      double t;
      if (rel <= sweep) {
        t = rel / sweep;
      } else {
        const double dEnd = rel - sweep;
        const double dStart = kPi * 2.0 - rel;
        t = (dStart < dEnd) ? 0.0 : 1.0;
      }
      int si = static_cast<int>(std::lround(t * (count - 1)));
      si = std::min(std::max(si, 0), count - 1);
      setValue(ui->state, ui->host, w.port, p.points[si].value);
    }
    return;
  }
  if (w.kind != Kind::Knob) {
    return;
  }

  const ParamInfo& p = infoFor(w.port);
  const double sensitivity = (ev.state & ShiftMask) ? 1200.0 : 180.0;
  const double dy = (ui->state.dragY - y) / sensitivity;
  const float startNorm = normOf(p, ui->state.dragValue);
  setValue(ui->state, ui->host, w.port, valueOf(p, startNorm + dy));
}

static void handleButtonRelease(X11UI* ui) {
  ui->state.active = -1;
}

static void processEvents(X11UI* ui) {
  while (XPending(ui->display)) {
    XEvent ev;
    XNextEvent(ui->display, &ev);
    switch (ev.type) {
      case Expose:
        if (ev.xexpose.count == 0) {
          ui->state.dirty = true;
        }
        break;
      case ConfigureNotify:
        if (ev.xconfigure.window == ui->window &&
            (ev.xconfigure.width != ui->windowW ||
             ev.xconfigure.height != ui->windowH)) {
          ui->windowW = std::max(ev.xconfigure.width, 160);
          ui->windowH = std::max(ev.xconfigure.height, 240);
          if (ui->windowSurface) {
            cairo_xlib_surface_set_size(ui->windowSurface, ui->windowW,
                                        ui->windowH);
          }
          updateScale(ui);
          ui->state.dirty = true;
        }
        break;
      case ButtonPress:
        handleButtonPress(ui, ev.xbutton);
        break;
      case MotionNotify:
        handleMotion(ui, ev.xmotion);
        break;
      case ButtonRelease:
        handleButtonRelease(ui);
        break;
      case LeaveNotify:
        if (ui->state.hover != -1 || ui->state.presetHover != 0) {
          ui->state.hover = -1;
          ui->state.presetHover = 0;
          ui->state.dirty = true;
        }
        break;
      default:
        break;
    }
  }
}

static int idle(LV2UI_Handle handle) {
  X11UI* ui = get(handle);
  if (!ui) {
    return 1;
  }
  processEvents(ui);

  const float prevGr = ui->state.meterGrSmooth;
  const float prevOut = ui->state.vuSmooth;
  ui->state.meterGrSmooth +=
      0.28f * (ui->state.meterGr - ui->state.meterGrSmooth);
  // A slower fall for the VU movement.
  if (ui->state.meterOut > ui->state.vuSmooth) {
    ui->state.vuSmooth += 0.55f * (ui->state.meterOut - ui->state.vuSmooth);
  } else {
    ui->state.vuSmooth += 0.10f * (ui->state.meterOut - ui->state.vuSmooth);
  }
  if (std::fabs(prevGr - ui->state.meterGrSmooth) > 0.05f ||
      std::fabs(prevOut - ui->state.vuSmooth) > 0.05f) {
    ui->state.dirty = true;
  }

  if (!ui->resizeRequested && ui->hostResize && ui->hostResize->ui_resize) {
    ui->resizeRequested = true;
    ui->hostResize->ui_resize(ui->hostResize->handle, ui->windowW, ui->windowH);
  }

  if (ui->state.dirty) {
    blit(ui);
    ui->state.dirty = false;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// LV2 UI entry points
// ---------------------------------------------------------------------------
static int ignoreXError(Display*, XErrorEvent*) { return 0; }

static bool isValidWindow(Display* display, Window window) {
  if (!window) {
    return false;
  }
  XErrorHandler old = XSetErrorHandler(ignoreXError);
  XWindowAttributes attributes;
  const Status status = XGetWindowAttributes(display, window, &attributes);
  XSync(display, False);
  XSetErrorHandler(old);
  return status != 0;
}

static float readScaleFactor(const LV2_Feature* const* features) {
  const LV2_URID_Map* map = nullptr;
  const LV2_Options_Option* options = nullptr;
  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, LV2_URID__map) && (*f)->data) {
      map = static_cast<const LV2_URID_Map*>(
          static_cast<const void*>((*f)->data));
    } else if (!std::strcmp((*f)->URI, LV2_OPTIONS__options) && (*f)->data) {
      options = static_cast<const LV2_Options_Option*>(
          static_cast<const void*>((*f)->data));
    }
  }
  if (!map || !options) {
    return 1.0f;
  }
  const LV2_URID key = map->map(map->handle, LV2_UI__scaleFactor);
  const LV2_URID tFloat = map->map(map->handle, LV2_ATOM__Float);
  const LV2_URID tDouble = map->map(map->handle, LV2_ATOM__Double);
  for (const LV2_Options_Option* o = options; o->key; ++o) {
    if (o->key != key || !o->value) {
      continue;
    }
    if (o->type == tFloat && o->size >= sizeof(float)) {
      return *static_cast<const float*>(o->value);
    }
    if (o->type == tDouble && o->size >= sizeof(double)) {
      return static_cast<float>(*static_cast<const double*>(o->value));
    }
  }
  return 1.0f;
}

static void chooseWindowSize(Display* display, float scaleFactor, int& width,
                             int& height) {
  const int screen = DefaultScreen(display);
  const double screenW = DisplayWidth(display, screen);
  const double screenH = DisplayHeight(display, screen);
  double sf = std::min(std::max(static_cast<double>(scaleFactor), 1.0), 2.5);
  double w = UI_W * sf;
  double h = UI_H * sf;
  const double maxW = screenW * 0.92;
  const double maxH = screenH * 0.86;
  const double fit = std::min(1.0, std::min(maxW / w, maxH / h));
  w *= fit;
  h *= fit;
  width = std::max(static_cast<int>(std::lround(w)), 320);
  height = std::max(static_cast<int>(std::lround(h)), 464);
}

static LV2UI_Handle uiInstantiate(const LV2UI_Descriptor*, const char*,
                                  const char* bundlePath,
                                  LV2UI_Write_Function write,
                                  LV2UI_Controller controller,
                                  LV2UI_Widget* widget,
                                  const LV2_Feature* const* features) {
  loadKnobAsset(bundlePath);
  loadMetalTexture(bundlePath);
  loadLogoFont(bundlePath);
  X11UI* ui = new X11UI();
  initState(ui->state);
  ui->host.write = write;
  ui->host.controller = controller;

  const void* parentData = nullptr;
  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, LV2_UI__parent) && (*f)->data) {
      parentData = (*f)->data;
    } else if (!std::strcmp((*f)->URI, LV2_UI__resize) && (*f)->data) {
      ui->hostResize = static_cast<const LV2UI_Resize*>(
          static_cast<const void*>((*f)->data));
    }
  }

  ui->display = XOpenDisplay(nullptr);
  if (!ui->display) {
    debugLog("instantiate: XOpenDisplay failed");
    delete ui;
    return nullptr;
  }

  ui->scaleFactor = readScaleFactor(features);
  debugLog("instantiate: features parentData=%p scale=%f", parentData,
           static_cast<double>(ui->scaleFactor));

  Window parent = 0;
  if (parentData) {
    const uintptr_t raw = reinterpret_cast<uintptr_t>(parentData);
    if (raw != 0 && raw <= 0xffffffffu &&
        isValidWindow(ui->display, static_cast<Window>(raw))) {
      parent = static_cast<Window>(raw);
    } else {
      parent = *static_cast<const Window*>(parentData);
    }
  }

  if (!isValidWindow(ui->display, parent)) {
    // Non-embedded fallback: show a top-level window rather than failing.
    parent = DefaultRootWindow(ui->display);
    ui->topLevel = true;
    debugLog("instantiate: no valid parent, using top-level window");
  }

  XWindowAttributes parentAttributes;
  if (!XGetWindowAttributes(ui->display, parent, &parentAttributes)) {
    debugLog("instantiate: XGetWindowAttributes(parent) failed");
    XCloseDisplay(ui->display);
    delete ui;
    return nullptr;
  }
  ui->visual = parentAttributes.visual;
  ui->depth = parentAttributes.depth;

  int width = 0;
  int height = 0;
  chooseWindowSize(ui->display, ui->scaleFactor, width, height);
  ui->windowW = width;
  ui->windowH = height;
  updateScale(ui);

  XSetWindowAttributes attributes;
  std::memset(&attributes, 0, sizeof(attributes));
  attributes.background_pixel = 0x0b0d10;
  attributes.border_pixel = 0;
  attributes.event_mask = ExposureMask | StructureNotifyMask |
                          ButtonPressMask | ButtonReleaseMask |
                          PointerMotionMask | LeaveWindowMask;
  attributes.colormap = parentAttributes.colormap
                            ? parentAttributes.colormap
                            : DefaultColormap(ui->display,
                                              DefaultScreen(ui->display));
  ui->parent = parent;
  ui->window = XCreateWindow(
      ui->display, parent, 0, 0, width, height, 0, parentAttributes.depth,
      InputOutput, parentAttributes.visual,
      CWBackPixel | CWBorderPixel | CWEventMask | CWColormap, &attributes);
  debugLog("instantiate: parent=0x%lx window=0x%lx size=%dx%d depth=%d",
           static_cast<unsigned long>(parent),
           static_cast<unsigned long>(ui->window), width, height,
           parentAttributes.depth);
  if (!ui->window) {
    XCloseDisplay(ui->display);
    delete ui;
    return nullptr;
  }

  if (ui->topLevel) {
    XStoreName(ui->display, ui->window, "Hearth Channel");
  }
  XMapRaised(ui->display, ui->window);
  XFlush(ui->display);

  ensureSurfaces(ui);
  blit(ui);
  *widget = reinterpret_cast<LV2UI_Widget>(static_cast<uintptr_t>(ui->window));

  if (ui->hostResize && ui->hostResize->ui_resize) {
    ui->resizeRequested = true;
    ui->hostResize->ui_resize(ui->hostResize->handle, width, height);
  }
  debugLog("instantiate: done");
  return ui;
}

static void uiCleanup(LV2UI_Handle handle) {
  X11UI* ui = get(handle);
  if (!ui) {
    return;
  }
  if (ui->imageCr) cairo_destroy(ui->imageCr);
  if (ui->imageSurface) cairo_surface_destroy(ui->imageSurface);
  if (ui->windowSurface) cairo_surface_destroy(ui->windowSurface);
  if (ui->display) {
    if (ui->window) XDestroyWindow(ui->display, ui->window);
    XCloseDisplay(ui->display);
  }
  delete ui;
}

static void uiPortEvent(LV2UI_Handle handle, uint32_t portIndex,
                        uint32_t bufferSize, uint32_t format,
                        const void* buffer) {
  X11UI* ui = get(handle);
  if (!ui || !buffer || bufferSize != sizeof(float) || format != 0) {
    return;
  }
  if (portIndex >= PORT_COUNT) {
    return;
  }
  const float value = *static_cast<const float*>(buffer);
  if (portIndex == PORT_METER_GR) {
    ui->state.meterGr = value;
  } else if (portIndex == PORT_METER_OUT) {
    ui->state.meterOut = value;
  } else {
    if (ui->state.presetIndex >= 0 &&
        std::fabs(value - ui->state.values[portIndex]) > 1.0e-6f) {
      ui->state.presetDirty = true;
    }
    ui->state.values[portIndex] = value;
    ui->state.dirty = true;
  }
}

static int uiResize(LV2UI_Handle handle, int width, int height) {
  X11UI* ui = get(handle);
  if (!ui) {
    return 1;
  }
  width = std::max(width, 160);
  height = std::max(height, 240);
  ui->windowW = width;
  ui->windowH = height;
  updateScale(ui);
  if (ui->window) {
    XResizeWindow(ui->display, ui->window, width, height);
  }
  if (ui->windowSurface) {
    cairo_xlib_surface_set_size(ui->windowSurface, width, height);
  }
  debugLog("resize: %dx%d", width, height);
  ui->state.dirty = true;
  return 0;
}

static const LV2UI_Idle_Interface kIdleInterface = {idle};
static const LV2UI_Resize kResizeInterface = {nullptr, uiResize};

static const void* uiExtensionData(const char* uri) {
  if (!std::strcmp(uri, LV2_UI__idleInterface)) {
    return &kIdleInterface;
  }
  if (!std::strcmp(uri, LV2_UI__resize)) {
    return &kResizeInterface;
  }
  return nullptr;
}

} // namespace ui
} // namespace hearth

static const LV2UI_Descriptor kDescriptor = {
    "https://github.com/tylorismusic-commits/hearth#ui",
    &hearth::ui::uiInstantiate,
    &hearth::ui::uiCleanup,
    &hearth::ui::uiPortEvent,
    &hearth::ui::uiExtensionData,
};

extern "C" LV2_SYMBOL_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index) {
  return index == 0 ? &kDescriptor : nullptr;
}

// ---------------------------------------------------------------------------
// Standalone preview (build with -DHEARTH_UI_PREVIEW)
// ---------------------------------------------------------------------------
#ifdef HEARTH_UI_PREVIEW
int main(int argc, char** argv) {
  using namespace hearth;
  using namespace hearth::ui;
  loadKnobAsset("assets/");
  loadMetalTexture("assets/");
  loadLogoFont("assets/fonts/");
  State state;
  initState(state);
  // Show the Vocal preset in the documentation screenshot.
  state.values[PORT_INPUT_GAIN] = 4.0f;
  state.values[PORT_DRIVE] = 2.0f;
  state.values[PORT_CHARACTER] = 30.0f;
  state.values[PORT_HPF_FREQ] = 80.0f;
  state.values[PORT_LOW_FREQ] = 110.0f;
  state.values[PORT_LOW_GAIN] = -1.0f;
  state.values[PORT_MID_FREQ] = 3200.0f;
  state.values[PORT_MID_GAIN] = 2.0f;
  state.values[PORT_MID_Q] = 0.5f;
  state.values[PORT_HIGH_FREQ] = 12000.0f;
  state.values[PORT_HIGH_GAIN] = 2.0f;
  state.values[PORT_COMP_THRESHOLD] = -24.0f;
  state.values[PORT_COMP_RATIO] = 3.0f;
  state.values[PORT_COMP_ATTACK] = 5.0f;
  state.values[PORT_COMP_RELEASE] = 150.0f;
  state.values[PORT_COMP_KNEE] = 6.0f;
  state.values[PORT_COMP_MAKEUP] = 3.0f;
  state.values[PORT_COMP_MIX] = 100.0f;
  state.values[PORT_COMP_MODE] = 0.0f;
  state.values[PORT_OUTPUT_GAIN] = -2.0f;
  state.presetIndex = 0;
  state.presetDirty = false;
  state.meterGrSmooth = -4.0f;
  state.vuSmooth = -9.0f;

  cairo_surface_t* surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, UI_W, UI_H);
  cairo_t* cr = cairo_create(surface);
  render(state, cr);
  cairo_destroy(cr);
  const char* path = (argc > 1) ? argv[1] : "hearth-ui.png";
  cairo_surface_write_to_png(surface, path);
  cairo_surface_destroy(surface);
  std::printf("wrote %s\n", path);
  return 0;
}
#endif
