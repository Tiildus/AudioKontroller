// =============================================================================
// AudioHandler.cpp — PulseAudio volume control implementation
//
// Threading model:
//   - pa_threaded_mainloop runs PA's event loop in its own internal thread.
//   - Our HID thread calls setVolumeForApps/setSystemVolume (via handleKnob).
//   - Those calls lock the mainloop, write the target fields, then kick off
//     a PA operation. The operation's result callback fires on the PA thread.
//   - Because both sides use lock/unlock, there are no data races.
// =============================================================================

#include "AudioHandler.h"
#include "Logger.h"
#include "Util.h"
#include <cmath>

bool AudioHandler::init() {
    mainloop.reset(pa_threaded_mainloop_new());
    if (!mainloop) {
        Logger::instance().error("Audio", "Failed to create PulseAudio mainloop");
        return false;
    }

    // A pa_context is the connection to the PulseAudio server. We need the
    // mainloop API object to associate them.
    pa_mainloop_api* api = pa_threaded_mainloop_get_api(mainloop.get());
    context = pa_context_new(api, "AudioKontroller");
    if (!context) {
        Logger::instance().error("Audio", "Failed to create PulseAudio context");
        mainloop.reset();
        return false;
    }

    // Register a callback so we get notified when the connection state changes.
    // The callback will signal the mainloop to unblock our wait loop below.
    pa_context_set_state_callback(context, &AudioHandler::contextStateCallback, this);

    pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr);
    if (pa_threaded_mainloop_start(mainloop.get()) < 0) {
        Logger::instance().error("Audio", "Failed to start PulseAudio mainloop");
        pa_context_unref(context);
        context = nullptr;
        mainloop.reset();
        return false;
    }

    // Wait here until the context reaches READY (or a failure state).
    // We must hold the lock while waiting so that PA doesn't race with us.
    // pa_threaded_mainloop_wait atomically releases the lock and sleeps until
    // pa_threaded_mainloop_signal wakes us (from contextStateCallback).
    // The while loop guards against spurious wakeups.
    {
        PaLockGuard lock(mainloop.get());
        while (true) {
            pa_context_state_t state = pa_context_get_state(context);
            if (state == PA_CONTEXT_READY) {
                connected = true;
                break;
            }
            if (!PA_CONTEXT_IS_GOOD(state)) {
                Logger::instance().error("Audio", "PulseAudio connection failed");
                break;
            }
            pa_threaded_mainloop_wait(mainloop.get()); // releases lock, sleeps, re-acquires
        }
    }

    if (connected) {
        Logger::instance().info("Audio", "Connected to PulseAudio");
    }
    return connected;
}

AudioHandler::~AudioHandler() {
    // Must stop the PA thread before disconnecting context, and disconnect
    // context before freeing the mainloop. unique_ptr handles the final free.
    if (mainloop) pa_threaded_mainloop_stop(mainloop.get());
    if (context) {
        pa_context_disconnect(context);
        pa_context_unref(context);
    }
}

// Called by PulseAudio whenever the connection state changes.
// We only need to unblock the constructor's wait loop, so we just signal.
void AudioHandler::contextStateCallback(pa_context*, void* userdata) {
    auto* handler = static_cast<AudioHandler*>(userdata);
    pa_threaded_mainloop_signal(handler->mainloop.get(), 0);
}

// Caller must hold the mainloop lock.
void AudioHandler::requestSinkInputs() {
    pa_operation* op = pa_context_get_sink_input_info_list(
        context, &AudioHandler::sinkInputInfoCallback, this);
    if (!op) {
        Logger::instance().error("Audio", "Failed to get sink input list");
        return;
    }
    // Unreffing the operation just releases our handle to the token.
    // The actual work continues asynchronously in the PA thread.
    pa_operation_unref(op);
}

