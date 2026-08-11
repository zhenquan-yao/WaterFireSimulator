#include "gravity_input.h"

#include "lvgl.h"

#include <algorithm>
#include <cmath>

namespace app {
namespace {

constexpr float kQuietFilterAlpha = 0.20f;
constexpr float kMotionFilterAlpha = 0.48f;
constexpr float kFastResponseDelta = 0.075f;
constexpr float kInputGain = 1.40f;
constexpr float kGravity = 9.80665f;
constexpr float kClamp = 1.5f;
constexpr float kZeroDeadZone = 0.015f;
constexpr float kCardinalSnap = 0.018f;
constexpr float kOutputHysteresis = 0.006f;

float clamp_axis(float value) {
    return std::clamp(value, -kClamp, kClamp);
}

float stabilize_axis(float value) {
    if (std::abs(value) < kZeroDeadZone) return 0.0f;
    if (std::abs(value - 1.0f) < kCardinalSnap) return 1.0f;
    if (std::abs(value + 1.0f) < kCardinalSnap) return -1.0f;
    return value;
}

void update_axis(float target, float& filtered, float& output) {
    const float delta = target - filtered;
    const float alpha = std::abs(delta) >= kFastResponseDelta
                            ? kMotionFilterAlpha
                            : kQuietFilterAlpha;
    filtered += alpha * delta;
    if (std::abs(filtered - output) >= kOutputHysteresis) {
        output = stabilize_axis(filtered);
    }
}

} // namespace

GravityInput::GravityInput() {
#if USE_DESKTOP
    status_ = "KEYS";
    update_virtual();
#else
    sensor_available_ = platform::imu::find_bmi270_device(device_, status_);
    if (sensor_available_) {
        status_ = "IMU";
        filtered_ = gravity_;
    }
#endif
}

void GravityInput::update() {
#if USE_DESKTOP
    update_virtual();
#else
    if (!sensor_available_) {
        return;
    }

    platform::imu::Acceleration reading;
    std::string error;
    if (!platform::imu::read_acceleration(device_, reading, error)) {
        status_ = "IMU!";
        return;
    }

    float ax = static_cast<float>(reading.x);
    float ay = static_cast<float>(reading.y);
    float az = static_cast<float>(reading.z);
    const float magnitude = std::sqrt(ax * ax + ay * ay + az * az);
    if (magnitude > 5.0f) {
        ax /= kGravity;
        ay /= kGravity;
    }

    // BMI270 axes are rotated 90 degrees relative to the landscape display.
    // Rotate sensor gravity clockwise into the simulation's screen space.
    const float target_x = stabilize_axis(clamp_axis(-ay * kInputGain));
    const float target_y = stabilize_axis(clamp_axis(ax * kInputGain));
    update_axis(target_x, filtered_.x, gravity_.x);
    update_axis(target_y, filtered_.y, gravity_.y);
    status_ = "IMU";
#endif
}

void GravityInput::set_virtual_key(uint32_t key, bool pressed) {
    if (key == LV_KEY_LEFT) {
        left_ = pressed;
    } else if (key == LV_KEY_RIGHT) {
        right_ = pressed;
    } else if (key == LV_KEY_UP) {
        up_ = pressed;
    } else if (key == LV_KEY_DOWN) {
        down_ = pressed;
    }
#if USE_DESKTOP
    update_virtual();
#endif
}

GravityVector GravityInput::value() const {
    return gravity_;
}

bool GravityInput::sensor_available() const {
    return sensor_available_;
}

const std::string& GravityInput::status() const {
    return status_;
}

void GravityInput::update_virtual() {
    const float target_x = static_cast<float>(right_) - static_cast<float>(left_);
    float target_y = static_cast<float>(up_) - static_cast<float>(down_);
    if (!up_ && !down_) {
        target_y = -1.0f;
    }
    gravity_.x = target_x;
    gravity_.y = target_y;
}

} // namespace app
