#include "Trace.h"

#include <array>

// {BD0DF8B0-F425-4279-83EF-9BB651EACA43}
TRACELOGGING_DEFINE_PROVIDER(g_hTraceProvider,
                             "HIDThermal",
                             (0xbd0df8b0, 0xf425, 0x4279, 0x83, 0xef, 0x9b, 0xb6, 0x51, 0xea, 0xca, 0x43));

namespace
{
struct WilLoggingRegistration
{
    WilLoggingRegistration() noexcept
    {
        wil::SetResultLoggingCallback(WilLoggingRegistration::LogCallback);
    }

    static void
    LogCallback(const wil::FailureInfo &failure) noexcept
    {
        std::array<wchar_t, 1024> buffer{};
        if (SUCCEEDED(wil::GetFailureLogString(buffer.data(), buffer.size(), failure)))
        {
            TraceLoggingWrite(g_hTraceProvider,
                              "OperationFailed",
                              TraceLoggingLevel(WINEVENT_LEVEL_ERROR),
                              TraceLoggingValue(buffer.data(), "ErrorMessage"));
        }
        else
        {
            TraceLoggingWrite(g_hTraceProvider,
                              "OperationFailedDetailsUnavailable",
                              TraceLoggingLevel(WINEVENT_LEVEL_ERROR),
                              TraceLoggingValue(buffer.data(), "ErrorMessage"));
        }
    }
};

WilLoggingRegistration s_registration{};
} // namespace