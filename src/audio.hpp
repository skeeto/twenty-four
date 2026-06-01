#pragma once

namespace tf {

enum class Sound { Pickup, Meld, Reject, Win, Tick, Undo };

// Procedurally-synthesised sound effects. No asset files: every effect is
// generated on the audio thread from oscillators + envelopes, so it behaves
// identically on desktop and in the browser.
class Audio {
public:
    bool init();
    void shutdown();
    void play(Sound s);  // lock-free; safe to call from the main thread
    void resume();       // unlock playback after a user gesture (web autoplay)

    struct Impl;  // defined in audio.cpp; public so the audio callback can reach it

private:
    Impl* impl_ = nullptr;
};

}  // namespace tf
