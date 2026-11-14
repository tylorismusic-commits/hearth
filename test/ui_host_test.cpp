// Hearth Channel — X11 UI embedding smoke test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <dlfcn.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

#include <lv2/core/lv2.h>
#include <lv2/ui/ui.h>

using LV2UIDescriptorFn = const LV2UI_Descriptor* (*)(uint32_t);

static int g_writes = 0;

static void writeFn(LV2UI_Controller, uint32_t port, uint32_t size,
                    uint32_t protocol, const void* buffer) {
  (void)size;
  (void)protocol;
  ++g_writes;
  printf("UI write: port=%u value=%f\n", port,
         buffer ? *static_cast<const float*>(buffer) : 0.0f);
}

int main(int argc, char** argv) {
  const char* uiPathArg = (argc > 1) ? argv[1] : nullptr;
  const char* outPath = (argc > 2) ? argv[2] : "/tmp/opencode/hearth-ui-live.png";
  if (!uiPathArg) {
    printf("usage: %s <hearthUI.so> [out.png]\n", argv[0]);
    return 2;
  }

  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    printf("FAIL: cannot open X display\n");
    return 1;
  }

  const int width = 1400;
  const int height = 1000;
  Window parent = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0,
                                      width, height, 0, 0, 0x101216);
  XMapWindow(display, parent);
  XSync(display, False);

  void* dl = dlopen(uiPathArg, RTLD_NOW);
  if (!dl) {
    printf("FAIL: dlopen: %s\n", dlerror());
    return 1;
  }
  auto descriptorFn = reinterpret_cast<LV2UIDescriptorFn>(
      dlsym(dl, "lv2ui_descriptor"));
  if (!descriptorFn) {
    printf("FAIL: lv2ui_descriptor missing\n");
    return 1;
  }
  const LV2UI_Descriptor* descriptor = descriptorFn(0);
  if (!descriptor) {
    printf("FAIL: no UI descriptor\n");
    return 1;
  }
  printf("UI URI: %s\n", descriptor->URI);

  LV2_Feature parentFeature{LV2_UI__parent, &parent};
  const LV2_Feature* features[] = {&parentFeature, nullptr};

  std::string uiPath = uiPathArg;
  std::string bundleDir = uiPath.substr(0, uiPath.find_last_of('/') + 1);

  LV2UI_Widget widget = nullptr;
  LV2UI_Handle handle = descriptor->instantiate(
      descriptor, "https://github.com/tylorismusic-commits/hearth", bundleDir.c_str(),
      &writeFn, nullptr, &widget, features);
  if (!handle) {
    printf("FAIL: instantiate\n");
    return 1;
  }
  printf("widget=%p\n", widget);

  // Simulate the host delivering port values.
  float gr = -7.5f;
  float out = -12.0f;
  float inputGain = 4.0f;
  descriptor->port_event(handle, 26, sizeof(float), 0, &gr);
  descriptor->port_event(handle, 27, sizeof(float), 0, &out);
  descriptor->port_event(handle, 4, sizeof(float), 0, &inputGain);

  // Let the UI process its events and paint.
  const LV2UI_Idle_Interface* idle = static_cast<const LV2UI_Idle_Interface*>(
      descriptor->extension_data(LV2_UI__idleInterface));
  if (!idle) {
    printf("FAIL: no idle interface\n");
    return 1;
  }
  for (int i = 0; i < 10; ++i) {
    idle->idle(handle);
    usleep(10000);
  }

  Window uiWindow = static_cast<Window>(reinterpret_cast<uintptr_t>(widget));

  // Exercise the UI-provided resize interface first, so the coordinates below
  // match the resized layout.
  const LV2UI_Resize* resizeInterface = static_cast<const LV2UI_Resize*>(
      descriptor->extension_data(LV2_UI__resize));
  if (resizeInterface && resizeInterface->ui_resize) {
    resizeInterface->ui_resize(handle, 700, 1000);
    for (int i = 0; i < 5; ++i) {
      idle->idle(handle);
      usleep(10000);
    }
  }
  XSync(display, False);

  // Simulate a preset load, then a knob drag and a toggle click.
  const int writesBefore = g_writes;
  XTestFakeMotionEvent(display, -1, 460, 47, CurrentTime);
  XTestFakeButtonEvent(display, Button1, True, CurrentTime);
  XTestFakeButtonEvent(display, Button1, False, CurrentTime);
  XTestFakeMotionEvent(display, -1, 117, 187, CurrentTime);
  XTestFakeButtonEvent(display, Button1, True, CurrentTime);
  XTestFakeMotionEvent(display, -1, 117, 140, CurrentTime);
  XTestFakeButtonEvent(display, Button1, False, CurrentTime);
  XTestFakeMotionEvent(display, -1, 452, 712, CurrentTime);
  XTestFakeButtonEvent(display, Button1, True, CurrentTime);
  XTestFakeButtonEvent(display, Button1, False, CurrentTime);
  // Skin chip: cycles the suite skin and persists the choice.
  XTestFakeMotionEvent(display, -1, 556, 41, CurrentTime);
  XTestFakeButtonEvent(display, Button1, True, CurrentTime);
  XTestFakeButtonEvent(display, Button1, False, CurrentTime);
  // Compressor threshold and ratio knobs.
  XTestFakeMotionEvent(display, -1, 83, 784, CurrentTime);
  XTestFakeButtonEvent(display, Button1, True, CurrentTime);
  XTestFakeMotionEvent(display, -1, 83, 730, CurrentTime);
  XTestFakeButtonEvent(display, Button1, False, CurrentTime);
  XTestFakeMotionEvent(display, -1, 200, 784, CurrentTime);
  XTestFakeButtonEvent(display, Button1, True, CurrentTime);
  XTestFakeMotionEvent(display, -1, 200, 730, CurrentTime);
  XTestFakeButtonEvent(display, Button1, False, CurrentTime);
  XFlush(display);
  for (int i = 0; i < 10; ++i) {
    idle->idle(handle);
    usleep(10000);
  }
  XSync(display, False);
  const int interactionWrites = g_writes - writesBefore;
  printf("interaction writes=%d\n", interactionWrites);
  if (interactionWrites < 10) {
    printf("FAIL: preset did not write parameters\n");
    return 1;
  }

  const char* configHome = std::getenv("XDG_CONFIG_HOME");
  if (configHome && *configHome) {
    const std::string skinConfig =
        std::string(configHome) + "/hearth/skins.conf";
    FILE* config = std::fopen(skinConfig.c_str(), "r");
    if (!config) {
      printf("FAIL: skin config was not written: %s\n", skinConfig.c_str());
      return 1;
    }
    char line[128] = {};
    bool hasDefault = false;
    while (std::fgets(line, sizeof(line), config)) {
      if (std::strncmp(line, "default=", 8) == 0) {
        hasDefault = true;
        printf("skin config: %s", line);
      }
    }
    std::fclose(config);
    if (!hasDefault) {
      printf("FAIL: skin config has no default\n");
      return 1;
    }
  }

  // Capture the X11 window into a PNG so we can inspect the live result.
  XWindowAttributes uiAttributes;
  XGetWindowAttributes(display, uiWindow, &uiAttributes);
  const int uiWidth = uiAttributes.width;
  const int uiHeight = uiAttributes.height;
  cairo_surface_t* xlib =
      cairo_xlib_surface_create(display, uiWindow, DefaultVisual(display, 0),
                                uiWidth, uiHeight);
  cairo_surface_t* image =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, uiWidth, uiHeight);
  cairo_t* cr = cairo_create(image);
  cairo_set_source_surface(cr, xlib, 0.0, 0.0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_flush(image);
  const cairo_status_t status = cairo_surface_write_to_png(image, outPath);
  printf("capture status=%d path=%s writes=%d\n", static_cast<int>(status),
         outPath, g_writes);

  cairo_surface_destroy(image);
  cairo_surface_destroy(xlib);
  descriptor->cleanup(handle);
  dlclose(dl);
  XDestroyWindow(display, parent);
  XCloseDisplay(display);

  return (status == CAIRO_STATUS_SUCCESS && widget != nullptr) ? 0 : 1;
}
