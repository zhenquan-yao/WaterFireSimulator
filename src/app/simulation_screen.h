#pragma once

#include "doom_fire.h"
#include "gravity_input.h"
#include "lvgl.h"

#include <cstdint>
#include <memory>
#include <vector>

struct FlipFluid;

namespace app {

class AssetManager;

class SimulationScreen {
public:
    SimulationScreen(AssetManager& assets, bool& running);
    ~SimulationScreen();

    SimulationScreen(const SimulationScreen&) = delete;
    SimulationScreen& operator=(const SimulationScreen&) = delete;

private:
    enum class Mode : uint8_t {
        Water,
        Fire,
    };

    static void frame_timer_cb(lv_timer_t* timer);
    static void toast_timer_cb(lv_timer_t* timer);
    static void toolbar_anim_cb(void* object, int32_t value);
    static void key_listener(uint32_t key, bool pressed, void* user_data);
    static void input_activity_listener(void* user_data);
    static void water_button_cb(lv_event_t* event);
    static void fire_button_cb(lv_event_t* event);
    static void color_button_cb(lv_event_t* event);
    static void reset_button_cb(lv_event_t* event);

    void build_ui(AssetManager& assets);
    lv_obj_t* create_button(lv_obj_t* parent, const char* text, int32_t width);
    void tick();
    void render_water(uint32_t* pixels, uint32_t stride);
    void render_fire(uint32_t* pixels, uint32_t stride);
    void set_mode(Mode mode);
    void cycle_palette();
    void reset_simulation();
    void update_controls();
    void reveal_toolbar();
    void hide_toolbar();
    void animate_toolbar(int32_t target_y, uint32_t duration_ms);
    void update_toolbar_visibility();
    void show_exit_toast();
    void handle_key(uint32_t key, bool pressed);

    bool& running_;
    GravityInput gravity_{};
    Mode mode_{Mode::Water};
    uint8_t water_palette_{0};
    uint8_t fire_palette_{0};
    FlipFluid* water_{nullptr};
    std::vector<float> water_grid_{};
    std::vector<float> water_raw_grid_{};
    std::vector<float> water_smooth_grid_{};
    int water_width_{0};
    int water_height_{0};
    std::unique_ptr<doom_fire_t> fire_{};
    lv_obj_t* root_{nullptr};
    lv_obj_t* canvas_{nullptr};
    lv_obj_t* toolbar_{nullptr};
    lv_obj_t* water_button_{nullptr};
    lv_obj_t* fire_button_{nullptr};
    lv_obj_t* color_button_{nullptr};
    lv_obj_t* reset_button_{nullptr};
    lv_obj_t* toast_{nullptr};
    lv_obj_t* exit_progress_{nullptr};
    lv_draw_buf_t* draw_buffer_{nullptr};
    lv_timer_t* frame_timer_{nullptr};
    lv_timer_t* toast_timer_{nullptr};
    bool escape_pressed_{false};
    bool escape_exit_fired_{false};
    uint32_t escape_pressed_at_{0};
    uint32_t toolbar_interaction_at_{0};
    bool toolbar_visible_{true};
};

} // namespace app
