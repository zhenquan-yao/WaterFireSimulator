#include "simulation_screen.h"

#include "asset_manager.h"
#include "flip.h"
#include "linux_input.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace app {
namespace {

constexpr int32_t kWidth = 320;
constexpr int32_t kHeight = 170;
constexpr uint32_t kFrameMs = 33;
constexpr uint32_t kExitHoldMs = 3000;
constexpr uint32_t kToolbarIdleMs = 2800;
constexpr int32_t kToolbarHeight = 32;
constexpr int32_t kToolbarHiddenY = -kToolbarHeight;
constexpr int kWaterSubsteps = 2;

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

constexpr std::array<std::array<Rgb, 6>, 3> kWaterPalettes{{
    {{{2, 7, 13}, {64, 190, 225}, {30, 145, 205}, {14, 103, 178}, {7, 70, 145}, {4, 43, 105}}},
    {{{0, 0, 0}, {255, 153, 255}, {255, 102, 255}, {255, 51, 204}, {255, 51, 153}, {255, 0, 102}}},
    {{{0, 0, 0}, {20, 255, 30}, {15, 255, 20}, {10, 255, 10}, {5, 255, 5}, {0, 255, 0}}},
}};

constexpr std::array<uint32_t, 3> kWaterButtonColors{
    0x23b5e8, 0xd64ca8, 0x39c65a};
constexpr std::array<uint32_t, 3> kFireButtonColors{
    0xd1580a, 0x2662e7, 0xab20b2};

uint8_t lerp_channel(uint8_t a, uint8_t b, float t) {
    return static_cast<uint8_t>(std::lround(static_cast<float>(a) +
                                            (static_cast<float>(b) - static_cast<float>(a)) * t));
}

Rgb lerp_rgb(Rgb a, Rgb b, float t) {
    return {lerp_channel(a.r, b.r, t),
            lerp_channel(a.g, b.g, t),
            lerp_channel(a.b, b.b, t)};
}

Rgb water_color(float level, uint8_t palette_index) {
    const float normalized = std::clamp(level / 20.0f, 0.0f, 1.0f);
    const float position = normalized * 5.0f;
    const int first = std::min(static_cast<int>(position), 5);
    const int second = std::min(first + 1, 5);
    return lerp_rgb(kWaterPalettes[palette_index][first],
                    kWaterPalettes[palette_index][second],
                    position - static_cast<float>(first));
}

Rgb fire_color(float heat, uint8_t palette_index) {
    const float t = std::clamp(heat / static_cast<float>(DOOM_HEAT_MAX), 0.0f, 1.0f);
    if (palette_index == 1) {
        if (t < 0.45f) return lerp_rgb({2, 3, 8}, {20, 45, 220}, t / 0.45f);
        if (t < 0.78f) return lerp_rgb({20, 45, 220}, {80, 220, 255}, (t - 0.45f) / 0.33f);
        return lerp_rgb({80, 220, 255}, {255, 255, 255}, (t - 0.78f) / 0.22f);
    }
    if (palette_index == 2) {
        if (t < 0.45f) return lerp_rgb({5, 0, 8}, {135, 15, 180}, t / 0.45f);
        if (t < 0.78f) return lerp_rgb({135, 15, 180}, {255, 70, 175}, (t - 0.45f) / 0.33f);
        return lerp_rgb({255, 70, 175}, {255, 245, 255}, (t - 0.78f) / 0.22f);
    }
    if (t < 0.35f) return lerp_rgb({5, 3, 3}, {155, 20, 5}, t / 0.35f);
    if (t < 0.72f) return lerp_rgb({155, 20, 5}, {255, 145, 15}, (t - 0.35f) / 0.37f);
    return lerp_rgb({255, 145, 15}, {255, 255, 225}, (t - 0.72f) / 0.28f);
}

uint32_t pixel_value(Rgb color) {
    return 0xff000000U | (static_cast<uint32_t>(color.r) << 16U) |
           (static_cast<uint32_t>(color.g) << 8U) | color.b;
}

void set_button_color(lv_obj_t* button, uint32_t color, bool active) {
    if (!button) return;
    lv_obj_set_style_bg_color(button, lv_color_hex(active ? color : 0x15181d), 0);
    lv_obj_set_style_border_color(button, lv_color_hex(active ? color : 0x404750), 0);
    lv_obj_set_style_text_color(button, lv_color_hex(active ? 0xffffff : 0xb8c0ca), 0);
}

} // namespace

