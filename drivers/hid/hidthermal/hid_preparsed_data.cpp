#include "hid_preparsed_data.h"

#include <Trace.h>

#include <wil/result.h>

#include "hidthermal_mem.h"

NTSTATUS
HidPreparsedData::load_from_device(WDFIOTARGET hid_target, LONGLONG timeout) noexcept
{
    WDF_REQUEST_SEND_OPTIONS options;
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, timeout);

    HID_COLLECTION_INFORMATION collection_information{};
    WDF_MEMORY_DESCRIPTOR descriptor;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&descriptor, &collection_information, sizeof(collection_information));

    ULONG_PTR bytes_returned = 0;
    RETURN_IF_NTSTATUS_FAILED(WdfIoTargetSendIoctlSynchronously(hid_target,
                                                                nullptr,
                                                                IOCTL_HID_GET_COLLECTION_INFORMATION,
                                                                nullptr,
                                                                &descriptor,
                                                                &options,
                                                                &bytes_returned));

    if ((bytes_returned < sizeof(collection_information)) || (collection_information.DescriptorSize == 0))
    {
        RETURN_NTSTATUS(STATUS_DEVICE_DATA_ERROR);
    }

    RETURN_IF_NTSTATUS_FAILED(WdfMemoryCreate(WDF_NO_OBJECT_ATTRIBUTES,
                                              NonPagedPoolNx,
                                              pool_tag,
                                              collection_information.DescriptorSize,
                                              m_buffer.put(),
                                              reinterpret_cast<void **>(&m_preparsed_data)));
    std::memset(m_preparsed_data, 0, collection_information.DescriptorSize);

    WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&descriptor, m_buffer.get(), nullptr);
    RETURN_IF_NTSTATUS_FAILED(WdfIoTargetSendIoctlSynchronously(hid_target,
                                                                nullptr,
                                                                IOCTL_HID_GET_COLLECTION_DESCRIPTOR,
                                                                nullptr,
                                                                &descriptor,
                                                                &options,
                                                                &bytes_returned));

    if (bytes_returned != collection_information.DescriptorSize)
    {
        RETURN_NTSTATUS(STATUS_DEVICE_DATA_ERROR);
    }

    RETURN_IF_NTSTATUS_FAILED(HidP_GetCaps(m_preparsed_data, &m_caps));

    return STATUS_SUCCESS;
}