#include "audio.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <cmath>
#include <cstdint>

namespace tf {

namespace {
constexpr int kSampleRate = 48000;
constexpr int kMaxVoices = 24;
constexpr int kQueueSize = 64;  // power of two
constexpr float kTwoPi = 6.28318530718f;

struct Voice {
    Sound sound = Sound::Tick;
    float t = 0.0f;
    bool active = false;
};

inline float sineWave(float freq, float t) { return std::sin(kTwoPi * freq * t); }
inline float squareWave(float freq, float t) { return sineWave(freq, t) >= 0 ? 1.0f : -1.0f; }

// Fast white noise for whooshes/sparkle (audio-thread local state).
inline float noise(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (s & 0xffffff) / 8388607.5f - 1.0f;
}

// Returns the sample for one voice at time t; sets done when the voice ends.
float renderVoice(Sound sound, float t, uint32_t& rng, bool& done) {
    done = false;
    switch (sound) {
        case Sound::Pickup: {
            const float dur = 0.09f;
            if (t >= dur) { done = true; return 0; }
            float env = std::exp(-t * 26.0f);
            float f = 680.0f + 240.0f * (t / dur);
            return 0.22f * env * sineWave(f, t);
        }
        case Sound::Meld: {
            const float dur = 0.24f;
            if (t >= dur) { done = true; return 0; }
            float attack = SDL_min(1.0f, t / 0.012f);
            float env = attack * std::exp(-t * 7.5f);
            float f = 523.25f + (783.99f - 523.25f) * (t / dur);  // C5 -> G5 glide
            float s = 0.7f * sineWave(f, t) + 0.3f * sineWave(2 * f, t);
            return 0.30f * env * s;
        }
        case Sound::Reject: {
            const float dur = 0.22f;
            if (t >= dur) { done = true; return 0; }
            float env = std::exp(-t * 6.0f);
            float f = 150.0f - 60.0f * (t / dur);             // descending buzz
            float vib = 1.0f + 0.05f * sineWave(28.0f, t);
            return 0.20f * env * squareWave(f * vib, t);
        }
        case Sound::Win: {
            const float dur = 0.70f;
            if (t >= dur) { done = true; return 0; }
            static const float notes[4] = {523.25f, 659.25f, 783.99f, 1046.50f};  // C E G C
            int idx = SDL_min(3, int(t / 0.12f));
            float nt = t - idx * 0.12f;
            float env = SDL_min(1.0f, nt / 0.008f) * std::exp(-nt * 6.0f);
            float tone = 0.7f * sineWave(notes[idx], t) + 0.3f * sineWave(2 * notes[idx], t);
            float sparkle = (t > 0.42f) ? 0.15f * std::exp(-(t - 0.42f) * 8.0f) * noise(rng) : 0.0f;
            return 0.26f * env * tone + sparkle;
        }
        case Sound::Tick: {
            const float dur = 0.05f;
            if (t >= dur) { done = true; return 0; }
            float env = std::exp(-t * 60.0f);
            return 0.14f * env * sineWave(1100.0f, t);
        }
        case Sound::Undo: {
            const float dur = 0.26f;
            if (t >= dur) { done = true; return 0; }
            float env = SDL_min(1.0f, t / 0.02f) * std::exp(-t * 7.0f);
            float f = 620.0f - 380.0f * (t / dur);  // downward whoosh
            return 0.18f * env * (0.8f * sineWave(f, t) + 0.2f * noise(rng));
        }
    }
    done = true;
    return 0;
}
}  // namespace

struct Audio::Impl {
    SDL_AudioStream* stream = nullptr;
    Voice voices[kMaxVoices];
    // SPSC ring of pending triggers (main thread -> audio thread).
    Sound queue[kQueueSize];
    std::atomic<unsigned> head{0};
    std::atomic<unsigned> tail{0};
    uint32_t rng = 0x1234567u;

    void drainTriggers() {
        unsigned t = tail.load(std::memory_order_relaxed);
        unsigned h = head.load(std::memory_order_acquire);
        while (t != h) {
            Sound s = queue[t % kQueueSize];
            t++;
            for (auto& v : voices) {
                if (!v.active) { v.active = true; v.sound = s; v.t = 0.0f; break; }
            }
        }
        tail.store(t, std::memory_order_relaxed);
    }

    void mix(float* out, int frames) {
        drainTriggers();
        const float dt = 1.0f / kSampleRate;
        for (int i = 0; i < frames; ++i) {
            float sample = 0.0f;
            for (auto& v : voices) {
                if (!v.active) continue;
                bool done = false;
                sample += renderVoice(v.sound, v.t, rng, done);
                v.t += dt;
                if (done) v.active = false;
            }
            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;
            out[i] = sample;
        }
    }
};

static void SDLCALL audioCallback(void* userdata, SDL_AudioStream* stream,
                                  int additional_amount, int /*total*/) {
    auto* impl = static_cast<Audio::Impl*>(userdata);
    int frames = additional_amount / static_cast<int>(sizeof(float));
    if (frames <= 0) return;
    // Generate in chunks to bound stack usage.
    constexpr int kChunk = 1024;
    float buf[kChunk];
    while (frames > 0) {
        int n = frames < kChunk ? frames : kChunk;
        impl->mix(buf, n);
        SDL_PutAudioStreamData(stream, buf, n * static_cast<int>(sizeof(float)));
        frames -= n;
    }
}

bool Audio::init() {
    impl_ = new Impl();
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    spec.freq = kSampleRate;
    impl_->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                              audioCallback, impl_);
    if (!impl_->stream) {
        SDL_Log("audio: %s", SDL_GetError());
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    SDL_ResumeAudioStreamDevice(impl_->stream);
    return true;
}

void Audio::shutdown() {
    if (impl_ && impl_->stream) SDL_DestroyAudioStream(impl_->stream);
    delete impl_;
    impl_ = nullptr;
}

void Audio::play(Sound s) {
    if (!impl_) return;
    unsigned h = impl_->head.load(std::memory_order_relaxed);
    impl_->queue[h % kQueueSize] = s;
    impl_->head.store(h + 1, std::memory_order_release);
}

void Audio::resume() {
    if (impl_ && impl_->stream) SDL_ResumeAudioStreamDevice(impl_->stream);
}

}  // namespace tf
