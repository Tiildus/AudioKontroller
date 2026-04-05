// =============================================================================
// AudioHandler.h — Per-application PulseAudio volume control
//
// Uses pa_threaded_mainloop, which runs PulseAudio's event loop in its own
// internal thread. This is safer than pa_mainloop (which you'd poll manually)
// because PulseAudio provides explicit lock/unlock calls that let other threads
// submit operations without data races.
//
// Usage:
//   AudioHandler audio;
//   audio.init();                               // connect to PulseAudio
//   audio.setVolumeForApps({"firefox"}, 0.5f);   // 50% volume for Firefox
//   audio.setSystemVolume(0.8f);                 // 80% master/system volume
// =============================================================================

#pragma once
#include "ConfigManager.h"
#include <pulse/pulseaudio.h>
#include <functional>
#include <memory>
#include <string>

class AudioHandler {
public:
    AudioHandler() = default;
    ~AudioHandler();

    // Connects to PulseAudio. Returns true on success, false on failure.
    // Must be called before any volume control methods.
    bool init();

    bool isConnected() const { return connected; }

    // Injects a function that returns the focused window's PID.
    // Used by handleKnob() for "focused" knob type.
    void setGetPIDFunc(std::function<int()> func) { getPID = func; }

    // Dispatches a knob event based on the knob's config type.
    // Supported types: "app", "focused", "system"
    void handleKnob(const KnobConfig& kc, float volume);

    // Set volume (0.0–1.0) for all PulseAudio streams belonging to the named app(s).
    // Each name is matched against the process binary name reported by PulseAudio
    // (e.g. "firefox", "spotify", "discord").
    void setVolumeForApps(const std::vector<std::string>& appNames, float volume);

    // Set the master/system volume (0.0–1.0) on the default output device.
    // Affects all audio, regardless of which application is playing.
    void setSystemVolume(float volume);

private:
    std::function<int()> getPID;

    // pa_threaded_mainloop owns its own thread and exposes lock/unlock so
    // we can safely call PA operations from our HID thread.
    // Wrapped in unique_ptr for leak safety if init() fails partway through.
    // Only handles freeing — stop and context cleanup are done manually in the
    // destructor to preserve the required teardown order.
    struct PaMainloopDeleter {
        void operator()(pa_threaded_mainloop* ml) const { pa_threaded_mainloop_free(ml); }
    };
    std::unique_ptr<pa_threaded_mainloop, PaMainloopDeleter> mainloop;
    pa_context* context = nullptr;
    bool connected = false;

    // These fields describe the current volume request. They are only written
    // while holding the mainloop lock, and only read from inside PA callbacks
    // (which also run under the lock), so no extra synchronization is needed.
    std::vector<std::string> targetApps;
    bool isSystem = false;
    float targetVolume = 1.0f;

    // Called by PulseAudio when the context state changes (connecting, ready, failed).
    // Must be static because PA uses a C-style function pointer callback.
    static void contextStateCallback(pa_context* c, void* userdata);

    // Called by PulseAudio once per active audio stream (sink input).
    // eol == 0: valid stream info; eol != 0: end of list sentinel.
    static void sinkInputInfoCallback(pa_context* c, const pa_sink_input_info* info, int eol, void* userdata);

    // Called by PulseAudio with info about the default output device (sink).
    // Used by setSystemVolume to get the channel count before setting volume.
    static void sinkInfoCallback(pa_context* c, const pa_sink_info* info, int eol, void* userdata);

    // Kicks off a PA request to enumerate all active streams.
    // The result is delivered asynchronously to sinkInputInfoCallback.
    // Must be called with the mainloop lock held.
    void requestSinkInputs();

    // Must be called with the mainloop lock held.
    void requestSinkInfo();
};
