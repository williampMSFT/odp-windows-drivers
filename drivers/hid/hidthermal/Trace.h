#pragma once

#include <windows.h>
#include <wdf.h>

#include <TraceLoggingProvider.h>
#include <winmeta.h>

#include <wil/result.h>

TRACELOGGING_DECLARE_PROVIDER(g_hTraceProvider);

#define CATCH_RETURN_NTSTATUS()                                                                                                    \
    catch (std::bad_alloc &)                                                                                                       \
    {                                                                                                                              \
        RETURN_NTSTATUS(STATUS_INSUFFICIENT_RESOURCES);                                                                            \
    }                                                                                                                              \
    catch (...)                                                                                                                    \
    {                                                                                                                              \
        RETURN_NTSTATUS(STATUS_UNSUCCESSFUL);                                                                                      \
    }
