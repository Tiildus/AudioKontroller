#include "AudioHandler.h"
#include "Logger.h"

AudioHandler::AudioHandler() {
    mainloop = pa_threaded_mainloop_new();
    if (!mainloop) {
        Logger::instance().error("Audio", "Failed to create PulseAudio mainloop");
        return;
    }

    pa_mainloop_api* api = pa_threaded_mainloop_get_api(mainloop);
    context = pa_context_new(api, "AudioKontroller");
    if (!context) {
        Logger::instance().error("Audio", "Failed to create PulseAudio context");
        pa_threaded_mainloop_free(mainloop);
        mainloop = nullptr;
        return;
    }

    pa_context_set_state_callback(context, &AudioHandler::contextStateCallback, this);
    pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr);

    pa_threaded_mainloop_start(mainloop);

    // Wait for context to become ready with proper locking
    pa_threaded_mainloop_lock(mainloop);
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
        pa_threaded_mainloop_wait(mainloop);
    }
    pa_threaded_mainloop_unlock(mainloop);

    if (connected) {
        Logger::instance().info("Audio", "Connected to PulseAudio");
    }
}

AudioHandler::~AudioHandler() {
    if (mainloop) pa_threaded_mainloop_stop(mainloop);
    if (context) {
        pa_context_disconnect(context);
        pa_context_unref(context);
    }
    if (mainloop) pa_threaded_mainloop_free(mainloop);
}

void AudioHandler::contextStateCallback(pa_context*, void* userdata) {
    auto* handler = static_cast<AudioHandler*>(userdata);
    pa_threaded_mainloop_signal(handler->mainloop, 0);
}

void AudioHandler::requestSinkInputs() {
    pa_operation* op = pa_context_get_sink_input_info_list(
        context, &AudioHandler::sinkInputInfoCallback, this);
    if (!op) {
        Logger::instance().error("Audio", "Failed to get sink input list");
        return;
    }
    pa_operation_unref(op);
}

void AudioHandler::sinkInputInfoCallback(pa_context*, const pa_sink_input_info* info, int eol, void* userdata) {
    if (eol != 0 || !info) return;

    auto* handler = static_cast<AudioHandler*>(userdata);

    // Extract process ID safely (no exceptions)
    const char* pidStr = pa_proplist_gets(info->proplist, "application.process.id");
    uint32_t pid = 0;
    bool hasPid = false;
    if (pidStr) {
        char* end = nullptr;
        long val = strtol(pidStr, &end, 10);
        if (end != pidStr && val > 0) {
            pid = static_cast<uint32_t>(val);
            hasPid = true;
        }
    }

    const char* appBin = pa_proplist_gets(info->proplist, "application.process.binary");
    if (!appBin) appBin = pa_proplist_gets(info->proplist, "media.name");
    std::string appName = appBin ? appBin : "";

    bool match = false;
    if (!handler->targetApp.empty() && handler->targetApp == appName)
        match = true;
    else if (handler->hasPID && hasPid && handler->targetPID == pid)
        match = true;

    if (match) {
        pa_cvolume cv;
        pa_cvolume_set(&cv, info->volume.channels,
            static_cast<uint32_t>(handler->targetVolume * PA_VOLUME_NORM));
        pa_operation* op = pa_context_set_sink_input_volume(
            handler->context, info->index, &cv, nullptr, nullptr);
        if (op) pa_operation_unref(op);
    }
}

void AudioHandler::setVolumeForApp(const std::string& appName, float volume) {
    if (!connected) return;
    pa_threaded_mainloop_lock(mainloop);
    targetApp = appName;
    hasPID = false;
    targetVolume = volume;
    requestSinkInputs();
    pa_threaded_mainloop_unlock(mainloop);
}

void AudioHandler::setVolumeForPID(uint32_t pid, float volume) {
    if (!connected) return;
    pa_threaded_mainloop_lock(mainloop);
    targetApp.clear();
    targetPID = pid;
    hasPID = true;
    targetVolume = volume;
    requestSinkInputs();
    pa_threaded_mainloop_unlock(mainloop);
}
