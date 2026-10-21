#include "device.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

#include <wil/result_macros.h>

#include "Trace.h"
#include "hidthermal_ioctl.h"
#include "hidthermal_mem.h"
#include "thermal_hid_usages.h"

namespace
{

constexpr LONGLONG
relative_timeout_in_seconds(ULONGLONG seconds)
{
    return seconds * -1 * WDF_TIMEOUT_TO_SEC;
}

constexpr LONGLONG hid_operation_timeout = relative_timeout_in_seconds(5);

NTSTATUS
find_value_caps(PHIDP_PREPARSED_DATA preparsed_data,
                HIDP_REPORT_TYPE report_type,
                USAGE_AND_PAGE usage,
                HIDP_VALUE_CAPS *value_caps) noexcept
{
    if (value_caps == nullptr)
    {
        RETURN_NTSTATUS(STATUS_INVALID_PARAMETER);
    }

    USHORT count = 1;
    HIDP_VALUE_CAPS cap{};
    RETURN_IF_NTSTATUS_FAILED(HidP_GetSpecificValueCaps(
        report_type, usage.UsagePage, HIDP_LINK_COLLECTION_UNSPECIFIED, usage.Usage, &cap, &count, preparsed_data));

    *value_caps = cap;

    return STATUS_SUCCESS;
}

} // namespace

_Use_decl_annotations_ NTSTATUS
DeviceContext::create(WDFDRIVER driver, PWDFDEVICE_INIT device_init)
{
    UNREFERENCED_PARAMETER(driver);

    WdfDeviceInitSetPowerPolicyOwnership(device_init, FALSE);

    WDF_PNPPOWER_EVENT_CALLBACKS pnp_callbacks;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp_callbacks);
    pnp_callbacks.EvtDevicePrepareHardware = DeviceContext::on_prepare_hardware;
    pnp_callbacks.EvtDeviceReleaseHardware = DeviceContext::on_release_hardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(device_init, &pnp_callbacks);

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DeviceContext);
    attributes.EvtDestroyCallback = DeviceContext::on_destroy;

    WDFDEVICE device;
    RETURN_IF_NTSTATUS_FAILED(WdfDeviceCreate(&device_init, &attributes, &device));

    auto *context = new (GetDeviceContext(device)) DeviceContext{};
    (void)context;

    WDF_IO_QUEUE_CONFIG queue_config;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue_config, WdfIoQueueDispatchParallel);
    queue_config.EvtIoDeviceControl = DeviceContext::on_ioctl;
    queue_config.PowerManaged = WdfFalse;

    WDF_OBJECT_ATTRIBUTES queue_attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&queue_attributes);
    queue_attributes.ExecutionLevel = WdfExecutionLevelPassive;

    RETURN_IF_NTSTATUS_FAILED(WdfIoQueueCreate(device, &queue_config, &queue_attributes, WDF_NO_HANDLE));
    RETURN_IF_NTSTATUS_FAILED(WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_HIDTHERMAL, nullptr));

    return STATUS_SUCCESS;
}

_Use_decl_annotations_ VOID
DeviceContext::on_destroy(WDFOBJECT object)
{
    GetDeviceContext(object)->~DeviceContext();
}

