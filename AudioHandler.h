#pragma once
#include <pulse/pulseaudio.h>
#include <string>

class AudioHandler {
public:
    AudioHandler();
    ~AudioHandler();

    bool isConnected() const { return connected; }
    void setVolumeForApp(const std::string& appName, float volume);
    void setVolumeForPID(uint32_t pid, float volume);

private:
    pa_threaded_mainloop* mainloop = nullptr;
    pa_context* context = nullptr;
    bool connected = false;

    // Filter parameters for callback (protected by mainloop lock)
    std::string targetApp;
    uint32_t targetPID = 0;
    bool hasPID = false;
    float targetVolume = 1.0f;

    static void contextStateCallback(pa_context* c, void* userdata);
    static void sinkInputInfoCallback(pa_context* c, const pa_sink_input_info* info, int eol, void* userdata);

    void requestSinkInputs(); // must be called with lock held
};
