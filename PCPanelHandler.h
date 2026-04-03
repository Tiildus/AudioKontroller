#pragma once
#include <hidapi/hidapi.h>
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <array>
#include <cstdint>

enum class PCPanelDevice {
    Mini,  // VID 0x0483, PID 0xA3C4 — 4 knobs, 4 buttons
    Pro,   // VID 0x0483, PID 0xA3C5 — 5 knobs + 4 sliders, 5 buttons
    RGB    // VID 0x04D8, PID 0xEB42 — 4 knobs, 4 buttons
};

class PCPanelHandler {
public:
    using CCCallback = std::function<void(int cc, float value)>;
    using ButtonCallback = std::function<void(int note)>;

    PCPanelHandler(PCPanelDevice deviceType);
    ~PCPanelHandler();

    void setCallback(CCCallback cb);
    void setButtonCallback(ButtonCallback cb);
    void startListening();

    // Minimum raw value change (0-255) required to fire the callback.
    // Prevents accidental triggers from lightly touching a knob.
    // Default: 4 (~1.5% of full range). Raise if still too sensitive.
    int knobThreshold = 4;

private:
    static constexpr uint8_t INPUT_KNOB      = 0x01;
    static constexpr uint8_t INPUT_BUTTON    = 0x02;
    static constexpr int     HID_REPORT_SIZE = 64;
    static constexpr int     READ_TIMEOUT_MS = 100;
    static constexpr int     INIT_SKIP_READS = 20;
    static constexpr int     MAX_KNOBS       = 16;
    static constexpr int     NO_VALUE        = -1; // sentinel: knob not yet seen

    hid_device* device = nullptr;
    CCCallback callback;
    ButtonCallback buttonCallback;
    std::atomic<bool> running{false};

    // Last raw value (0-255) reported for each knob index, -1 if never seen
    std::array<int, MAX_KNOBS> lastValue;

    void getVidPid(PCPanelDevice type, uint16_t& vid, uint16_t& pid);
    void readLoop();
};
