#pragma once

#include <windows.h>
#include <wdf.h>

#include <memory>
#include <mutex>
#include <optional>

#include <hidusage.h>
#include <hidclass.h>
#include <hidpi.h>

#include <wil/resource.h>
#include "hid_preparsed_data.h"

class DeviceContext final
{
  public:
    static NTSTATUS
    create(WDFDRIVER driver, PWDFDEVICE_INIT device_init);

  private:
    enum class InputUsage
    {
        FanSpeed,
        SensorTemperature,
    };

    struct HardwareState
    {
        wil::unique_wdf_any<WDFIOTARGET> hid_target{};

        HidPreparsedData preparsed_data{};

        HIDP_VALUE_CAPS fan_speed_output_caps{};
        HIDP_VALUE_CAPS fan_speed_input_caps{};
        HIDP_VALUE_CAPS sensor_temperature_input_caps{};
    };

    static EVT_WDF_OBJECT_CONTEXT_DESTROY on_destroy;
    static EVT_WDF_DEVICE_PREPARE_HARDWARE on_prepare_hardware;
    static EVT_WDF_DEVICE_RELEASE_HARDWARE on_release_hardware;
    static EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL on_ioctl;

    // Verifies that the driver has a fan speed output report.
    NTSTATUS
    verify_output_report(const HIDP_VALUE_CAPS &target_caps) noexcept;

    // Verifies that the input report is a valid fan speed or sensor temperature report.
    NTSTATUS
    verify_input_report(const HIDP_VALUE_CAPS &target_caps) noexcept;

    NTSTATUS
    set_fan_speed(ULONG value) noexcept;

    NTSTATUS
    get_report_value(InputUsage input_usage, ULONG *value) noexcept;

  private:
    std::mutex m_device_lock;
    std::optional<HardwareState> m_hardware;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DeviceContext, GetDeviceContext)
