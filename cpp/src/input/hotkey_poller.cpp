#include <cameraunlock/input/hotkey_poller.h>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace cameraunlock::input {

constexpr int kKeyPressedMask = 0x8000;

HotkeyPoller::~HotkeyPoller() {
    Stop();
}

HotkeyPoller::HotkeyPoller(HotkeyPoller&& other) noexcept {
    // Stop the other's thread first
    other.Stop();

    m_toggleKey.store(other.m_toggleKey.load());
    m_recenterKey.store(other.m_recenterKey.load());
    m_pollInterval.store(other.m_pollInterval.load());
    m_toggleCallback = std::move(other.m_toggleCallback);
    m_recenterCallback = std::move(other.m_recenterCallback);

    std::lock_guard<std::mutex> lock(other.m_hotkeyMutex);
    m_hotkeys = std::move(other.m_hotkeys);
    m_nextHotkeyId = other.m_nextHotkeyId;
}

HotkeyPoller& HotkeyPoller::operator=(HotkeyPoller&& other) noexcept {
    if (this != &other) {
        Stop();
        other.Stop();

        m_toggleKey.store(other.m_toggleKey.load());
        m_recenterKey.store(other.m_recenterKey.load());
        m_pollInterval.store(other.m_pollInterval.load());
        m_toggleCallback = std::move(other.m_toggleCallback);
        m_recenterCallback = std::move(other.m_recenterCallback);

        std::lock_guard<std::mutex> lock(other.m_hotkeyMutex);
        m_hotkeys = std::move(other.m_hotkeys);
        m_nextHotkeyId = other.m_nextHotkeyId;
    }
    return *this;
}

void HotkeyPoller::SetToggleKey(int vkCode, HotkeyCallback callback) {
    m_toggleKey.store(vkCode);
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_toggleCallback = std::move(callback);
}

void HotkeyPoller::SetRecenterKey(int vkCode, HotkeyCallback callback) {
    m_recenterKey.store(vkCode);
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_recenterCallback = std::move(callback);
}

int HotkeyPoller::AddHotkey(int vkCode, HotkeyCallback callback) {
    std::lock_guard<std::mutex> lock(m_hotkeyMutex);
    int id = m_nextHotkeyId++;
    m_hotkeys.push_back({id, vkCode, false, std::move(callback)});
    return id;
}

void HotkeyPoller::RemoveHotkey(int id) {
    std::lock_guard<std::mutex> lock(m_hotkeyMutex);
    auto it = std::find_if(m_hotkeys.begin(), m_hotkeys.end(),
        [id](const HotkeyEntry& entry) { return entry.id == id; });
    if (it != m_hotkeys.end()) {
        m_hotkeys.erase(it);
    }
}

bool HotkeyPoller::Start(int pollIntervalMs) {
    if (m_running.load()) {
        return true;
    }

    m_pollInterval.store(pollIntervalMs);
    m_stopFlag.store(false);
    m_running.store(true);

    // Reset key states
    m_toggleKeyDown.store(false);
    m_recenterKeyDown.store(false);
    {
        std::lock_guard<std::mutex> lock(m_hotkeyMutex);
        for (auto& entry : m_hotkeys) {
            entry.keyDown = false;
        }
    }

    m_thread = std::thread(&HotkeyPoller::PollLoop, this);
    return true;
}

void HotkeyPoller::Stop() {
    if (!m_running.load()) {
        return;
    }

    m_stopFlag.store(true);

    if (m_thread.joinable()) {
        m_thread.join();
    }

    m_running.store(false);
}

void HotkeyPoller::SetToggleKeyCode(int vkCode) {
    m_toggleKey.store(vkCode);
}

void HotkeyPoller::SetRecenterKeyCode(int vkCode) {
    m_recenterKey.store(vkCode);
}

void HotkeyPoller::CheckKey(int vkCode, std::atomic<bool>& keyDown, const HotkeyCallback& callback,
                            bool allowFire) {
    if (vkCode == 0 || !callback) return;

#ifdef _WIN32
    bool pressed = (GetAsyncKeyState(vkCode) & kKeyPressedMask) != 0;
    if (pressed && !keyDown.load()) {
        keyDown.store(true);
        if (allowFire) callback();
    } else if (!pressed && keyDown.load()) {
        keyDown.store(false);
    }
#endif
}

void HotkeyPoller::PollLoop() {
    while (!m_stopFlag.load()) {
        Poll();

        int interval = m_pollInterval.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
    }
}

// GetAsyncKeyState sees keystrokes system-wide, so without a foreground guard
// a user typing End/Home/PageUp in another window silently toggles the mod.
// Key edges are still tracked while unfocused (allowFire=false) so a press
// that starts in another window doesn't fire a stale callback on refocus.
#ifdef _WIN32
namespace {
bool IsOwnProcessForeground() {
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    return foregroundPid == GetCurrentProcessId();
}
}  // namespace
#endif

