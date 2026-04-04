// =============================================================================
// PCPanelHandler.cpp — HID read loop and device management
// =============================================================================

#include "PCPanelHandler.h"
#include "Logger.h"
#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>

PCPanelHandler::PCPanelHandler(PCPanelDevice deviceType) {
    lastValue.fill(NO_VALUE); // all knobs start as "never seen"

    hid_init(); // initialize the hidapi library (sets up libusb internally)

    uint16_t vid, pid;
    getVidPid(deviceType, vid, pid);

    // Open the device by vendor ID + product ID. nullptr = any serial number.
    // This will fail if the user isn't in the "uinput" group or if the udev
    // rules haven't been applied (see install.sh).
    device.reset(hid_open(vid, pid, nullptr));
    if (!device) {
        std::ostringstream ss;
        ss << "Failed to open device (VID=0x" << std::hex << vid << " PID=0x" << pid << ")";
        Logger::instance().error("PCPanel", ss.str());
        return;
    }

    // Non-blocking mode: hid_read() returns immediately with 0 bytes instead
    // of blocking forever when no data is available. We use hid_read_timeout()
    // instead, which gives us a bounded wait with a timeout.
    hid_set_nonblocking(device.get(), 1);
    Logger::instance().info("PCPanel", "Connected to device");
}

PCPanelHandler::~PCPanelHandler() {
<<<<<<< HEAD
    stopListening(); // join the thread before closing the device handle
    device.reset();  // close HID device before shutting down hidapi
    hid_exit();      // clean up hidapi/libusb resources
=======
    stopListening();
    if (device) {
        hid_close(device);
    }
    hid_exit();
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
}

void PCPanelHandler::setCallback(CCCallback cb)           { callback = cb; }
void PCPanelHandler::setButtonCallback(ButtonCallback cb) { buttonCallback = cb; }

<<<<<<< HEAD
=======
void PCPanelHandler::setButtonCallback(ButtonCallback cb) {
    buttonCallback = cb;
}

>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
void PCPanelHandler::startListeningAsync() {
    if (!device) {
        Logger::instance().error("PCPanel", "No device connected, cannot listen");
        return;
    }
    running = true;
    readThread = std::thread(&PCPanelHandler::readLoop, this);
}

<<<<<<< HEAD
// Signals the loop to stop and waits for the thread to finish.
// Joinable check is needed because stopListening() may be called from
// both the destructor and the signal handler path.
=======
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
void PCPanelHandler::stopListening() {
    running = false;
    if (readThread.joinable()) readThread.join();
}

void PCPanelHandler::getVidPid(PCPanelDevice type, uint16_t& vid, uint16_t& pid) {
    switch (type) {
        case PCPanelDevice::Mini: vid = 0x0483; pid = 0xA3C4; break;
        case PCPanelDevice::Pro:  vid = 0x0483; pid = 0xA3C5; break;
        case PCPanelDevice::RGB:  vid = 0x04D8; pid = 0xEB42; break;
    }
}

// Handles a HID read error: logs once per streak, backs off, and checks
// whether the error threshold has been reached. Returns false when the
// caller should exit the read loop.
bool PCPanelHandler::handleHidError(int& consecutiveErrors) {
    consecutiveErrors++;
    if (consecutiveErrors == 1) {
        Logger::instance().error("PCPanel", "HID read error, device may be disconnected");
    }
    if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
        Logger::instance().error("PCPanel", "Too many HID errors, stopping");
        running = false;
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(ERROR_BACKOFF_MS));
    return true;
}

// Applies dead-zone filtering and fires the knob callback if the movement
// exceeds the threshold.
void PCPanelHandler::processKnobEvent(uint8_t index, uint8_t value) {
    if (!callback || index >= MAX_KNOBS) return;

    int prev = lastValue[index];
    if (prev == NO_VALUE || std::abs(value - prev) >= knobThreshold) {
        lastValue[index] = value;
        float normalized = value / HID_VALUE_MAX;
        callback(index, normalized);
    }
}

// Fires the button callback on press events only (ignores releases).
void PCPanelHandler::processButtonEvent(uint8_t index, uint8_t value) {
    if (value == 1 && buttonCallback) {
        buttonCallback(index);
    }
}

// Main HID read loop — runs on readThread.
//
// hid_read_timeout() return values:
//   > 0  : bytes read, process the report
//   == 0 : timeout expired, no data (normal, try again)
//   < 0  : error (device disconnected, driver error, etc.)
void PCPanelHandler::readLoop() {
    unsigned char buf[HID_REPORT_SIZE];
    int skipCount = 0;
    int consecutiveErrors = 0;

    while (running) {
        int bytesRead = hid_read_timeout(device.get(), buf, HID_REPORT_SIZE, READ_TIMEOUT_MS);

<<<<<<< HEAD
        if (bytesRead == 0) continue;
=======
        if (bytesRead == 0) continue; // timeout, normal

        if (bytesRead < 0) {
            consecutiveErrors++;
            if (consecutiveErrors == 1) {
                Logger::instance().error("PCPanel", "HID read error, device may be disconnected");
            }
            if (consecutiveErrors >= 50) {
                Logger::instance().error("PCPanel", "Too many HID errors, stopping");
                running = false;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        consecutiveErrors = 0;
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5

        if (bytesRead < 0) {
            if (!handleHidError(consecutiveErrors)) break;
            continue;
        }

        consecutiveErrors = 0;
        if (bytesRead < MIN_REPORT_BYTES) continue;

        uint8_t type  = buf[0];
        uint8_t index = buf[1];
        uint8_t value = buf[2];

        // The device sends a burst of knob position reports when first opened,
        // reflecting the current physical state of each knob. Skip those to
        // avoid setting unintended volumes at startup. Only knob events are
        // skipped — button presses are always intentional user actions.
        if (type == INPUT_KNOB && skipCount < INIT_SKIP_READS) {
            skipCount++;
            continue;
        }

        if (type == INPUT_KNOB)        processKnobEvent(index, value);
        else if (type == INPUT_BUTTON) processButtonEvent(index, value);
    }
}
