#include "PCPanelHandler.h"
#include "AudioHandler.h"
#include "Overlay.h"
#include "WindowUtils.h"
#include "ButtonHandler.h"

int main(int argc, char *argv[]) {
    Overlay overlay;
    AudioHandler audio;
    ButtonHandler button;
    PCPanelHandler panel(PCPanelDevice::Mini);

    // --- Volume Controls (knob indices 0-3, values 0.0-1.0) ---
    panel.setCallback([&](int knob, float vol) {
        switch (knob) {
            case 0: audio.setVolumeForApp("spotify", vol); break;
            case 1: audio.setVolumeForApp("firefox", vol); break;
            case 2: audio.setVolumeForApp("discord", vol); break;
            case 3: audio.setVolumeForPID(getFocusedWindowPID(), vol); break;
        }
        overlay.showVolume(vol);
    });

    // --- Button Controls (button indices 0-3) ---
    panel.setButtonCallback([&](int btn) {
        switch (btn) {
            case 0: button.toggleMediaPlayPause(); break;
            case 1: button.sendKeySequence({"type", "Hello World"}); break; //must be formated for wtype
            case 2: button.forceCloseFocusedWindow(); break;
            case 3: break; // reserved
        }
    });

    panel.startListening();
}
