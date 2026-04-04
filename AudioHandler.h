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
//   audio.setVolumeForApp("firefox", 0.5f);     // 50% volume for Firefox
//   audio.setVolumeForPID(1234, 0.75f);         // 75% volume for PID 1234
//   audio.setSystemVolume(0.8f);                // 80% master/system volume
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

<<<<<<< HEAD
    // Connects to PulseAudio. Returns true on success, false on failure.
    // Must be called before setVolumeForApp/setVolumeForPID.
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

    // Set volume (0.0–1.0) for all PulseAudio streams belonging to a specific PID.
    // Used for the "focused" knob, where we know the window's PID but not its name.
=======
    bool isConnected() const { return connected; }
    void setVolumeForApp(const std::string& appName, float volume);
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
    void setVolumeForPID(uint32_t pid, float volume);

    // Set the master/system volume (0.0–1.0) on the default output device.
    // Affects all audio, regardless of which application is playing.
    void setSystemVolume(float volume);

private:
<<<<<<< HEAD
    // Holds the injected PID-lookup function for "focused" knob type.
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
    std::vector<std::string> targetApps;  // non-empty when matching by name(s)
    uint32_t targetPID = 0;
    bool hasPID = false;     // true when matching by PID instead of name
    bool isSystem = false;   // true when targeting the default output device
=======
    pa_threaded_mainloop* mainloop = nullptr;
    pa_context* context = nullptr;
    bool connected = false;

    // Filter parameters for callback (protected by mainloop lock)
    std::string targetApp;
    uint32_t targetPID = 0;
    bool hasPID = false;
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
    float targetVolume = 1.0f;

    // Called by PulseAudio when the context state changes (connecting, ready, failed).
    // Must be static because PA uses a C-style function pointer callback.
    static void contextStateCallback(pa_context* c, void* userdata);

    // Called by PulseAudio once per active audio stream (sink input).
    // eol == 0: valid stream info; eol != 0: end of list sentinel.
    static void sinkInputInfoCallback(pa_context* c, const pa_sink_input_info* info, int eol, void* userdata);

<<<<<<< HEAD
    // Called by PulseAudio with info about the default output device (sink).
    // Used by setSystemVolume to get the channel count before setting volume.
    static void sinkInfoCallback(pa_context* c, const pa_sink_info* info, int eol, void* userdata);

    // Kicks off a PA request to enumerate all active streams.
    // The result is delivered asynchronously to sinkInputInfoCallback.
    // Must be called with the mainloop lock held.
    void requestSinkInputs();

    // Kicks off a PA request to query the default output device.
    // The result is delivered asynchronously to sinkInfoCallback.
    // Must be called with the mainloop lock held.
    void requestSinkInfo();
=======
    void requestSinkInputs(); // must be called with lock held
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
};
