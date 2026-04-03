#include "AudioHandler.h"
#include <iostream>
#include <thread>
#include <chrono>

// AudioHandler manages a connection to PulseAudio and allows setting per-stream volumes.
// Major responsibilities:
// - create and manage a PulseAudio mainloop and context
// - query sink inputs (per-application playback streams)
// - match streams by process name or PID and apply a requested volume
AudioHandler::AudioHandler() {
    // Create the mainloop and context used for all PulseAudio operations
    mainloop = pa_mainloop_new();
    context = pa_context_new(pa_mainloop_get_api(mainloop), "MidiController");

    // Register a simple state callback to log when the context becomes ready or fails
    pa_context_set_state_callback(context, &AudioHandler::contextStateCallback, this);
    // Connect to the local PulseAudio server with default flags
    pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr);

    // Wait until the context becomes ready (synchronous wait loop)
    // Start PulseAudio mainloop in a background thread
    std::thread([this]() {
        int ret;
        pa_mainloop_run(mainloop, &ret);
    }).detach();

    // Wait until the context is ready (without manually iterating)
    while (true) {
        pa_context_state_t state = pa_context_get_state(context);
        if (state == PA_CONTEXT_READY) break;
        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
            std::cerr << "[Audio] PulseAudio connection failed\n";
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "[Audio] Connected to PulseAudio\n";
}

// Clean up PulseAudio resources when the handler is destroyed
AudioHandler::~AudioHandler() {
    if (context) {
        pa_context_disconnect(context);
        pa_context_unref(context);
        context = nullptr;
    }
    if (mainloop) {
        pa_mainloop_quit(mainloop, 0);
        pa_mainloop_free(mainloop);
        mainloop = nullptr;
    }
}

// Simple context state callback used to print state transitions.
// It's registered in the constructor to observe when the context becomes ready/failed.
void AudioHandler::contextStateCallback(pa_context* c, void* userdata) {
    pa_context_state_t state = pa_context_get_state(c);
    if (state == PA_CONTEXT_READY)
        std::cout << "[Audio] Context ready\n";
    else if (state == PA_CONTEXT_FAILED)
        std::cerr << "[Audio] Context failed\n";
}

// Request the list of current sink inputs (playback streams) from PulseAudio.
// This function issues an asynchronous operation and then runs the mainloop
// until the operation completes to behave synchronously for callers.
void AudioHandler::requestSinkInputs() {
    pa_operation* op = pa_context_get_sink_input_info_list(
        context, &AudioHandler::sinkInputInfoCallback, this);

    if (!op) {
        std::cerr << "[Audio] Failed to get sink input list\n";
        return;
    }

    // Wait for the operation to finish by iterating the mainloop
    if (op)
        pa_operation_unref(op);
}

// Callback invoked for each sink input returned by PulseAudio.
// This examines properties to extract the process id and application name,
// determines whether it matches the configured target (by name or PID),
// and applies the configured volume if requested.
void AudioHandler::sinkInputInfoCallback(pa_context*, const pa_sink_input_info* info, int eol, void* userdata) {
    // eol > 0 = normal end-of-list, eol < 0 = error (info is NULL in both cases)
    if (eol != 0 || !info) return;

    auto* handler = static_cast<AudioHandler*>(userdata);
    const pa_proplist* props = info->proplist;

    // Extract process ID from properties (application.process.id)
    const char* pidStr = pa_proplist_gets(props, "application.process.id");
    std::optional<uint32_t> pid;
    if (pidStr) pid = std::stoi(pidStr);

    // Extract an application name: prefer process binary, fallback to media.name
    const char* appBin = pa_proplist_gets(props, "application.process.binary");
    if (!appBin) appBin = pa_proplist_gets(props, "media.name");
    std::string appName = appBin ? appBin : "";

    std::cout << "[Audio] Found stream index " << info->index
              << " app=" << appName
              << " pid=" << (pid ? std::to_string(*pid) : "(none)") << "\n";

    // Determine whether this stream matches the requested target (by name or PID)
    bool match = false;
    if (!handler->targetApp.empty() && handler->targetApp == appName)
        match = true;
    else if (handler->targetPID && pid && handler->targetPID.value() == pid.value())
        match = true;

    // If it's a match and applyVolume is set, change the stream volume
    if (match && handler->applyVolume) {
        handler->setVolume(info->index, handler->targetVolume);
        std::cout << "[Audio] Volume set for " << appName
                  << " (index " << info->index << ") to "
                  << int(handler->targetVolume * 100) << "%\n";
    }
}

// Set the volume for a specific sink input index.
// Volume uses PulseAudio's pa_cvolume structure; this builds a stereo volume
// with the same level on both channels and issues a set operation.
void AudioHandler::setVolume(uint32_t index, float volume) {
    pa_cvolume cv;
    // Convert float (0.0 - 1.0) to PulseAudio's integer volume scale
    pa_cvolume_set(&cv, 2, static_cast<uint32_t>(volume * PA_VOLUME_NORM));

    pa_operation* op = pa_context_set_sink_input_volume(context, index, &cv, nullptr, nullptr);
    if (op) {
        // Wait for the operation to complete
        if (op)
            pa_operation_unref(op);
    }
}

// Public helper: set volume by application name. This sets internal target fields
// and triggers a sink input query to find and modify matching streams.
void AudioHandler::setVolumeForApp(const std::string& appName, float volume) {
    targetApp = appName;
    targetPID.reset();
    targetVolume = volume;
    applyVolume = true;
    requestSinkInputs();
}

// Public helper: set volume by process ID. Similar to setVolumeForApp but targets PID.
void AudioHandler::setVolumeForPID(uint32_t pid, float volume) {
    targetApp.clear();
    targetPID = pid;
    targetVolume = volume;
    applyVolume = true;
    requestSinkInputs();
}