_Use_decl_annotations_ VOID
DeviceContext::on_ioctl(
    WDFQUEUE queue, WDFREQUEST request, size_t output_buffer_length, size_t input_buffer_length, ULONG ioctl_code)
{
    auto *context = GetDeviceContext(WdfIoQueueGetDevice(queue));
    ULONG_PTR information = 0;

    const NTSTATUS status = [&]() -> NTSTATUS {
        switch (ioctl_code)
        {
        case IOCTL_HIDTHERMAL_SET_FAN_SPEED: {
            if (input_buffer_length != sizeof(HidThermalFanSpeed))
            {
                RETURN_NTSTATUS(STATUS_BUFFER_TOO_SMALL);
            }

            if (output_buffer_length != 0)
            {
                RETURN_NTSTATUS(STATUS_INVALID_PARAMETER);
            }

            HidThermalFanSpeed *input = nullptr;
            RETURN_IF_NTSTATUS_FAILED(
                WdfRequestRetrieveInputBuffer(request, sizeof(*input), reinterpret_cast<void **>(&input), nullptr));
            RETURN_IF_NTSTATUS_FAILED(context->set_fan_speed(input->value));

            return STATUS_SUCCESS;
        }
        case IOCTL_HIDTHERMAL_GET_FAN_SPEED:
        case IOCTL_HIDTHERMAL_GET_TEMPERATURE: {
            if (input_buffer_length != 0)
            {
                RETURN_NTSTATUS(STATUS_INVALID_PARAMETER);
            }

            HidThermalReading *output = nullptr;
            RETURN_IF_NTSTATUS_FAILED(
                WdfRequestRetrieveOutputBuffer(request, sizeof(*output), reinterpret_cast<void **>(&output), nullptr));
            ULONG value = 0;
            const auto input_usage =
                (ioctl_code == IOCTL_HIDTHERMAL_GET_TEMPERATURE) ? InputUsage::SensorTemperature : InputUsage::FanSpeed;
            RETURN_IF_NTSTATUS_FAILED(context->get_report_value(input_usage, &value));
            output->value = value;
            information = sizeof(*output);

            return STATUS_SUCCESS;
        }
        default:
            RETURN_NTSTATUS(STATUS_INVALID_DEVICE_REQUEST);
        }
    }();

    if (!NT_SUCCESS(status))
    {
        TraceLoggingWrite(g_hTraceProvider,
                          "DeviceControlFailed",
                          TraceLoggingLevel(WINEVENT_LEVEL_ERROR),
                          TraceLoggingValue(ioctl_code, "IoControlCode"),
                          TraceLoggingNTStatus(status));
    }

    WdfRequestCompleteWithInformation(request, status, information);
}

_Use_decl_annotations_ NTSTATUS
DeviceContext::on_prepare_hardware(WDFDEVICE device, WDFCMRESLIST resources_raw, WDFCMRESLIST resources_translated)
try
{
    UNREFERENCED_PARAMETER(resources_raw);
    UNREFERENCED_PARAMETER(resources_translated);

    auto *context = GetDeviceContext(device);
    auto lock = std::scoped_lock(context->m_device_lock);
    HardwareState hardware;

    // Open handle to underlying HID device
    {
        RETURN_IF_NTSTATUS_FAILED(WdfIoTargetCreate(device, WDF_NO_OBJECT_ATTRIBUTES, hardware.hid_target.put()));

        WDF_IO_TARGET_OPEN_PARAMS open_params;
        WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_FILE(&open_params, nullptr);
        RETURN_IF_NTSTATUS_FAILED(WdfIoTargetOpen(hardware.hid_target.get(), &open_params));
    }

    // verify HID device has the capabilities we require
    RETURN_IF_NTSTATUS_FAILED(hardware.preparsed_data.load_from_device(hardware.hid_target.get(), hid_operation_timeout));

    {
        const HIDP_CAPS &caps = hardware.preparsed_data.caps();
        if ((caps.UsagePage != thermal_hid::usage_page) || (caps.Usage != thermal_hid::usage::thermal_management_device))
        {
            RETURN_NTSTATUS(STATUS_DEVICE_CONFIGURATION_ERROR);
        }
    }

    {
        PHIDP_PREPARSED_DATA preparsed_data = hardware.preparsed_data.data();
        RETURN_IF_NTSTATUS_FAILED(find_value_caps(preparsed_data,
                                                  HidP_Output,
                                                  {thermal_hid::usage::fan_speed, thermal_hid::usage_page},
                                                  &hardware.fan_speed_output_caps));

        RETURN_IF_NTSTATUS_FAILED(find_value_caps(
            preparsed_data, HidP_Input, {thermal_hid::usage::fan_speed, thermal_hid::usage_page}, &hardware.fan_speed_input_caps));

        RETURN_IF_NTSTATUS_FAILED(find_value_caps(preparsed_data,
                                                  HidP_Input,
                                                  {thermal_hid::usage::sensor_temperature, thermal_hid::usage_page},
                                                  &hardware.sensor_temperature_input_caps));
    }

    RETURN_IF_NTSTATUS_FAILED(context->verify_input_report(hardware.fan_speed_input_caps));
    RETURN_IF_NTSTATUS_FAILED(context->verify_input_report(hardware.sensor_temperature_input_caps));

    RETURN_IF_NTSTATUS_FAILED(context->verify_output_report(hardware.fan_speed_output_caps));

    context->m_hardware.emplace(std::move(hardware));

    return STATUS_SUCCESS;
}
CATCH_RETURN_NTSTATUS();

