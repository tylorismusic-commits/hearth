// Hearth Channel — lilv host integration test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdio>

#include <lilv/lilv.h>

#include "param_info.hpp"

using namespace hearth;

static int g_failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    printf("FAIL: %s\n", what);
  }
}

int main(int argc, char** argv) {
  const char* bundleDir = (argc > 1) ? argv[1] : nullptr;
  if (!bundleDir) {
    printf("usage: %s <bundle-dir>\n", argv[0]);
    return 2;
  }

  LilvWorld* world = lilv_world_new();
  LilvNode* pathNode = lilv_new_string(world, bundleDir);
  lilv_world_set_option(world, LILV_OPTION_LV2_PATH, pathNode);
  lilv_node_free(pathNode);
  lilv_world_load_all(world);

  LilvNode* uri = lilv_new_uri(world, "https://github.com/tylorismusic-commits/hearth");
  const LilvPlugins* plugins = lilv_world_get_all_plugins(world);
  const LilvPlugin* plugin = lilv_plugins_get_by_uri(plugins, uri);
  lilv_node_free(uri);
  check(plugin != nullptr, "plugin found in bundle");
  if (!plugin) {
    lilv_world_free(world);
    return 1;
  }

  const uint32_t numPorts = lilv_plugin_get_num_ports(plugin);
  check(numPorts == PORT_COUNT, "port count matches RDF");

  LilvUIs* uis = lilv_plugin_get_uis(plugin);
  check(lilv_uis_size(uis) >= 1, "at least one UI declared");
  lilv_uis_free(uis);

  LilvNode* presetClass =
      lilv_new_uri(world, "http://lv2plug.in/ns/ext/presets#Preset");
  LilvNodes* presets = lilv_plugin_get_related(plugin, presetClass);
  check(lilv_nodes_size(presets) >= 4, "factory presets found");
  lilv_nodes_free(presets);
  lilv_node_free(presetClass);

  LilvInstance* instance = lilv_plugin_instantiate(plugin, 48000.0, nullptr);
  check(instance != nullptr, "plugin instantiated");
  if (!instance) {
    lilv_world_free(world);
    return 1;
  }

  const uint32_t block = 256;
  float audioInL[block];
  float audioInR[block];
  float audioOutL[block];
  float audioOutR[block];
  float ctrl[PORT_COUNT] = {};

  for (uint32_t i = 0; i < PORT_COUNT; ++i) {
    if (i < 4) {
      void* ptr = (i == 0) ? static_cast<void*>(audioInL)
                  : (i == 1) ? static_cast<void*>(audioInR)
                  : (i == 2) ? static_cast<void*>(audioOutL)
                             : static_cast<void*>(audioOutR);
      lilv_instance_connect_port(instance, i, ptr);
    } else {
      lilv_instance_connect_port(instance, i, &ctrl[i]);
    }
  }

  // Defaults (the host normally supplies these from the RDF).
  ctrl[PORT_CHARACTER] = 25.0f;
  ctrl[PORT_COMP_ON] = 1.0f;
  ctrl[PORT_COMP_MIX] = 100.0f;
  ctrl[PORT_COMP_THRESHOLD] = -18.0f;
  ctrl[PORT_COMP_RATIO] = 4.0f;
  ctrl[PORT_COMP_ATTACK] = 10.0f;
  ctrl[PORT_COMP_RELEASE] = 250.0f;
  ctrl[PORT_COMP_KNEE] = 6.0f;
  ctrl[PORT_COMP_MODE] = 0.0f;
  ctrl[PORT_ENABLE] = 1.0f;
  ctrl[PORT_LOW_FREQ] = 110.0f;
  ctrl[PORT_MID_FREQ] = 1600.0f;
  ctrl[PORT_MID_Q] = 0.7f;
  ctrl[PORT_HIGH_FREQ] = 12000.0f;

  lilv_instance_activate(instance);

  // 1) Run a sine through the default chain; all outputs must be finite.
  for (uint32_t b = 0; b < 4; ++b) {
    for (uint32_t i = 0; i < block; ++i) {
      const float x = std::sin(2.0f * 3.14159265358979f * 1000.0f *
                               (b * block + i) / 48000.0f) * 0.5f;
      audioInL[i] = x;
      audioInR[i] = x;
    }
    lilv_instance_run(instance, block);
  }
  bool finite = true;
  for (uint32_t i = 0; i < block; ++i) {
    finite = finite && std::isfinite(audioOutL[i]) && std::isfinite(audioOutR[i]);
  }
  check(finite, "host run produces finite audio");
  check(std::isfinite(ctrl[PORT_METER_OUT]), "output meter finite");

  // 2) Flat chain with the compressor off should be close to unity.
  for (uint32_t i = 4; i < PORT_COUNT; ++i) {
    ctrl[i] = 0.0f;
  }
  ctrl[PORT_COMP_ON] = 0.0f;
  ctrl[PORT_ENABLE] = 1.0f;
  ctrl[PORT_CHARACTER] = 0.0f;
  ctrl[PORT_DRIVE] = 0.0f;
  ctrl[PORT_HPF_FREQ] = 0.0f;
  ctrl[PORT_OUTPUT_GAIN] = 0.0f;
  ctrl[PORT_LOW_FREQ] = 110.0f;
  ctrl[PORT_MID_FREQ] = 1600.0f;
  ctrl[PORT_MID_Q] = 0.7f;
  ctrl[PORT_HIGH_FREQ] = 12000.0f;

  for (int b = 0; b < 20; ++b) {
    for (uint32_t i = 0; i < block; ++i) {
      const float x = std::sin(2.0f * 3.14159265358979f * 1000.0f *
                               (b * block + i) / 48000.0f) * 0.1f;
      audioInL[i] = x;
      audioInR[i] = x;
    }
    lilv_instance_run(instance, block);
  }
  float peak = 0.0f;
  for (uint32_t i = 0; i < block; ++i) {
    peak = std::max(peak, std::fabs(audioOutL[i]));
  }
  check(peak > 0.085f && peak < 0.115f, "host flat chain is near unity");

  // 3) Bypass must pass the dry signal through.
  ctrl[PORT_ENABLE] = 0.0f;
  ctrl[PORT_LOW_GAIN] = 0.0f;
  for (int b = 0; b < 20; ++b) {
    for (uint32_t i = 0; i < block; ++i) {
      audioInL[i] = 0.05f;
      audioInR[i] = 0.05f;
    }
    lilv_instance_run(instance, block);
  }
  check(std::fabs(audioOutL[block - 1] - 0.05f) < 0.002f,
        "host bypass passes dry signal");

  lilv_instance_deactivate(instance);
  lilv_instance_free(instance);
  lilv_world_free(world);

  if (g_failures == 0) {
    printf("ALL HOST TESTS PASSED\n");
  }
  return g_failures == 0 ? 0 : 1;
}
