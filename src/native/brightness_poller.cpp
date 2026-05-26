#include "brightness_poller.h"

#include <chrono>

#include <comdef.h>
#include <wbemidl.h>
#include <windows.h>

namespace {
constexpr auto kSampleInterval = std::chrono::seconds(5);

// Query WmiMonitorBrightness.CurrentBrightness (BYTE 0–100). Returns true
// on success — false on hardware that doesn't expose the class (most
// desktop monitors without DDC/CI integration).
bool ReadBrightness(int* out) {
    if (!out) return false;

    const HRESULT sec = CoInitializeSecurity(
        nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    if (FAILED(sec) && sec != RPC_E_TOO_LATE) return false;

    IWbemLocator* locator = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IWbemLocator, reinterpret_cast<void**>(&locator)))) {
        return false;
    }

    BSTR ns = SysAllocString(L"ROOT\\WMI");
    IWbemServices* services = nullptr;
    HRESULT hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0,
                                        nullptr, nullptr, &services);
    SysFreeString(ns);
    locator->Release();
    if (FAILED(hr)) return false;

    CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                      RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                      nullptr, EOAC_NONE);

    BSTR lang = SysAllocString(L"WQL");
    BSTR q    = SysAllocString(L"SELECT CurrentBrightness FROM WmiMonitorBrightness");
    IEnumWbemClassObject* en = nullptr;
    hr = services->ExecQuery(lang, q,
                             WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                             nullptr, &en);
    SysFreeString(q);
    SysFreeString(lang);
    services->Release();
    if (FAILED(hr) || !en) return false;

    bool found = false;
    IWbemClassObject* obj = nullptr;
    ULONG ret = 0;
    if (SUCCEEDED(en->Next(1000, 1, &obj, &ret)) && ret > 0) {
        VARIANT v{};
        VariantInit(&v);
        if (SUCCEEDED(obj->Get(L"CurrentBrightness", 0, &v, nullptr, nullptr))) {
            if (v.vt == VT_UI1) {
                *out = static_cast<int>(v.bVal);
                found = true;
            } else if (v.vt == VT_I4 || v.vt == VT_INT) {
                *out = static_cast<int>(v.lVal);
                found = true;
            } else if (v.vt == VT_UI4 || v.vt == VT_UINT) {
                *out = static_cast<int>(v.ulVal);
                found = true;
            }
        }
        VariantClear(&v);
        obj->Release();
    }
    en->Release();
    return found;
}
}  // namespace

BrightnessPoller::BrightnessPoller() {
    worker_ = std::thread([this] { WorkerLoop(); });
}

BrightnessPoller::~BrightnessPoller() {
    stop_.store(true);
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void BrightnessPoller::WorkerLoop() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool needs_uninit = SUCCEEDED(hr) || hr == S_FALSE;

    try {
        while (!stop_.load()) {
            int b = 0;
            if (ReadBrightness(&b)) {
                value_.store(b);
                has_value_.store(true);
            } else {
                has_value_.store(false);
            }

            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait_for(lock, kSampleInterval, [this] { return stop_.load(); });
        }
    } catch (...) {
    }

    if (needs_uninit) CoUninitialize();
}

bool BrightnessPoller::TryGet(int* out) const {
    if (!has_value_.load() || !out) return false;
    *out = value_.load();
    return true;
}
