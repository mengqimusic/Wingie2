#ifndef WINGIE2_CONFIG_RUNTIME_H
#define WINGIE2_CONFIG_RUNTIME_H

#include <stdint.h>

enum ConfigOrigin : uint8_t {
  CONFIG_ORIGIN_STARTUP,
  CONFIG_ORIGIN_HARDWARE,
  CONFIG_ORIGIN_MIDI,
  CONFIG_ORIGIN_WEB
};

enum PerformanceParameter : uint8_t {
  PERFORMANCE_MIX,
  PERFORMANCE_DECAY,
  PERFORMANCE_VOLUME
};

extern float channel_mix[2];
extern float channel_decay[2];
extern float channel_volume[2];
extern bool tuning_dirty;
extern uint32_t tuning_revision;
extern uint32_t config_boot_id;
extern volatile uint32_t config_state_revision;
extern volatile bool config_event_pending;
extern volatile ConfigOrigin config_last_origin;

void initialize_config_runtime();
void lock_config_state();
void unlock_config_state();
void mark_config_state_changed(ConfigOrigin origin);
bool configuration_is_dirty();
bool set_channel_mode(uint8_t channel, int value, ConfigOrigin origin);
bool set_channel_performance_parameter(uint8_t channel, PerformanceParameter parameter,
                                       float value, ConfigOrigin origin, bool remote_takeover);
bool set_channel_threshold(uint8_t channel, float value, ConfigOrigin origin);
bool set_clip_gain(bool post, float value, ConfigOrigin origin);
bool set_a3_frequency(float value, ConfigOrigin origin);
bool set_tuning_index(int value, ConfigOrigin origin);
bool set_midi_channel(uint8_t slot, int value, ConfigOrigin origin);
bool set_ratio_value(uint8_t index, float value, ConfigOrigin origin, float *canonical = nullptr);
bool set_cave_frequency(uint8_t channel, uint8_t bank, uint8_t index, float value,
                        ConfigOrigin origin, float *canonical = nullptr);
bool set_cave_mute(uint8_t channel, uint8_t bank, uint8_t index, bool value, ConfigOrigin origin);

#endif
