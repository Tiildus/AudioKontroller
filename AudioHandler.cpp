#include "AudioHandler.h"
#include "Logger.h"
#include <thread>
#include <chrono>

AudioHandler::AudioHandler() {
    mainloop = pa_mainloop_new();
    context = pa_context_new(pa_mainloop_get_api(mainloop), "AudioKontroller");

    pa_context_set_state_callback(context, &AudioHandler::contextStateCallback, this);
    pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr);

    // Start PulseAudio mainloop in a background thread
    std::thread([this]() {
        int ret;
        pa_mainloop_run(mainloop, &ret);
    }).detach();

    // Wait until the context is ready
    while (true) {
        pa_context_state_t state = pa_context_get_state(context);
        if (state == PA_CONTEXT_READY) break;
        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
            Logger::instance().error("Audio", "PulseAudio connection failed");
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    Logger::instance().info("Audio", "Connected to PulseAudio");
}

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

void AudioHandler::contextStateCallback(pa_context*, void*) {
    // State transitions are handled synchronously in the constructor
}

void AudioHandler::requestSinkInputs() {
    pa_operation* op = pa_context_get_sink_input_info_list(
        context, &AudioHandler::sinkInputInfoCallback, this);

    if (!op) {
        Logger::instance().error("Audio", "Failed to get sink input list");
        return;
    }

    if (op)
        pa_operation_unref(op);
}

void AudioHandler::sinkInputInfoCallback(pa_context*, const pa_sink_input_info* info, int eol, void* userdata) {
    if (eol != 0 || !info) return;

    auto* handler = static_cast<AudioHandler*>(userdata);
    const pa_proplist* props = info->proplist;

    const char* pidStr = pa_proplist_gets(props, "application.process.id");
    std::optional<uint32_t> pid;
    if (pidStr) pid = std::stoi(pidStr);

    const char* appBin = pa_proplist_gets(props, "application.process.binary");
    if (!appBin) appBin = pa_proplist_gets(props, "media.name");
    std::string appName = appBin ? appBin : "";

    bool match = false;
    if (!handler->targetApp.empty() && handler->targetApp == appName)
        match = true;
    else if (handler->targetPID && pid && handler->targetPID.value() == pid.value())
        match = true;

    if (match && handler->applyVolume) {
        handler->setVolume(info->index, handler->targetVolume);
    }
}

void AudioHandler::setVolume(uint32_t index, float volume) {
    pa_cvolume cv;
    pa_cvolume_set(&cv, 2, static_cast<uint32_t>(volume * PA_VOLUME_NORM));

    pa_operation* op = pa_context_set_sink_input_volume(context, index, &cv, nullptr, nullptr);
    if (op) {
        if (op)
            pa_operation_unref(op);
    }
}

void AudioHandler::setVolumeForApp(const std::string& appName, float volume) {
    targetApp = appName;
    targetPID.reset();
    targetVolume = volume;
    applyVolume = true;
    requestSinkInputs();
}

void AudioHandler::setVolumeForPID(uint32_t pid, float volume) {
    targetApp.clear();
    targetPID = pid;
    targetVolume = volume;
    applyVolume = true;
    requestSinkInputs();
}
