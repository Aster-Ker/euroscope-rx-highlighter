#include "MainThreadWaker.h"

namespace RxHighlight {

namespace {
constexpr wchar_t kClassName[] = L"RxHighlighterMainThreadWaker";
constexpr UINT kWakeMessage = WM_APP + 0x52; // private to our own window
constexpr UINT_PTR kTimerId = 1;

HINSTANCE ThisModule()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&ThisModule),
        &module);
    return module;
}
} // namespace

MainThreadWaker::MainThreadWaker(std::function<void()> callback, UINT intervalMs)
    : callback_(std::move(callback)), instance_(ThisModule())
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &MainThreadWaker::WndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc); // fails harmlessly if already registered

    HWND hwnd = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance_, nullptr);
    if (hwnd != nullptr) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        SetTimer(hwnd, kTimerId, intervalMs, nullptr);
        hwnd_ = hwnd;
    }
}

MainThreadWaker::~MainThreadWaker()
{
    HWND hwnd = hwnd_.exchange(nullptr);
    if (hwnd != nullptr) {
        KillTimer(hwnd, kTimerId);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        DestroyWindow(hwnd);
    }
    UnregisterClassW(kClassName, instance_);
}

void MainThreadWaker::Wake()
{
    HWND hwnd = hwnd_.load();
    if (hwnd != nullptr && !pending_.exchange(true)) {
        if (!PostMessageW(hwnd, kWakeMessage, 0, 0)) {
            pending_ = false;
        }
    }
}

void MainThreadWaker::Run()
{
    pending_ = false;
    if (callback_) {
        callback_();
    }
}

LRESULT CALLBACK MainThreadWaker::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<MainThreadWaker*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr && (msg == kWakeMessage || (msg == WM_TIMER && wParam == kTimerId))) {
        self->Run();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace RxHighlight
