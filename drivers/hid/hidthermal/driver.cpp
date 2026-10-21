#include <windows.h>

#include <wdf.h>

#include "Trace.h"
#include "device.h"

extern "C" DRIVER_INITIALIZE DriverEntry;
EVT_WDF_OBJECT_CONTEXT_CLEANUP HidThermalEvtDriverContextCleanup;

_Use_decl_annotations_ VOID
HidThermalEvtDriverContextCleanup(WDFOBJECT driver)
{
    UNREFERENCED_PARAMETER(driver);
    TraceLoggingUnregister(g_hTraceProvider);
}

_Use_decl_annotations_ extern "C" NTSTATUS
DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = HidThermalEvtDriverContextCleanup;

    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, DeviceContext::create);

    TraceLoggingRegister(g_hTraceProvider);
    const NTSTATUS status = WdfDriverCreate(driver_object, registry_path, &attributes, &config, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status))
    {
        TraceLoggingUnregister(g_hTraceProvider);
    }

    return status;
}
