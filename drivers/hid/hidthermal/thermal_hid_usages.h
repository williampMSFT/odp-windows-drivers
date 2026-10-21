#pragma once

#include <hidusage.h>

namespace thermal_hid
{
constexpr USAGE usage_page = 0xFFED;

namespace usage
{
constexpr USAGE thermal_management_device = 0x0001;
constexpr USAGE fan_speed = 0x0020;
constexpr USAGE sensor_temperature = 0x0021;
} // namespace usage
} // namespace thermal_hid
