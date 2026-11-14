// Hearth theme loader tests
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include "hearth_theme.hpp"

static int failures = 0;

static void check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static void writeFile(const std::string& path, const std::string& text) {
  FILE* file = std::fopen(path.c_str(), "w");
  if (!file) return;
  std::fwrite(text.data(), 1, text.size(), file);
  std::fclose(file);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: %s <test-skins-dir>\n", argv[0]);
    return 2;
  }

  char temp[] = "/tmp/hearth-theme-test-XXXXXX";
  char* tempDir = mkdtemp(temp);
  if (!tempDir) {
    std::printf("FAIL: mkdtemp\n");
    return 1;
  }

  setenv("HEARTH_SKIN_DIR", argv[1], 1);
  setenv("XDG_CONFIG_HOME", tempDir, 1);
  setenv("XDG_DATA_HOME", tempDir, 1);
  unsetenv("HEARTH_SKIN");

  using namespace hearth::theme;

  Color color;
  check(parseColor("#ff8000", color), "parse #rrggbb");
  check(color.r > 0.99 && color.g > 0.49 && color.g < 0.51 &&
            color.b < 0.01,
        "parse #rrggbb values");
  check(parseColor("255,128,0", color), "parse decimal rgb");
  check(color.r > 0.99 && color.g > 0.49 && color.g < 0.51 &&
            color.b < 0.01,
        "parse decimal rgb values");
  check(parseColor("#11223344", color), "parse #rrggbbaa");
  check(color.r > 0.06 && color.r < 0.07 && color.a > 0.26 &&
            color.a < 0.27,
        "parse #rrggbbaa values");
  check(!parseColor("not-a-color", color), "reject bad color");

  Manager manager;
  manager.init("", "hearth");
  check(manager.currentId() == "classic", "default is classic");
  check(manager.skins().size() >= 3, "discovers test skins");

  check(manager.set("test-light"), "set test-light");
  check(manager.currentId() == "test-light", "current is test-light");
  check(manager.theme().name == "Test Light", "skin name loaded");
  check(manager.theme().background.r > 0.9, "light background loaded");
  check(manager.theme().dir.find("test-light") != std::string::npos,
        "skin directory tracked");

  const std::string configPath = manager.configPath();
  check(fileExists(configPath), "config file written");
  check(manager.configDefault() == "test-light", "config default written");

  check(manager.set("does-not-exist") == false, "reject missing skin");
  check(manager.currentId() == "test-light", "missing skin leaves current");

  check(manager.cycle(1), "cycle forward");
  check(manager.currentId() == "classic", "cycle wraps to classic");
  check(manager.cycle(-1), "cycle backward");
  check(manager.currentId() == "test-light", "cycle wraps backward");

  check(manager.set("test-dark"), "set test-dark");
  check(manager.currentId() == "test-dark", "current is test-dark");
  check(manager.theme().background.r < 0.2, "dark background loaded");

  // External config change should be picked up after the mtime changes.
  sleep(2);
  writeFile(configPath, "default=test-light\n");
  check(manager.reloadIfConfigChanged(), "reload detects config change");
  check(manager.currentId() == "test-light", "reload applies skin");

  std::printf(failures == 0 ? "ALL THEME TESTS PASSED\n"
                            : "%d THEME TESTS FAILED\n",
              failures);
  return failures == 0 ? 0 : 1;
}
