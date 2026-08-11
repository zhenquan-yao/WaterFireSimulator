/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>

namespace platform::imu {

struct Device {
    std::string iio_path;
    std::string display_name;
};

struct Acceleration {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

bool find_bmi270_device(Device& device, std::string& error_message);
bool read_acceleration(const Device& device, Acceleration& reading, std::string& error_message);

} // namespace platform::imu
