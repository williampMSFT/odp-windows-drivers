#pragma once

#include <windows.h>

#include <wdf.h>

#include <hidusage.h>

#include <hidclass.h>
#include <hidpi.h>

#include <wil/resource.h>

struct HidPreparsedData
{

  public:
    HidPreparsedData() = default;
    HidPreparsedData(const HidPreparsedData &) = delete;
    HidPreparsedData &
    operator=(const HidPreparsedData &) = delete;

    HidPreparsedData(HidPreparsedData &&) = default;

    HidPreparsedData &
    operator=(HidPreparsedData &&) = default;

    NTSTATUS
    load_from_device(WDFIOTARGET hid_target, LONGLONG timeout) noexcept;

    PHIDP_PREPARSED_DATA
    data() const noexcept
    {
        return m_preparsed_data;
    }

    const HIDP_CAPS &
    caps() const noexcept
    {
        return m_caps;
    }

    explicit
    operator bool() const noexcept
    {
        return static_cast<bool>(m_buffer);
    }

  private:
    wil::unique_wdf_memory m_buffer{};
    PHIDP_PREPARSED_DATA m_preparsed_data{};
    HIDP_CAPS m_caps{};
};