_Use_decl_annotations_ NTSTATUS
DeviceContext::on_release_hardware(WDFDEVICE device, WDFCMRESLIST resources_translated)
{
    UNREFERENCED_PARAMETER(resources_translated);

    auto *context = GetDeviceContext(device);
    auto lock = std::scoped_lock(context->m_device_lock);
    context->m_hardware.reset();

    return STATUS_SUCCESS;
}

NTSTATUS
DeviceContext::verify_output_report(const HIDP_VALUE_CAPS &target_caps) noexcept
try
{
    if (target_caps.LogicalMin < 0)
    {
        // HID devices advertised signedness by specifying a logicalmin that's < 0.
        // This driver doesn't support signed values (they don't make sense for fan speed or temperature (typically decikelvin)),
        // so we just reject devices that advertise signed values.
        RETURN_NTSTATUS(STATUS_DEVICE_CONFIGURATION_ERROR);
    }

    if (target_caps.IsRange)
    {
        RETURN_NTSTATUS(STATUS_DEVICE_CONFIGURATION_ERROR);
    }

    if (!target_caps.HasNull)
    {
        RETURN_NTSTATUS(STATUS_DEVICE_CONFIGURATION_ERROR);
    }

    return STATUS_SUCCESS;
}
CATCH_RETURN_NTSTATUS();

NTSTATUS
DeviceContext::verify_input_report(const HIDP_VALUE_CAPS &target_caps) noexcept try
{
    if (target_caps.LogicalMin < 0)
    {
        // HID devices advertised signedness by specifying a logicalmin that's < 0.
        // This driver doesn't support signed values (they don't make sense for fan speed or temperature (typically decikelvin)),
        // so we just reject devices that advertise signed values.
        RETURN_NTSTATUS(STATUS_DEVICE_CONFIGURATION_ERROR);
    }

    if (target_caps.IsRange)
    {
        RETURN_NTSTATUS(STATUS_DEVICE_CONFIGURATION_ERROR);
    }

    return STATUS_SUCCESS;
}
CATCH_RETURN_NTSTATUS();

