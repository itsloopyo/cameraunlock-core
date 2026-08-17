#pragma once

#include <functional>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <vector>

namespace cameraunlock::input {

// Callback type for hotkey events
using HotkeyCallback = std::function<void()>;

// Thread-based hotkey polling system for Windows
// Polls keyboard state at regular intervals and fires callbacks on key press
class HotkeyPoller {
public:
    HotkeyPoller() = default;
    ~HotkeyPoller();

    // Non-copyable
    HotkeyPoller(const HotkeyPoller&) = delete;
    HotkeyPoller& operator=(const HotkeyPoller&) = delete;

    // Move-only. A running poller keeps polling after a move: the thread is
    // bound to the source's this-pointer, so it is stopped there and a fresh
    // one is started here. Key edge states reset in the process, so a key held
    // down across the move fires again on its next press, not immediately.
    HotkeyPoller(HotkeyPoller&& other) noexcept;
    HotkeyPoller& operator=(HotkeyPoller&& other) noexcept;

    // Set the toggle key and callback
    // vkCode: Windows virtual key code (e.g., VK_F10 = 0x79)
    void SetToggleKey(int vkCode, HotkeyCallback callback);

    // Set the recenter key and callback
    void SetRecenterKey(int vkCode, HotkeyCallback callback);

    // Add a generic hotkey with callback
    // Returns an ID that can be used to remove the hotkey
    int AddHotkey(int vkCode, HotkeyCallback callback);

    // Remove a hotkey by ID
    void RemoveHotkey(int id);

    // Start the polling thread
    // pollIntervalMs: polling interval in milliseconds (default 16ms = ~60Hz)
    bool Start(int pollIntervalMs = 16);

    // Stop the polling thread
    void Stop();

    // Check if polling is running
    bool IsRunning() const { return m_running.load(); }

    // Update hotkeys at runtime (thread-safe)
    void SetToggleKeyCode(int vkCode);
    void SetRecenterKeyCode(int vkCode);

    // Get current key codes
    int GetToggleKeyCode() const { return m_toggleKey.load(); }
    int GetRecenterKeyCode() const { return m_recenterKey.load(); }

    // For game-loop based polling (alternative to background thread)
    // Call this once per frame instead of using Start()
    void Poll();

private:
    void PollLoop();
    // Edge-detects and appends the callback to fire rather than invoking it, so Poll()
    // can release its locks before running user code. See the note in Poll().
    void CollectKey(int vkCode, std::atomic<bool>& keyDown, const HotkeyCallback& callback,
                    bool allowFire, std::vector<HotkeyCallback>& toFire);

    std::thread m_thread;
    std::atomic<bool> m_stopFlag{false};
    std::atomic<bool> m_running{false};
    std::atomic<int> m_pollInterval{16};

    // Built-in toggle/recenter keys
    std::atomic<int> m_toggleKey{0};
    std::atomic<int> m_recenterKey{0};
    std::atomic<bool> m_toggleKeyDown{false};
    std::atomic<bool> m_recenterKeyDown{false};
    HotkeyCallback m_toggleCallback;
    HotkeyCallback m_recenterCallback;
    std::mutex m_callbackMutex;

    // Generic hotkeys
    struct HotkeyEntry {
        int id;
        int vkCode;
        bool keyDown;
        HotkeyCallback callback;
    };
    std::vector<HotkeyEntry> m_hotkeys;
    std::mutex m_hotkeyMutex;
    int m_nextHotkeyId = 1;
};

// Common virtual key codes for convenience
namespace VK {
    constexpr int F1 = 0x70;
    constexpr int F2 = 0x71;
    constexpr int F3 = 0x72;
    constexpr int F4 = 0x73;
    constexpr int F5 = 0x74;
    constexpr int F6 = 0x75;
    constexpr int F7 = 0x76;
    constexpr int F8 = 0x77;
    constexpr int F9 = 0x78;
    constexpr int F10 = 0x79;
    constexpr int F11 = 0x7A;
    constexpr int F12 = 0x7B;
    constexpr int Escape = 0x1B;
    constexpr int Space = 0x20;
    constexpr int Home = 0x24;
    constexpr int End = 0x23;
    constexpr int Insert = 0x2D;
    constexpr int Delete = 0x2E;
    constexpr int NumPad0 = 0x60;
    constexpr int NumPad1 = 0x61;
    constexpr int NumPad2 = 0x62;
    constexpr int NumPad3 = 0x63;
    constexpr int NumPad4 = 0x64;
    constexpr int NumPad5 = 0x65;
    constexpr int NumPad6 = 0x66;
    constexpr int NumPad7 = 0x67;
    constexpr int NumPad8 = 0x68;
    constexpr int NumPad9 = 0x69;
}

// Convert virtual key code to human-readable string
const char* VirtualKeyToString(int vkCode);

// Check if a virtual key code is valid for hotkey use
bool IsValidHotkeyCode(int vkCode);

} // namespace cameraunlock::input
