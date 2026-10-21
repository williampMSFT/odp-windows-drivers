#pragma once

#include <windows.h>
#include <winioctl.h>

// {F2436298-9E1D-4B8B-A28D-CDE19A43D732}
inline constexpr GUID GUID_DEVINTERFACE_HIDTHERMAL = {0xf2436298, 0x9e1d, 0x4b8b, {0xa2, 0x8d, 0xcd, 0xe1, 0x9a, 0x43, 0xd7, 0x32}};

inline constexpr DWORD IOCTL_HIDTHERMAL_GET_FAN_SPEED = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA);
inline constexpr DWORD IOCTL_HIDTHERMAL_SET_FAN_SPEED = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_DATA);
inline constexpr DWORD IOCTL_HIDTHERMAL_GET_TEMPERATURE = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_DATA);

// Reading from a temperature sensor
struct HidThermalReading
{
    ULONG value;
};

// Set requests take this HID logical value and return no output.
struct HidThermalFanSpeed
{
    ULONG value;
};

static_assert(sizeof(HidThermalReading) == 4);
static_assert(sizeof(HidThermalFanSpeed) == 4);
