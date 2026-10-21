#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <cfgmgr32.h>

#include <algorithm>
#include <format>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include <wil/resource.h>

#include "..\..\drivers\hid\hidthermal\hidthermal_ioctl.h"

// For variant visitation
template <typename... T> struct overloaded : T...
{
    using T::operator()...;
};

std::string
to_string(const wchar_t *wstr)
{
    std::string result;
    result.resize(std::wcslen(wstr));
    std::transform(wstr, wstr + std::wcslen(wstr), result.begin(), [](wchar_t c) { return static_cast<char>(c); });
    return result;
}

void
usage()
{
    std::wcout << L"Usage: hidthermalctl [--device INDEX] COMMAND [VALUE]\n"
                  L"  list                  List available devices and their indices\n"
                  L"  get-fan               Get the current fan speed\n"
                  L"  set-fan VALUE         Set the fan speed\n"
                  L"  get-temperature       Get the current temperature\n"
                  L"Run from an elevated terminal. Use --device when multiple devices are present.\n";
}

ULONG
parse_unsigned(const wchar_t *text)
{
    ULONG result;
    std::wstringstream wss;
    wss << text;
    wss >> result;

    if (wss.fail() || !wss.eof())
    {
        throw std::runtime_error("Could not parse argument as ULONG");
    }
    return result;
}

namespace Operation
{
enum class DeviceType
{
    Fan,
    Temperature
};

struct Help
{
};
struct List
{
};
struct Get
{
    int device_index = 0;
    DeviceType type;
};
struct Set
{
    int device_index = 0;
    ULONG value;
};

enum class Opcode
{
    Help,
    List,
    GetFan,
    SetFan,
    GetTemperature,
};
} // namespace Operation

using Command = std::variant<Operation::Help, Operation::List, Operation::Get, Operation::Set>;
Command
parse_command(int argc, wchar_t **argv)
{

    std::optional<Operation::Opcode> operation = std::nullopt;
    int device_index = 0;
    std::optional<ULONG> set_value = std::nullopt;

    int i = 1;
    auto take_int = [&](const char *operation_name) {
        if (i + 1 >= argc)
        {
            throw std::runtime_error(std::format("Expected an integer argument for {}", operation_name));
        }
        i++;
        return parse_unsigned(argv[i]);
    };
    auto update_operation = [&](Operation::Opcode op) {
        if (operation.has_value())
        {
            throw std::runtime_error("Multiple commands specified.");
        }
        operation = op;
    };

    for (; i < argc; i++)
    {
        if ((!std::wcscmp(argv[i], L"--help")) || (!std::wcscmp(argv[i], L"-h")) || (!std::wcscmp(argv[i], L"-?")))
        {
            update_operation(Operation::Opcode::Help);
        }
        else if (!std::wcscmp(argv[i], L"--device"))
        {
            device_index = take_int("device");
        }
        else if (!std::wcscmp(argv[i], L"list"))
        {
            update_operation(Operation::Opcode::List);
        }
        else if (!std::wcscmp(argv[i], L"get-fan"))
        {
            update_operation(Operation::Opcode::GetFan);
        }
        else if (!std::wcscmp(argv[i], L"set-fan"))
        {
            update_operation(Operation::Opcode::SetFan);
            set_value = take_int("set-fan");
        }
        else if (!std::wcscmp(argv[i], L"get-temperature"))
        {
            update_operation(Operation::Opcode::GetTemperature);
        }
        else
        {
            throw std::runtime_error(std::format("Unrecognized argument: {}", to_string(argv[i])));
        }
    }

    switch (operation.value_or(Operation::Opcode::Help))
    {
    case Operation::Opcode::Help:
        return Operation::Help{};
    case Operation::Opcode::List:
        return Operation::List{};
    case Operation::Opcode::GetFan:
        return Operation::Get{device_index, Operation::DeviceType::Fan};
    case Operation::Opcode::SetFan:
        return Operation::Set{device_index, set_value.value()};
    case Operation::Opcode::GetTemperature:
        return Operation::Get{device_index, Operation::DeviceType::Temperature};
    default:
        throw std::runtime_error("Unknown command");
    }
}

