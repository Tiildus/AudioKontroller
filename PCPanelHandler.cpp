#include "PCPanelHandler.h"
#include "Logger.h"
#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>

PCPanelHandler::PCPanelHandler(PCPanelDevice deviceType) {
    lastValue.fill(NO_VALUE);
    hid_init();

    uint16_t vid, pid;
    getVidPid(deviceType, vid, pid);

    device = hid_open(vid, pid, nullptr);
    if (!device) {
        std::ostringstream ss;
        ss << "Failed to open device (VID=0x" << std::hex << vid << " PID=0x" << pid << ")";
        Logger::instance().error("PCPanel", ss.str());
        return;
    }

    hid_set_nonblocking(device, 1);
    Logger::instance().info("PCPanel", "Connected to device");
}

PCPanelHandler::~PCPanelHandler() {
    stopListening();
    if (device) {
        hid_close(device);
    }
    hid_exit();
}

void PCPanelHandler::setCallback(CCCallback cb) {
    callback = cb;
}

void PCPanelHandler::setButtonCallback(ButtonCallback cb) {
    buttonCallback = cb;
}

void PCPanelHandler::startListeningAsync() {
    if (!device) {
        Logger::instance().error("PCPanel", "No device connected, cannot listen");
        return;
    }
    running = true;
    readThread = std::thread(&PCPanelHandler::readLoop, this);
}

void PCPanelHandler::stopListening() {
    running = false;
    if (readThread.joinable()) readThread.join();
}

void PCPanelHandler::getVidPid(PCPanelDevice type, uint16_t& vid, uint16_t& pid) {
    switch (type) {
        case PCPanelDevice::Mini:
            vid = 0x0483; pid = 0xA3C4; break;
        case PCPanelDevice::Pro:
            vid = 0x0483; pid = 0xA3C5; break;
        case PCPanelDevice::RGB:
            vid = 0x04D8; pid = 0xEB42; break;
    }
}

void PCPanelHandler::readLoop() {
    unsigned char buf[HID_REPORT_SIZE];
    int skipCount = 0;
    int consecutiveErrors = 0;

    while (running) {
        int bytesRead = hid_read_timeout(device, buf, HID_REPORT_SIZE, READ_TIMEOUT_MS);

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

        // Skip initial reads while device stabilizes
        if (skipCount < INIT_SKIP_READS) {
            skipCount++;
            continue;
        }

        if (bytesRead < 3) continue;

        uint8_t type  = buf[0];
        uint8_t index = buf[1];
        uint8_t value = buf[2];

        if (type == INPUT_KNOB && callback) {
            if (index < MAX_KNOBS) {
                int prev = lastValue[index];
                if (prev == NO_VALUE || std::abs(value - prev) >= knobThreshold) {
                    lastValue[index] = value;
                    float normalized = value / 255.0f;
                    callback(index, normalized);
                }
            }
        }
        else if (type == INPUT_BUTTON && value == 1 && buttonCallback) {
            buttonCallback(index);
        }
    }
}