SimulationScreen::SimulationScreen(AssetManager& assets, bool& running) : running_(running) {
    fire_ = std::make_unique<doom_fire_t>();
    doom_fire_init(fire_.get(), 0x57415445U);
    doom_fire_reset(fire_.get());
    reset_simulation();
    build_ui(assets);
    platform::set_key_listener(key_listener, this);
    platform::set_input_activity_listener(input_activity_listener, this);
    frame_timer_ = lv_timer_create(frame_timer_cb, kFrameMs, this);
}

SimulationScreen::~SimulationScreen() {
    platform::clear_input_activity_listener(input_activity_listener, this);
    platform::clear_key_listener(key_listener, this);
    if (frame_timer_) lv_timer_delete(frame_timer_);
    if (toast_timer_) lv_timer_delete(toast_timer_);
    if (toolbar_) lv_anim_delete(toolbar_, toolbar_anim_cb);
    if (water_) flip_destroy(water_);
    if (root_ && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    if (draw_buffer_) lv_draw_buf_destroy(draw_buffer_);
}

void SimulationScreen::build_ui(AssetManager& assets) {
    root_ = lv_obj_create(nullptr);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, kWidth, kHeight);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x05080d), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    draw_buffer_ = lv_draw_buf_create(kWidth, kHeight, LV_COLOR_FORMAT_XRGB8888, 0);
    canvas_ = lv_canvas_create(root_);
    lv_canvas_set_draw_buf(canvas_, draw_buffer_);
    lv_obj_align(canvas_, LV_ALIGN_CENTER, 0, 0);

    toolbar_ = lv_obj_create(root_);
    lv_obj_remove_style_all(toolbar_);
    lv_obj_set_size(toolbar_, 268, kToolbarHeight);
    lv_obj_align(toolbar_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(toolbar_, lv_color_hex(0x080b10), 0);
    lv_obj_set_style_bg_opa(toolbar_, 235, 0);
    lv_obj_set_style_border_width(toolbar_, 1, 0);
    lv_obj_set_style_border_color(toolbar_, lv_color_hex(0x303842), 0);
    lv_obj_set_style_radius(toolbar_, 6, 0);
    lv_obj_set_style_pad_hor(toolbar_, 6, 0);
    lv_obj_set_style_pad_ver(toolbar_, 5, 0);
    lv_obj_set_style_pad_column(toolbar_, 4, 0);
    lv_obj_set_flex_flow(toolbar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(toolbar_, LV_OBJ_FLAG_SCROLLABLE);

    auto* mode_group = lv_obj_create(toolbar_);
    lv_obj_remove_style_all(mode_group);
    lv_obj_set_size(mode_group, 120, 22);
    lv_obj_set_flex_flow(mode_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(mode_group, 2, 0);
    lv_obj_clear_flag(mode_group, LV_OBJ_FLAG_SCROLLABLE);
    water_button_ = create_button(mode_group, "1:WATER", 59);
    fire_button_ = create_button(mode_group, "2:FIRE", 59);
    lv_obj_add_event_cb(water_button_, water_button_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(fire_button_, fire_button_cb, LV_EVENT_CLICKED, this);

    color_button_ = create_button(toolbar_, "SPC:COLOR", 62);
    reset_button_ = create_button(toolbar_, "R:RESET", 62);
    lv_obj_add_event_cb(color_button_, color_button_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(reset_button_, reset_button_cb, LV_EVENT_CLICKED, this);

    const lv_font_t* font = assets.load_font("inter-semibold.ttf", 10);
    if (font) {
        lv_obj_set_style_text_font(toolbar_, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    toast_ = lv_label_create(root_);
    lv_label_set_text(toast_, "Hold ESC for 3s to exit");
    lv_obj_set_style_bg_color(toast_, lv_color_hex(0x11161d), 0);
    lv_obj_set_style_bg_opa(toast_, 235, 0);
    lv_obj_set_style_text_color(toast_, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_pad_hor(toast_, 10, 0);
    lv_obj_set_style_pad_ver(toast_, 6, 0);
    lv_obj_set_style_radius(toast_, 4, 0);
    lv_obj_align(toast_, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    if (font) lv_obj_set_style_text_font(toast_, font, 0);

    exit_progress_ = lv_bar_create(root_);
    lv_obj_set_size(exit_progress_, kWidth, 3);
    lv_obj_align(exit_progress_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(exit_progress_, 0, static_cast<int32_t>(kExitHoldMs));
    lv_bar_set_value(exit_progress_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_opa(exit_progress_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(exit_progress_, lv_color_hex(0xff4d4d), LV_PART_INDICATOR);
    lv_obj_add_flag(exit_progress_, LV_OBJ_FLAG_HIDDEN);

    toast_timer_ = lv_timer_create(toast_timer_cb, 1400, this);
    lv_timer_pause(toast_timer_);
    toolbar_interaction_at_ = lv_tick_get();
    lv_screen_load(root_);
    update_controls();
    tick();
}

lv_obj_t* SimulationScreen::create_button(lv_obj_t* parent, const char* text, int32_t width) {
    auto* button = lv_button_create(parent);
    lv_obj_set_size(button, width, 22);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

void SimulationScreen::tick() {
    gravity_.update();
    const auto gravity = gravity_.value();
    update_toolbar_visibility();

    if (mode_ == Mode::Water && water_) {
        for (int step = 0; step < kWaterSubsteps; ++step) {
            flip_step(water_, 1.0f / (30.0f * kWaterSubsteps),
                      gravity.x, gravity.y);
        }
        flip_get_led_grid(water_, water_raw_grid_.data());

        for (int x = 0; x < water_width_; ++x) {
            const int left_x = std::max(x - 1, 0);
            const int right_x = std::min(x + 1, water_width_ - 1);
            for (int y = 0; y < water_height_; ++y) {
                const size_t center = static_cast<size_t>(x * water_height_ + y);
                water_smooth_grid_[center] =
                    (water_raw_grid_[static_cast<size_t>(left_x * water_height_ + y)] +
                     2.0f * water_raw_grid_[center] +
                     water_raw_grid_[static_cast<size_t>(right_x * water_height_ + y)]) *
                    0.25f;
            }
        }

        const float motion = flip_motion_level(water_);
        const float temporal_alpha = motion < 0.18f ? 0.42f : 0.70f;
        for (int x = 0; x < water_width_; ++x) {
            for (int y = 0; y < water_height_; ++y) {
                const int bottom_y = std::max(y - 1, 0);
                const int top_y = std::min(y + 1, water_height_ - 1);
                const size_t center = static_cast<size_t>(x * water_height_ + y);
                const float smooth =
                    (water_smooth_grid_[static_cast<size_t>(x * water_height_ + bottom_y)] +
                     2.0f * water_smooth_grid_[center] +
                     water_smooth_grid_[static_cast<size_t>(x * water_height_ + top_y)]) *
                    0.25f;
                water_grid_[center] +=
                    (smooth - water_grid_[center]) * temporal_alpha;
            }
        }
    } else {
        doom_fire_step(fire_.get(), gravity.x, gravity.y, lv_tick_get());
    }

    auto* pixels = reinterpret_cast<uint32_t*>(draw_buffer_->data);
    const uint32_t stride = draw_buffer_->header.stride / sizeof(uint32_t);
    if (mode_ == Mode::Water) render_water(pixels, stride);
    else render_fire(pixels, stride);
    lv_draw_buf_flush_cache(draw_buffer_, nullptr);
    lv_obj_invalidate(canvas_);

    if (escape_pressed_ && !escape_exit_fired_) {
        const uint32_t elapsed = lv_tick_elaps(escape_pressed_at_);
        lv_bar_set_value(exit_progress_, static_cast<int32_t>(std::min(elapsed, kExitHoldMs)), LV_ANIM_OFF);
        if (elapsed >= kExitHoldMs) {
            escape_exit_fired_ = true;
            running_ = false;
        }
    }
}

void SimulationScreen::render_water(uint32_t* pixels, uint32_t stride) {
    if (!pixels || water_width_ <= 0 || water_height_ <= 0) return;
    for (int y = 0; y < kHeight; ++y) {
        const float source_y = static_cast<float>(water_height_ - 1) -
                               static_cast<float>(y) * static_cast<float>(water_height_ - 1) /
                                   static_cast<float>(kHeight - 1);
        const int y0 = std::clamp(static_cast<int>(std::floor(source_y)), 0, water_height_ - 1);
        const int y1 = std::min(y0 + 1, water_height_ - 1);
        const float ty = source_y - static_cast<float>(y0);
        for (int x = 0; x < kWidth; ++x) {
            const float source_x = static_cast<float>(x) * static_cast<float>(water_width_ - 1) /
                                   static_cast<float>(kWidth - 1);
            const int x0 = std::clamp(static_cast<int>(std::floor(source_x)), 0, water_width_ - 1);
            const int x1 = std::min(x0 + 1, water_width_ - 1);
            const float tx = source_x - static_cast<float>(x0);
            const float a = water_grid_[static_cast<size_t>(x0 * water_height_ + y0)];
            const float b = water_grid_[static_cast<size_t>(x1 * water_height_ + y0)];
            const float c = water_grid_[static_cast<size_t>(x0 * water_height_ + y1)];
            const float d = water_grid_[static_cast<size_t>(x1 * water_height_ + y1)];
            const float level = (a + (b - a) * tx) * (1.0f - ty) +
                                (c + (d - c) * tx) * ty;
            Rgb color = water_color(level, water_palette_);
            if (level < 0.15f) {
                const uint8_t depth = static_cast<uint8_t>(6 + (y * 10 / kHeight));
                color = {2, depth, static_cast<uint8_t>(depth + 8)};
            }
            pixels[static_cast<uint32_t>(y) * stride + x] = pixel_value(color);
        }
    }
}

void SimulationScreen::render_fire(uint32_t* pixels, uint32_t stride) {
    const float* heat = doom_fire_heat(fire_.get());
    for (int y = 0; y < kHeight; ++y) {
        // The last two simulation rows are the burner, not visible flame.
        const float source_y = static_cast<float>(y) * static_cast<float>(DOOM_H - 3) /
                               static_cast<float>(kHeight - 1);
        const int y0 = std::clamp(static_cast<int>(std::floor(source_y)), 0, DOOM_H - 1);
        const int y1 = std::min(y0 + 1, DOOM_H - 1);
        const float ty = source_y - static_cast<float>(y0);
        for (int x = 0; x < kWidth; ++x) {
            const float source_x = static_cast<float>(x) * static_cast<float>(DOOM_W - 1) /
                                   static_cast<float>(kWidth - 1);
            const int x0 = std::clamp(static_cast<int>(std::floor(source_x)), 0, DOOM_W - 1);
            const int x1 = std::min(x0 + 1, DOOM_W - 1);
            const float tx = source_x - static_cast<float>(x0);
            const float a = heat[y0 * DOOM_W + x0];
            const float b = heat[y0 * DOOM_W + x1];
            const float c = heat[y1 * DOOM_W + x0];
            const float d = heat[y1 * DOOM_W + x1];
            const float value = (a + (b - a) * tx) * (1.0f - ty) +
                                (c + (d - c) * tx) * ty;
            pixels[static_cast<uint32_t>(y) * stride + x] =
                pixel_value(fire_color(value, fire_palette_));
        }
    }

    // Keep cinders crisp after the low-resolution flame body is upscaled.
    for (const doom_ember_t& ember : fire_->embers) {
        if (!ember.active) continue;
        const int x = std::clamp(static_cast<int>(std::lround(
                                     ember.x * static_cast<float>(kWidth - 1) /
                                     static_cast<float>(DOOM_W - 1))),
                                 0, kWidth - 1);
        const int y = std::clamp(static_cast<int>(std::lround(
                                     ember.y * static_cast<float>(kHeight - 1) /
                                     static_cast<float>(DOOM_H - 1))),
                                 0, kHeight - 1);
        const uint32_t core = pixel_value(fire_color(
            std::min(ember.heat + 8.0f, static_cast<float>(DOOM_HEAT_MAX)),
            fire_palette_));
        const uint32_t tail = pixel_value(fire_color(ember.heat * 0.58f,
                                                     fire_palette_));
        pixels[static_cast<uint32_t>(y) * stride + x] = core;
        if (x + 1 < kWidth) pixels[static_cast<uint32_t>(y) * stride + x + 1] = core;
        if (y + 1 < kHeight) {
            pixels[static_cast<uint32_t>(y + 1) * stride + x] = core;
            if (x + 1 < kWidth) {
                pixels[static_cast<uint32_t>(y + 1) * stride + x + 1] = core;
            }
        }
        if (y + 2 < kHeight) pixels[static_cast<uint32_t>(y + 2) * stride + x] = tail;
        if (y + 3 < kHeight) pixels[static_cast<uint32_t>(y + 3) * stride + x] = tail;
    }
}

void SimulationScreen::set_mode(Mode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    update_controls();
}

void SimulationScreen::cycle_palette() {
    if (mode_ == Mode::Water) water_palette_ = static_cast<uint8_t>((water_palette_ + 1) % 3);
    else fire_palette_ = static_cast<uint8_t>((fire_palette_ + 1) % 3);
    update_controls();
}

void SimulationScreen::reset_simulation() {
    if (water_) {
        flip_destroy(water_);
        water_ = nullptr;
    }
    water_ = flip_create(2.0f, 1.0f, 48, 0.40f);
    if (water_) {
        flip_set_gravity_scale(water_, 9.81f);
        flip_set_solver_quality(water_, 4, 28, 0.42f);
        flip_set_velocity_damping(water_, 0.995f, 0.0025f);
        water_width_ = flip_grid_width(water_);
        water_height_ = flip_grid_height(water_);
        water_grid_.assign(static_cast<size_t>(water_width_ * water_height_), 0.0f);
        water_raw_grid_.assign(static_cast<size_t>(water_width_ * water_height_), 0.0f);
        water_smooth_grid_.assign(static_cast<size_t>(water_width_ * water_height_), 0.0f);
    }
    doom_fire_reset(fire_.get());
}

void SimulationScreen::update_controls() {
    set_button_color(water_button_, 0x008fd5, mode_ == Mode::Water);
    set_button_color(fire_button_, kFireButtonColors[fire_palette_],
                     mode_ == Mode::Fire);
    const uint32_t palette_color = mode_ == Mode::Water
                                       ? kWaterButtonColors[water_palette_]
                                       : kFireButtonColors[fire_palette_];
    set_button_color(color_button_, palette_color, true);
    set_button_color(reset_button_, 0x66707c, false);
}

void SimulationScreen::animate_toolbar(int32_t target_y, uint32_t duration_ms) {
    if (!toolbar_) return;
    lv_anim_delete(toolbar_, toolbar_anim_cb);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, toolbar_);
    lv_anim_set_exec_cb(&animation, toolbar_anim_cb);
    lv_anim_set_values(&animation, lv_obj_get_y(toolbar_), target_y);
    lv_anim_set_duration(&animation, duration_ms);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

void SimulationScreen::reveal_toolbar() {
    toolbar_interaction_at_ = lv_tick_get();
    if (toolbar_visible_) return;
    toolbar_visible_ = true;
    animate_toolbar(0, 160);
}

void SimulationScreen::hide_toolbar() {
    if (!toolbar_visible_) return;
    toolbar_visible_ = false;
    animate_toolbar(kToolbarHiddenY, 220);
}

void SimulationScreen::update_toolbar_visibility() {
    if (toolbar_visible_ && !escape_pressed_ &&
        lv_tick_elaps(toolbar_interaction_at_) >= kToolbarIdleMs) {
        hide_toolbar();
    }
}

void SimulationScreen::show_exit_toast() {
    lv_obj_remove_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    lv_timer_set_repeat_count(toast_timer_, 1);
    lv_timer_resume(toast_timer_);
    lv_timer_reset(toast_timer_);
}

void SimulationScreen::handle_key(uint32_t key, bool pressed) {
    if (pressed) reveal_toolbar();
    gravity_.set_virtual_key(key, pressed);
    if (key == LV_KEY_ESC) {
        if (pressed && !escape_pressed_) {
            escape_pressed_ = true;
            escape_exit_fired_ = false;
            escape_pressed_at_ = lv_tick_get();
            lv_bar_set_value(exit_progress_, 0, LV_ANIM_OFF);
            lv_obj_remove_flag(exit_progress_, LV_OBJ_FLAG_HIDDEN);
        } else if (!pressed && escape_pressed_) {
            escape_pressed_ = false;
            lv_obj_add_flag(exit_progress_, LV_OBJ_FLAG_HIDDEN);
            if (!escape_exit_fired_) show_exit_toast();
        }
        return;
    }
    if (!pressed) return;

    if (key == LV_KEY_NEXT) {
        set_mode(mode_ == Mode::Water ? Mode::Fire : Mode::Water);
    } else if (key == ' ') {
        cycle_palette();
    } else if (key == LV_KEY_ENTER || key == 'r' || key == 'R') {
        reset_simulation();
    } else if (key == '1') {
        set_mode(Mode::Water);
    } else if (key == '2') {
        set_mode(Mode::Fire);
    }
}

void SimulationScreen::frame_timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<SimulationScreen*>(lv_timer_get_user_data(timer));
    if (screen) screen->tick();
}

void SimulationScreen::toast_timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<SimulationScreen*>(lv_timer_get_user_data(timer));
    if (screen && screen->toast_) lv_obj_add_flag(screen->toast_, LV_OBJ_FLAG_HIDDEN);
}

void SimulationScreen::toolbar_anim_cb(void* object, int32_t value) {
    lv_obj_set_y(static_cast<lv_obj_t*>(object), value);
}

void SimulationScreen::key_listener(uint32_t key, bool pressed, void* user_data) {
    auto* screen = static_cast<SimulationScreen*>(user_data);
    if (screen) screen->handle_key(key, pressed);
}

void SimulationScreen::input_activity_listener(void* user_data) {
    auto* screen = static_cast<SimulationScreen*>(user_data);
    if (screen) screen->reveal_toolbar();
}

void SimulationScreen::water_button_cb(lv_event_t* event) {
    auto* screen = static_cast<SimulationScreen*>(lv_event_get_user_data(event));
    if (screen) {
        screen->reveal_toolbar();
        screen->set_mode(Mode::Water);
    }
}

void SimulationScreen::fire_button_cb(lv_event_t* event) {
    auto* screen = static_cast<SimulationScreen*>(lv_event_get_user_data(event));
    if (screen) {
        screen->reveal_toolbar();
        screen->set_mode(Mode::Fire);
    }
}

void SimulationScreen::color_button_cb(lv_event_t* event) {
    auto* screen = static_cast<SimulationScreen*>(lv_event_get_user_data(event));
    if (screen) {
        screen->reveal_toolbar();
        screen->cycle_palette();
    }
}

void SimulationScreen::reset_button_cb(lv_event_t* event) {
    auto* screen = static_cast<SimulationScreen*>(lv_event_get_user_data(event));
    if (screen) {
        screen->reveal_toolbar();
        screen->reset_simulation();
    }
}

} // namespace app
