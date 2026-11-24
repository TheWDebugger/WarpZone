#pragma once
// Streams the emulator's APU samples to GD's FMOD system as a user-PCM stream.
// Producer: EmuLayer pushes samples after each emulated frame (main thread).
// Consumer: FMOD's mixer thread calls pcmRead() to pull them.
#include <Geode/Geode.hpp>
#include <Geode/fmod/fmod.hpp>
#include <cstdint>
#include <deque>
#include <mutex>

namespace smb {

class AudioOut {
public:
    static constexpr int RATE = 44100;
    // Never let more than ~90 ms pile up, so a hitch can't turn into permanent audio lag.
    static constexpr size_t MAX_QUEUED = 4000;

    static AudioOut& get() { static AudioOut a; return a; }

    bool start(float volume) {
        if (m_chan) return true;
        auto* engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_system) return false;
        auto* sys = engine->m_system;

        { std::lock_guard lk(m_mutex); m_queue.clear(); m_last = 0; }

        FMOD_CREATESOUNDEXINFO ex{};
        ex.cbsize = sizeof(ex);
        ex.length = RATE * 2 * 60 * 60;           // nominal length in bytes; the stream loops forever
        ex.numchannels = 1;
        ex.defaultfrequency = RATE;
        ex.format = FMOD_SOUND_FORMAT_PCM16;
        ex.decodebuffersize = 1024;               // small chunks = low latency
        ex.pcmreadcallback = &AudioOut::pcmRead;

        auto r = sys->createStream(nullptr, FMOD_OPENUSER | FMOD_LOOP_NORMAL | FMOD_2D, &ex, &m_sound);
        if (r != FMOD_OK) { geode::log::warn("WarpZone: createStream failed ({})", (int)r); m_sound = nullptr; return false; }
        r = sys->playSound(m_sound, nullptr, true, &m_chan);
        if (r != FMOD_OK) { geode::log::warn("WarpZone: playSound failed ({})", (int)r); stop(); return false; }
        m_chan->setVolume(volume);
        m_chan->setPaused(false);
        return true;
    }

    void stop() {
        if (m_chan) { m_chan->stop(); m_chan = nullptr; }
        if (m_sound) { m_sound->release(); m_sound = nullptr; }
        std::lock_guard lk(m_mutex);
        m_queue.clear();
    }

    void push(int16_t const* data, size_t n) {
        if (!m_chan || !n) return;
        std::lock_guard lk(m_mutex);
        m_queue.insert(m_queue.end(), data, data + n);
        if (m_queue.size() > MAX_QUEUED) m_queue.erase(m_queue.begin(), m_queue.end() - MAX_QUEUED);
    }

private:
    static FMOD_RESULT F_CALL pcmRead(FMOD_SOUND*, void* data, unsigned int datalen) {
        auto& self = get();
        auto* out = static_cast<int16_t*>(data);
        size_t n = datalen / sizeof(int16_t);
        std::lock_guard lk(self.m_mutex);
        for (size_t i = 0; i < n; i++) {
            if (!self.m_queue.empty()) { self.m_last = self.m_queue.front(); self.m_queue.pop_front(); }
            else self.m_last = (int16_t)(self.m_last * 0.9f); // underrun: fade out instead of clicking
            out[i] = self.m_last;
        }
        return FMOD_OK;
    }

    std::mutex m_mutex;
    std::deque<int16_t> m_queue;
    int16_t m_last = 0;
    FMOD::Sound* m_sound = nullptr;
    FMOD::Channel* m_chan = nullptr;
};

} // namespace smb
