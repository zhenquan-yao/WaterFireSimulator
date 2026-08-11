#pragma once

#include "imu_service.h"

#include <cstdint>
#include <string>

namespace app {

struct GravityVector {
    float x{0.0f};
    float y{-1.0f};
};

class GravityInput {
public:
    GravityInput();

    void update();
    void set_virtual_key(uint32_t key, bool pressed);
    GravityVector value() const;
    bool sensor_available() const;
    const std::string& status() const;

private:
    void update_virtual();

    platform::imu::Device device_{};
    GravityVector gravity_{};
    GravityVector filtered_{};
    std::string status_{};
    bool sensor_available_{false};
    bool left_{false};
    bool right_{false};
    bool up_{false};
    bool down_{false};
};

} // namespace app