std::vector<std::wstring>
find_devices()
{
    std::vector<std::wstring> devices;
    GUID interface_guid = GUID_DEVINTERFACE_HIDTHERMAL;
    for (;;)
    {
        ULONG length = 0;
        CONFIGRET result =
            CM_Get_Device_Interface_List_SizeW(&length, &interface_guid, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if (result != CR_SUCCESS)
        {
            THROW_WIN32(CM_MapCrToWin32Err(result, ERROR_GEN_FAILURE));
        }

        std::vector<wchar_t> buffer(length);
        result =
            CM_Get_Device_Interface_ListW(&interface_guid, nullptr, buffer.data(), length, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if (result == CR_BUFFER_SMALL)
        {
            continue;
        }
        if (result != CR_SUCCESS)
        {
            THROW_WIN32(CM_MapCrToWin32Err(result, ERROR_GEN_FAILURE));
        }

        for (const wchar_t *path = buffer.data(); *path != L'\0'; path += std::wcslen(path) + 1)
        {
            devices.emplace_back(path);
        }

        return devices;
    }
}

wil::unique_hfile
open_device(const std::wstring &device_path, bool write)
{
    wil::unique_hfile device{CreateFileW(device_path.c_str(),
                                         write ? GENERIC_WRITE : GENERIC_READ,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         nullptr,
                                         OPEN_EXISTING,
                                         FILE_ATTRIBUTE_NORMAL,
                                         nullptr)};
    if (!device)
    {
        throw std::runtime_error(std::format("Opening device failed with error (try running as admin) {}", GetLastError()));
    }
    return device;
}

int
wmain(int argc, wchar_t **argv)
try
{
    Command cmd = parse_command(argc, argv);

    std::vector<std::wstring> devices = find_devices();
    if (devices.empty())
    {
        throw std::runtime_error("No HID thermal devices found");
    }

    std::visit(
        overloaded{[&](Operation::List) {
                       for (const auto &device : devices)
                       {
                           std::wcout << device << L"\n";
                       }
                   },
                   [&](Operation::Get get) {
                       wil::unique_hfile device = open_device(devices[get.device_index], false);
                       HidThermalReading reading{};
                       DWORD bytes_returned = 0;
                       const DWORD ioctl = [&]() {
                           switch (get.type)
                           {
                           case Operation::DeviceType::Fan:
                               return IOCTL_HIDTHERMAL_GET_FAN_SPEED;
                           case Operation::DeviceType::Temperature:
                               return IOCTL_HIDTHERMAL_GET_TEMPERATURE;
                           default:
                               throw std::runtime_error("Unrecognized device type");
                           }
                       }();
                       THROW_IF_WIN32_BOOL_FALSE(
                           DeviceIoControl(device.get(), ioctl, nullptr, 0, &reading, sizeof(reading), &bytes_returned, nullptr));
                       if (bytes_returned != sizeof(reading))
                       {
                           throw std::runtime_error(std::format("Unexpected driver response size: {}", bytes_returned));
                       }

                       switch (get.type)
                       {
                       case Operation::DeviceType::Fan:
                           std::wcout << std::format(L"Fan speed: {}\n", reading.value);
                           break;
                       case Operation::DeviceType::Temperature:
                           std::wcout << std::format(L"Temperature: {}\n", reading.value);
                           break;
                       }
                   },
                   [&](Operation::Set set) {
                       wil::unique_hfile device = open_device(devices[set.device_index], true);
                       THROW_IF_WIN32_BOOL_FALSE(DeviceIoControl(device.get(),
                                                                 IOCTL_HIDTHERMAL_SET_FAN_SPEED,
                                                                 &set.value,
                                                                 sizeof(set.value),
                                                                 nullptr,
                                                                 0,
                                                                 nullptr,
                                                                 nullptr));
                       std::wcout << std::format(L"Fan speed set to: {}\n", set.value);
                   },
                   [&](Operation::Help) { usage(); }},
        cmd);

    return 0;
}
catch (const std::exception &ex)
{
    std::cerr << "Failed: " << ex.what() << "\n";
    return -1;
}
