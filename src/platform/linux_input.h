#pragma once

#include "lvgl.h"

#include <cstddef>

namespace platform {

constexpr size_t kNavKeyCount = 5;
using KeyListener = void (*)(uint32_t key, bool pressed, void* user_data);
using InputActivityListener = void (*)(void* user_data);

void init_key_input(lv_display_t* display);
void attach_key_router(lv_indev_t* indev);
void set_nav_shortcut_mode(bool enabled);
void register_nav_button(size_t index, lv_obj_t* button);
void unregister_nav_button(size_t index, lv_obj_t* button);
void set_key_listener(KeyListener listener, void* user_data);
void clear_key_listener(KeyListener listener, void* user_data);
void set_input_activity_listener(InputActivityListener listener, void* user_data);
void clear_input_activity_listener(InputActivityListener listener, void* user_data);
void route_key_state(uint32_t key, bool pressed);

} // namespace platform
