// =============================================================================
// PCPanelHandler.h — USB HID input from PCPanel hardware controllers
//
// The PCPanel is a hardware controller with physical knobs and buttons.
// It communicates over USB using HID (Human Interface Device) protocol —
// the same standard used by keyboards and mice. No custom driver is needed;
// the hidapi library provides cross-platform access to HID devices.
//
// This class opens the USB device, reads raw HID reports in a background
// thread, and translates them into typed callbacks:
//   - setCallback()       — fires on every knob movement with (knob index, 0–1 float)
//   - setButtonCallback() — fires on button press with (button index)
//
// Supported device variants (differ only in USB IDs and control count):
//   Mini  — 4 knobs, 4 buttons
//   Pro   — 5 knobs + 4 sliders, 5 buttons
//   RGB   — 4 knobs, 4 buttons
// =============================================================================

#pragma once
#include <hidapi/hidapi.h>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <array>
#include <cstdint>

enum class PCPanelDevice {
    Mini,  // VID 0x0483, PID 0xA3C4
    Pro,   // VID 0x0483, PID 0xA3C5
    RGB    // VID 0x04D8, PID 0xEB42
};

class PCPanelHandler {
public:
    // Callback types: (knob index, normalized 0.0–1.0 value) and (button index)
    using CCCallback     = std::function<void(int cc, float value)>;
    using ButtonCallback = std::function<void(int note)>;

    PCPanelHandler(PCPanelDevice deviceType);
    ~PCPanelHandler();

    void setCallback(CCCallback cb);
    void setButtonCallback(ButtonCallback cb);

    void startListeningAsync();
    // Sets running=false and joins the background thread (waits for it to exit).
    void stopListening();
    // Signals the read loop to stop. Writes an atomic bool — safe from a signal handler.
    void requestStop() { running = false; }

    bool isConnected() const { return device != nullptr; }

    // Minimum raw value change (0–255 scale) required to fire the knob callback.
    // Filters out electrical noise / jitter from potentiometers at rest.
    // Atomic because it's read from the HID thread in processKnobEvent().
    std::atomic<int> knobThreshold{4};

private:
    // HID report byte 0 values that identify knob vs button events.
    static constexpr uint8_t INPUT_KNOB      = 0x01;
    static constexpr uint8_t INPUT_BUTTON    = 0x02;

    static constexpr int HID_REPORT_SIZE = 64;  // bytes per HID report from the device
    static constexpr int READ_TIMEOUT_MS = 100; // how long to wait for each report (milliseconds)
    static constexpr int INIT_SKIP_READS = 20;  // discard first N reads while device stabilizes
    static constexpr int MAX_KNOBS       = 16;  // maximum knob index supported
    static constexpr int NO_VALUE        = -1;  // sentinel: knob hasn't been touched yet

    static constexpr float HID_VALUE_MAX          = 255.0f; // max raw value from HID report
    static constexpr int   MAX_CONSECUTIVE_ERRORS = 50;     // error count before giving up
    static constexpr int   ERROR_BACKOFF_MS       = 100;    // sleep between error retries
    static constexpr int   MIN_REPORT_BYTES       = 3;      // minimum valid report size

    struct HidDeviceDeleter {
        void operator()(hid_device* d) const { hid_close(d); }
    };
    std::unique_ptr<hid_device, HidDeviceDeleter> device;
    CCCallback callback;
    ButtonCallback buttonCallback;

    // atomic<bool> so requestStop() can safely write from a signal handler
    // while the read loop thread reads it.
    std::atomic<bool> running{false};
    std::thread readThread;

    // Tracks the last reported value per knob to implement the dead zone.
    // Initialized to NO_VALUE so the first movement always fires.
    std::array<int, MAX_KNOBS> lastValue;

    void getVidPid(PCPanelDevice type, uint16_t& vid, uint16_t& pid);
    void readLoop(); // runs on readThread

    bool handleHidError(int& consecutiveErrors);
    void processKnobEvent(uint8_t index, uint8_t value);
    void processButtonEvent(uint8_t index, uint8_t value);
};