// Called once per active audio stream by PulseAudio.
// When eol != 0, it's the end-of-list sentinel — no real stream data.
void AudioHandler::sinkInputInfoCallback(pa_context*, const pa_sink_input_info* info, int eol, void* userdata) {
    if (eol != 0 || !info) return;

    auto* handler = static_cast<AudioHandler*>(userdata);

    // Gather all name fields PulseAudio provides for this stream.
    // Different apps populate different fields, so we check all of them:
    //   application.process.binary — e.g. "firefox", "Discord"
    //   application.name           — e.g. "Firefox", "WEBRTC VoiceEngine"
    //   media.name                 — e.g. "Playback", "AudioStream"
    const char* names[] = {
        pa_proplist_gets(info->proplist, "application.process.binary"),
        pa_proplist_gets(info->proplist, "application.name"),
        pa_proplist_gets(info->proplist, "media.name"),
    };

    // Match if any config target is a case-insensitive substring of any
    // PulseAudio name field. This means "discord" in config matches a
    // binary named "Discord" or an app named "discord-canary".
    // The "focused" knob type also uses this path — handleKnob resolves the
    // focused window's PID to a process name via /proc, then feeds it here.
    bool match = false;
    for (const auto& target : handler->targetApps) {
        for (const char* name : names) {
            if (name && containsIgnoreCase(std::string(name), target)) {
                match = true;
                break;
            }
        }
        if (match) break;
    }

    if (match) {
        // Build a pa_cvolume with the correct channel count for this stream.
        // Using info->volume.channels ensures stereo/surround streams all work
        // correctly instead of hardcoding 2 channels.
        pa_cvolume cv;
        pa_cvolume_set(&cv, info->volume.channels,
            static_cast<uint32_t>(handler->targetVolume * PA_VOLUME_NORM));

        // Submit the volume change. Fire-and-forget; we don't need a callback.
        pa_operation* op = pa_context_set_sink_input_volume(
            handler->context, info->index, &cv, nullptr, nullptr);
        if (op) pa_operation_unref(op);
    }
}

void AudioHandler::handleKnob(const KnobConfig& kc, float volume) {
    volume = std::clamp(volume, 0.0f, 1.0f);
    if (kc.type == "app") {
        setVolumeForApps(kc.targets, std::pow(volume, volumeGamma));
    } else if (kc.type == "focused") {
        if (getPID) {
            int pid = getPID();
            if (pid > 0) {
                // Resolve the focused window's PID to a process name and match
                // by name instead of PID. This handles Electron/Chromium apps
                // (Discord, VS Code, etc.) where the window-owning process and
                // the audio-playing process are different PIDs but share the
                // same binary name in PulseAudio.
                std::string name = getProcessName(pid);
                if (!name.empty())
                    setVolumeForApps({name}, std::pow(volume, volumeGamma));
            }
        }
    } else if (kc.type == "system") {
        setSystemVolume(volume);
    }
}

// Locks the mainloop so it's safe to call from the HID thread.
void AudioHandler::setVolumeForApps(const std::vector<std::string>& appNames, float volume) {
    if (!connected) return;
    PaLockGuard lock(mainloop.get());
    targetApps = appNames;
    isSystem = false;
    targetVolume = volume;
    requestSinkInputs(); // enumerate streams; callback applies the volume
}

// Locks the mainloop so it's safe to call from the HID thread.
void AudioHandler::setSystemVolume(float volume) {
    if (!connected) return;
    PaLockGuard lock(mainloop.get());
    isSystem = true;
    targetApps.clear();
    targetVolume = volume;
    requestSinkInfo();
}

// Must be called with the mainloop lock held.
void AudioHandler::requestSinkInfo() {
    pa_operation* op = pa_context_get_sink_info_by_name(
        context, "@DEFAULT_SINK@", &AudioHandler::sinkInfoCallback, this);
    if (!op) {
        Logger::instance().error("Audio", "Failed to query default sink");
        return;
    }
    pa_operation_unref(op);
}

// Called by PulseAudio with info about the default output sink.
// Sets the sink volume using the actual channel count of the device.
void AudioHandler::sinkInfoCallback(pa_context*, const pa_sink_info* info, int eol, void* userdata) {
    if (eol != 0 || !info) return;

    auto* handler = static_cast<AudioHandler*>(userdata);
    if (!handler->isSystem) return;

    pa_cvolume cv;
    pa_cvolume_set(&cv, info->volume.channels,
        static_cast<uint32_t>(handler->targetVolume * PA_VOLUME_NORM));

    pa_operation* op = pa_context_set_sink_volume_by_index(
        handler->context, info->index, &cv, nullptr, nullptr);
    if (op) pa_operation_unref(op);
}
