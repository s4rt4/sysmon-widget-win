#include "temperature_poller.h"

#include <chrono>

#include <comdef.h>
#include <wbemidl.h>
#include <windows.h>

namespace {
constexpr auto kSampleInterval = std::chrono::seconds(10);

// Run a WQL query that returns a single int (long) property. Returns true
// on success and writes the value out.
bool QueryWmiLong(const wchar_t* ns, const wchar_t* query, const wchar_t* property,
                  long* out) {
    if (!ns || !query || !property || !out) return false;

    // Init security once per thread (idempotent — RPC_E_TOO_LATE is fine).
    const HRESULT sec = CoInitializeSecurity(
        nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    if (FAILED(sec) && sec != RPC_E_TOO_LATE) {
        return false;
    }

    IWbemLocator* locator = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IWbemLocator, reinterpret_cast<void**>(&locator)))) {
        return false;
    }

    BSTR ns_bstr = SysAllocString(ns);
    IWbemServices* services = nullptr;
    HRESULT hr = locator->ConnectServer(ns_bstr, nullptr, nullptr, nullptr, 0,
                                        nullptr, nullptr, &services);
    SysFreeString(ns_bstr);
    locator->Release();
    if (FAILED(hr)) return false;

    CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                      RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                      nullptr, EOAC_NONE);

    BSTR lang = SysAllocString(L"WQL");
    BSTR q    = SysAllocString(query);
    IEnumWbemClassObject* en = nullptr;
    hr = services->ExecQuery(lang, q,
                             WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                             nullptr, &en);
    SysFreeString(q);
    SysFreeString(lang);
    services->Release();
    if (FAILED(hr)) return false;

    bool found = false;
    IWbemClassObject* obj = nullptr;
    ULONG ret = 0;
    if (SUCCEEDED(en->Next(1000, 1, &obj, &ret)) && ret > 0) {
        VARIANT v{};
        VariantInit(&v);
        if (SUCCEEDED(obj->Get(property, 0, &v, nullptr, nullptr))) {
            if (v.vt == VT_I4 || v.vt == VT_INT) {
                *out = v.lVal; found = true;
            } else if (v.vt == VT_UI4 || v.vt == VT_UINT) {
                *out = static_cast<long>(v.ulVal); found = true;
            }
        }
        VariantClear(&v);
        obj->Release();
    }
    en->Release();
    return found;
}

bool ReadTemperatureC(float* out) {
    long t = 0;
    // Try PerfFormattedData first (faster and reasonably standard).
    if (QueryWmiLong(L"ROOT\\CIMV2",
                     L"SELECT Temperature FROM Win32_PerfFormattedData_Counters_ThermalZoneInformation",
                     L"Temperature", &t) && t > 273 && t < 423) {
        *out = static_cast<float>(t - 273);
        return true;
    }
    // Fall back to MSAcpi_ThermalZoneTemperature (10 K units).
    if (QueryWmiLong(L"ROOT\\WMI",
                     L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature",
                     L"CurrentTemperature", &t) && t > 0) {
        const float c = static_cast<float>(t) / 10.0f - 273.15f;
        if (c > -50.0f && c < 150.0f) {
            *out = c;
            return true;
        }
    }
    return false;
}
}  // namespace

TemperaturePoller::TemperaturePoller() {
    worker_ = std::thread([this] { WorkerLoop(); });
}

TemperaturePoller::~TemperaturePoller() {
    stop_.store(true);
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void TemperaturePoller::WorkerLoop() {
    // RPC_E_CHANGED_MODE means another apartment was already initialised
    // on this thread — skip Uninit to avoid corrupting the refcount.
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool needs_uninit = SUCCEEDED(hr) || hr == S_FALSE;

    try {
        while (!stop_.load()) {
            float t = 0.0f;
            if (ReadTemperatureC(&t)) {
                value_.store(t);
                has_value_.store(true);
            } else {
                has_value_.store(false);
            }

            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait_for(lock, kSampleInterval, [this] { return stop_.load(); });
        }
    } catch (...) {
        // Worker exceptions would otherwise std::terminate the process.
    }

    if (needs_uninit) CoUninitialize();
}

bool TemperaturePoller::TryGet(float* out) const {
    if (!has_value_.load()) return false;
    *out = value_.load();
    return true;
}
