#pragma once
// Runs a callback on EuroScope's main (UI) thread:
//  * immediately when another thread calls Wake(), and
//  * every `intervalMs` as a safety net (e.g. to end the linger period).
//
// It uses a hidden message-only window owned by the thread that creates it,
// so EuroScope's own message loop delivers the calls. This is the standard,
// thread-safe way to get from a background thread back onto the UI thread;
// the EuroScope API is only ever called from the callback, on the UI thread.

#include <atomic>
#include <functional>

#include <windows.h>

namespace RxHighlight {

class MainThreadWaker
{
public:
    // Must be constructed on the EuroScope main thread.
    MainThreadWaker(std::function<void()> callback, UINT intervalMs);
    ~MainThreadWaker();

    MainThreadWaker(const MainThreadWaker&) = delete;
    MainThreadWaker& operator=(const MainThreadWaker&) = delete;

    // Safe to call from any thread. Multiple calls before the callback runs
    // are coalesced into one.
    void Wake();

    bool IsValid() const { return hwnd_ != nullptr; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Run();

    std::function<void()> callback_;
    std::atomic<HWND> hwnd_{nullptr};
    std::atomic<bool> pending_{false};
    HINSTANCE instance_ = nullptr;
};

} // namespace RxHighlight
