/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "imu_service.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace platform::imu {
namespace {

constexpr const char* kIioRoot = "/sys/bus/iio/devices";

std::string trim(std::string value) {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) {
        return !is_space(static_cast<unsigned char>(ch));
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) {
        return !is_space(static_cast<unsigned char>(ch));
    }).base(), value.end());
    return value;
}

bool read_text(const std::filesystem::path& path, std::string& value) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    value = trim(buffer.str());
    return true;
}

bool read_number(const std::filesystem::path& path, double& value) {
    std::string text;
    if (!read_text(path, text)) {
        return false;
    }
    try {
        value = std::stod(text);
        return true;
    } catch (...) {
        return false;
    }
}

bool contains_bmi270(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return name.find("bmi270") != std::string::npos ||
           name.find("bosch,bmi270") != std::string::npos;
}

bool read_axis(const std::filesystem::path& root, const char* file, double scale, double& value) {
    double raw = 0.0;
    if (!read_number(root / file, raw)) {
        return false;
    }
    value = raw * scale;
    return true;
}

} // namespace

bool find_bmi270_device(Device& device, std::string& error_message) {
    const std::filesystem::path root(kIioRoot);
    std::error_code error;
    if (!std::filesystem::exists(root, error)) {
        error_message = "Linux IIO is unavailable";
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (error) {
            break;
        }
        std::string name;
        if (!read_text(entry.path() / "name", name) || !contains_bmi270(name)) {
            continue;
        }
        device.iio_path = entry.path().string();
        device.display_name = name.empty() ? "bmi270" : name;
        return true;
    }

    error_message = "BMI270 IIO device not found";
    return false;
}

bool read_acceleration(const Device& device, Acceleration& reading, std::string& error_message) {
    const std::filesystem::path root(device.iio_path);
    double scale = 0.0;
    if (!read_number(root / "in_accel_scale", scale)) {
        error_message = "Failed to read BMI270 acceleration scale";
        return false;
    }
    if (!read_axis(root, "in_accel_x_raw", scale, reading.x) ||
        !read_axis(root, "in_accel_y_raw", scale, reading.y) ||
        !read_axis(root, "in_accel_z_raw", scale, reading.z)) {
        error_message = "Failed to read BMI270 acceleration";
        return false;
    }
    return true;
}

} // namespace platform::imu
