#pragma once
#include <hidapi/hidapi.h>
#include <functional>
#include <string>
#include <thread>
#include <atomic>

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

private:
    static constexpr uint8_t INPUT_KNOB   = 0x01;
    static constexpr uint8_t INPUT_BUTTON = 0x02;
    static constexpr int     HID_REPORT_SIZE = 64;
    static constexpr int     READ_TIMEOUT_MS = 100;
    static constexpr int     INIT_SKIP_READS = 20;

    hid_device* device = nullptr;
    CCCallback callback;
    ButtonCallback buttonCallback;
    std::atomic<bool> running{false};

    void getVidPid(PCPanelDevice type, uint16_t& vid, uint16_t& pid);
    void readLoop();
};