void HotkeyPoller::Poll() {
#ifdef _WIN32
    const bool allowFire = IsOwnProcessForeground();
#else
    const bool allowFire = true;
#endif

    // Check built-in keys under callback lock (avoids copying std::function)
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        CheckKey(m_toggleKey.load(), m_toggleKeyDown, m_toggleCallback, allowFire);
        CheckKey(m_recenterKey.load(), m_recenterKeyDown, m_recenterCallback, allowFire);
    }

    // Check generic hotkeys
    {
        std::lock_guard<std::mutex> lock(m_hotkeyMutex);
        for (auto& entry : m_hotkeys) {
            if (entry.vkCode == 0 || !entry.callback) continue;

#ifdef _WIN32
            bool pressed = (GetAsyncKeyState(entry.vkCode) & kKeyPressedMask) != 0;
            if (pressed && !entry.keyDown) {
                entry.keyDown = true;
                if (allowFire) entry.callback();
            } else if (!pressed && entry.keyDown) {
                entry.keyDown = false;
            }
#endif
        }
    }
}

const char* VirtualKeyToString(int vkCode) {
    switch (vkCode) {
        case 0x70: return "F1";
        case 0x71: return "F2";
        case 0x72: return "F3";
        case 0x73: return "F4";
        case 0x74: return "F5";
        case 0x75: return "F6";
        case 0x76: return "F7";
        case 0x77: return "F8";
        case 0x78: return "F9";
        case 0x79: return "F10";
        case 0x7A: return "F11";
        case 0x7B: return "F12";
        case 0x1B: return "Escape";
        case 0x20: return "Space";
        case 0x24: return "Home";
        case 0x23: return "End";
        case 0x2D: return "Insert";
        case 0x2E: return "Delete";
        case 0x60: return "NumPad0";
        case 0x61: return "NumPad1";
        case 0x62: return "NumPad2";
        case 0x63: return "NumPad3";
        case 0x64: return "NumPad4";
        case 0x65: return "NumPad5";
        case 0x66: return "NumPad6";
        case 0x67: return "NumPad7";
        case 0x68: return "NumPad8";
        case 0x69: return "NumPad9";
        case 0x6A: return "NumPad*";
        case 0x6B: return "NumPad+";
        case 0x6D: return "NumPad-";
        case 0x6E: return "NumPad.";
        case 0x6F: return "NumPad/";
        case 0x90: return "NumLock";
        case 0x91: return "ScrollLock";
        case 0x13: return "Pause";
        case 0x2C: return "PrintScreen";
        // Number keys 0-9 (VK codes 0x30-0x39)
        case 0x30: return "0";
        case 0x31: return "1";
        case 0x32: return "2";
        case 0x33: return "3";
        case 0x34: return "4";
        case 0x35: return "5";
        case 0x36: return "6";
        case 0x37: return "7";
        case 0x38: return "8";
        case 0x39: return "9";
        // Letter keys A-Z (VK codes 0x41-0x5A)
        case 0x41: return "A";
        case 0x42: return "B";
        case 0x43: return "C";
        case 0x44: return "D";
        case 0x45: return "E";
        case 0x46: return "F";
        case 0x47: return "G";
        case 0x48: return "H";
        case 0x49: return "I";
        case 0x4A: return "J";
        case 0x4B: return "K";
        case 0x4C: return "L";
        case 0x4D: return "M";
        case 0x4E: return "N";
        case 0x4F: return "O";
        case 0x50: return "P";
        case 0x51: return "Q";
        case 0x52: return "R";
        case 0x53: return "S";
        case 0x54: return "T";
        case 0x55: return "U";
        case 0x56: return "V";
        case 0x57: return "W";
        case 0x58: return "X";
        case 0x59: return "Y";
        case 0x5A: return "Z";
        default:
            return "Unknown";
    }
}

bool IsValidHotkeyCode(int vkCode) {
    // Function keys F1-F12
    if (vkCode >= 0x70 && vkCode <= 0x7B) return true;

    // NumPad keys
    if (vkCode >= 0x60 && vkCode <= 0x6F) return true;

    // Special keys
    if (vkCode == 0x13) return true;  // Pause
    if (vkCode == 0x2C) return true;  // PrintScreen
    if (vkCode == 0x90) return true;  // NumLock
    if (vkCode == 0x91) return true;  // ScrollLock
    if (vkCode == 0x24) return true;  // Home
    if (vkCode == 0x23) return true;  // End
    if (vkCode == 0x2D) return true;  // Insert
    if (vkCode == 0x2E) return true;  // Delete

    return false;
}

} // namespace cameraunlock::input