NTSTATUS
DeviceContext::set_fan_speed(ULONG value) noexcept try
{
    auto lock = std::scoped_lock(m_device_lock);
    if (!m_hardware)
    {
        RETURN_NTSTATUS(STATUS_DEVICE_NOT_READY);
    }

    std::vector<CHAR> report_buffer(m_hardware->preparsed_data.caps().OutputReportByteLength);
    if (report_buffer.empty())
    {
        RETURN_NTSTATUS(STATUS_DEVICE_CONFIGURATION_ERROR);
    }

    const auto report_buffer_size = static_cast<ULONG>(report_buffer.size());

    // NOTE: there is a bug in HidP_InitializeReportForID that causes it to set incorrect default null values for
    //       nullable fields on devices that have both input and output reports.  If nullability is important for
    //       your driver, you may need to manually implement null values until HidP_InitializeReportForIdEx is
    //       published.  See the hidtime driver's null_value_for_field function for an example of this.
    const auto &caps = m_hardware->fan_speed_output_caps;
    RETURN_IF_NTSTATUS_FAILED(HidP_InitializeReportForID(
        HidP_Output, caps.ReportID, m_hardware->preparsed_data.data(), report_buffer.data(), report_buffer_size));

    RETURN_IF_NTSTATUS_FAILED(HidP_SetUsageValue(HidP_Output,
                                                 thermal_hid::usage_page,
                                                 caps.LinkCollection,
                                                 thermal_hid::usage::fan_speed,
                                                 value,
                                                 m_hardware->preparsed_data.data(),
                                                 report_buffer.data(),
                                                 report_buffer_size));

    WDF_MEMORY_DESCRIPTOR descriptor;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&descriptor, report_buffer.data(), report_buffer_size);

    WDF_REQUEST_SEND_OPTIONS options;
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, hid_operation_timeout);

    RETURN_IF_NTSTATUS_FAILED(
        WdfIoTargetSendWriteSynchronously(m_hardware->hid_target.get(), nullptr, &descriptor, nullptr, &options, nullptr));

    return STATUS_SUCCESS;
}
CATCH_RETURN_NTSTATUS();

NTSTATUS
DeviceContext::get_report_value(InputUsage input_usage, ULONG *value) noexcept try
{
    auto lock = std::scoped_lock(m_device_lock);
    if (!m_hardware)
    {
        RETURN_NTSTATUS(STATUS_DEVICE_NOT_READY);
    }

    const HIDP_VALUE_CAPS *caps = nullptr;
    USAGE usage = 0;
    switch (input_usage)
    {
    case InputUsage::FanSpeed:
        caps = &m_hardware->fan_speed_input_caps;
        usage = thermal_hid::usage::fan_speed;
        break;
    case InputUsage::SensorTemperature:
        caps = &m_hardware->sensor_temperature_input_caps;
        usage = thermal_hid::usage::sensor_temperature;
        break;
    }

    std::vector<CHAR> report_buffer(m_hardware->preparsed_data.caps().InputReportByteLength);
    if (report_buffer.empty())
    {
        RETURN_NTSTATUS(STATUS_DEVICE_CONFIGURATION_ERROR);
    }

    const auto report_buffer_size = static_cast<ULONG>(report_buffer.size());

    // NOTE: there is a bug in HidP_InitializeReportForID that causes it to set incorrect default null values for
    //       nullable fields on devices that have both input and output reports.  If nullability is important for
    //       your driver, you may need to manually implement null values until HidP_InitializeReportForIdEx is
    //       published.  See the hidtime driver's null_value_for_field function for an example of this.
    RETURN_IF_NTSTATUS_FAILED(HidP_InitializeReportForID(
        HidP_Input, caps->ReportID, m_hardware->preparsed_data.data(), report_buffer.data(), report_buffer_size));

    WDF_MEMORY_DESCRIPTOR descriptor;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&descriptor, report_buffer.data(), report_buffer_size);

    WDF_REQUEST_SEND_OPTIONS options;
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, hid_operation_timeout);

    ULONG_PTR bytes_returned = 0;
    RETURN_IF_NTSTATUS_FAILED(WdfIoTargetSendIoctlSynchronously(
        m_hardware->hid_target.get(), nullptr, IOCTL_HID_GET_INPUT_REPORT, &descriptor, &descriptor, &options, &bytes_returned));

    if (static_cast<UCHAR>(report_buffer[0]) != caps->ReportID)
    {
        RETURN_NTSTATUS(STATUS_DEVICE_DATA_ERROR);
    }

    RETURN_IF_NTSTATUS_FAILED(HidP_GetUsageValue(HidP_Input,
                                                 thermal_hid::usage_page,
                                                 caps->LinkCollection,
                                                 usage,
                                                 value,
                                                 m_hardware->preparsed_data.data(),
                                                 report_buffer.data(),
                                                 report_buffer_size));

    return STATUS_SUCCESS;
}
CATCH_RETURN_NTSTATUS();
