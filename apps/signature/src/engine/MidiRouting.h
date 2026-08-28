// MidiRouting.h — CLIENT REVIEW ROUND 4 (item 1).
//
// "Each MIDI input triggers a different module, like Omnisphere."
//
// The two build types differ fundamentally, so ONE routing model is defined
// here and both paths feed it:
//
//   * STANDALONE — the app can hold several MIDI input devices open at once,
//     and the useful question is "which DEVICE plays which slot". We own the
//     `juce::MidiInput` objects (see MidiDeviceRouter below) because JUCE's
//     StandalonePluginHolder merges every enabled input into one buffer and
//     throws the device identity away.
//
//   * PLUGIN (VST3/AU) — the host hands us ONE merged MIDI stream and there are
//     no devices to choose. The equivalent question is "which CHANNEL plays
//     which slot" (ch 1 -> A, ch 2 -> B, ... or Omni).
//
// The common currency is a 4-bit SLOT MASK (bit i = Perform slot A..D). Every
// message that reaches the engine carries one, and the effective mask is
//        deviceMask (standalone only) AND channelMask[channel]
// so the two models compose instead of competing. Defaults are 0b1111 (Omni)
// everywhere, which is byte-for-byte the pre-round-4 behaviour.
//
// Thread-safety law: the audio thread only ever reads `std::atomic<uint8_t>`
// masks and pops fixed-size PODs out of a lock-free FIFO. Nothing here
// allocates, locks or blocks on the audio (or the MIDI input) thread.
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace jm {

static constexpr juce::uint8 kSlotMaskAll = 0x0F;

//==============================================================================
/// A 3-byte MIDI event plus the slot mask it was routed to. POD, so the FIFO
/// below never allocates. (Sysex is not routed — nothing in the engine reads it.)
struct RoutedMidiEvent
{
    juce::uint8 bytes[3] { 0, 0, 0 };
    juce::uint8 size = 0;
    juce::uint8 mask = kSlotMaskAll;
    /// 1 = the mask is final; do NOT also apply the channel table. Used by the
    /// on-screen keyboard and wheels, which are part of the instrument rather
    /// than an external source and must always reach every routed slot.
    juce::uint8 resolved = 0;

    juce::MidiMessage toMessage() const
    {
        return size == 1 ? juce::MidiMessage (bytes[0])
             : size == 2 ? juce::MidiMessage (bytes[0], bytes[1])
                         : juce::MidiMessage (bytes[0], bytes[1], bytes[2]);
    }
    static bool fromMessage (const juce::MidiMessage& m, juce::uint8 mask, bool isResolved,
                             RoutedMidiEvent& out)
    {
        const int n = m.getRawDataSize();
        if (n < 1 || n > 3) return false;
        const auto* d = m.getRawData();
        for (int i = 0; i < n; ++i) out.bytes[i] = d[i];
        out.size = (juce::uint8) n;
        out.mask = mask;
        out.resolved = isResolved ? 1 : 0;
        return true;
    }
};

//==============================================================================
/// Lock-free single-producer(-per-source) queue of already-routed events.
/// Producers: the MIDI input threads (standalone) and the message thread (the
/// on-screen keyboard / pitch-bend / mod wheel). Consumer: the audio thread.
/// The `AbstractFifo` write is guarded by a spin flag so several MIDI threads
/// cannot interleave a write; the audio thread never takes it.
class RoutedMidiQueue
{
public:
    static constexpr int kCapacity = 1024;

    void push (const juce::MidiMessage& m, juce::uint8 mask, bool resolved = false)
    {
        RoutedMidiEvent e;
        if (! RoutedMidiEvent::fromMessage (m, mask, resolved, e)) return;
        // producer-side mutual exclusion only (never entered by the audio thread)
        while (writing_.exchange (true, std::memory_order_acquire)) { /* other MIDI thread */ }
        int s1, n1, s2, n2;
        fifo_.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) events_[(size_t) s1] = e;
        fifo_.finishedWrite (n1);
        writing_.store (false, std::memory_order_release);
    }

    /// Audio thread: drain everything queued since the last block.
    template <typename Fn>
    void drain (Fn&& fn)
    {
        int s1, n1, s2, n2;
        fifo_.prepareToRead (fifo_.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) fn (events_[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) fn (events_[(size_t) (s2 + i)]);
        fifo_.finishedRead (n1 + n2);
    }

    void reset() { fifo_.reset(); }

private:
    juce::AbstractFifo fifo_ { kCapacity };
    std::array<RoutedMidiEvent, kCapacity> events_ {};
    std::atomic<bool> writing_ { false };
};

//==============================================================================
/// The persistent routing model: 16 channel masks + a per-device-identifier
/// mask table. Message thread writes, audio/MIDI threads read atomically.
class MidiRouting
{
public:
    struct DeviceRoute
    {
        juce::String identifier, name;
        bool enabled = false;
        juce::uint8 mask = kSlotMaskAll;
    };

    MidiRouting() { for (auto& c : channelMask_) c.store (kSlotMaskAll); }

    // ---------------------------------------------------------- channel model
    juce::uint8 channelMask (int channel1to16) const
    {
        const int i = juce::jlimit (1, 16, channel1to16) - 1;
        return channelMask_[(size_t) i].load (std::memory_order_relaxed);
    }
    void setChannelMask (int channel1to16, juce::uint8 mask)
    {
        const int i = juce::jlimit (1, 16, channel1to16) - 1;
        channelMask_[(size_t) i].store ((juce::uint8) (mask & kSlotMaskAll), std::memory_order_relaxed);
    }
    bool channelsAreOmni() const
    {
        for (auto& c : channelMask_) if (c.load() != kSlotMaskAll) return false;
        return true;
    }
    void resetChannelsToOmni() { for (auto& c : channelMask_) c.store (kSlotMaskAll); }

    // ----------------------------------------------------------- device model
    /// Message thread only. Devices are keyed by JUCE device identifier so the
    /// routing survives re-plugging and reboots.
    std::vector<DeviceRoute>& devices() { return devices_; }
    const std::vector<DeviceRoute>& devices() const { return devices_; }

    DeviceRoute* findDevice (const juce::String& identifier)
    {
        for (auto& d : devices_) if (d.identifier == identifier) return &d;
        return nullptr;
    }

    /// Merge the machine's current MIDI inputs into the table, keeping any
    /// stored routing for devices that are not plugged in right now.
    void refreshDeviceList()
    {
        for (auto& info : juce::MidiInput::getAvailableDevices())
        {
            if (auto* d = findDevice (info.identifier)) { d->name = info.name; continue; }
            devices_.push_back ({ info.identifier, info.name, false, kSlotMaskAll });
        }
    }

    // ------------------------------------------------------------ persistence
    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        juce::Array<juce::var> ch;
        for (auto& c : channelMask_) ch.add ((int) c.load());
        o->setProperty ("channels", ch);
        juce::Array<juce::var> devs;
        for (auto& d : devices_)
        {
            auto* dv = new juce::DynamicObject();
            dv->setProperty ("id", d.identifier);
            dv->setProperty ("name", d.name);
            dv->setProperty ("enabled", d.enabled);
            dv->setProperty ("mask", (int) d.mask);
            devs.add (juce::var (dv));
        }
        o->setProperty ("devices", devs);
        return juce::var (o);
    }

    void fromVar (const juce::var& v)
    {
        if (! v.isObject()) return;
        if (auto* ch = v.getProperty ("channels", juce::var()).getArray())
            for (int i = 0; i < juce::jmin (16, ch->size()); ++i)
                channelMask_[(size_t) i].store ((juce::uint8) ((int) ch->getReference (i) & kSlotMaskAll));
        if (auto* devs = v.getProperty ("devices", juce::var()).getArray())
        {
            for (auto& dv : *devs)
            {
                DeviceRoute d;
                d.identifier = dv.getProperty ("id", "").toString();
                d.name = dv.getProperty ("name", "").toString();
                d.enabled = (bool) dv.getProperty ("enabled", false);
                d.mask = (juce::uint8) ((int) dv.getProperty ("mask", (int) kSlotMaskAll) & kSlotMaskAll);
                if (d.identifier.isEmpty()) continue;
                if (auto* e = findDevice (d.identifier)) *e = d;
                else devices_.push_back (d);
            }
        }
    }

private:
    std::array<std::atomic<juce::uint8>, 16> channelMask_;
    std::vector<DeviceRoute> devices_;
};

//==============================================================================
/// STANDALONE ONLY — owns one `juce::MidiInput` per enabled device and stamps
/// every incoming message with that device's slot mask before pushing it into
/// the routed queue. This is the piece JUCE's StandalonePluginHolder cannot
/// give us: it merges all enabled inputs into a single buffer, discarding the
/// device identity the client's request depends on.
///
/// Plugin builds never construct one — a plugin must not grab MIDI hardware.
class MidiDeviceRouter
{
public:
    /// Installed by the standalone app so we can take a device away from
    /// StandalonePluginHolder before opening it ourselves (otherwise the same
    /// notes would arrive twice: once through the holder, once through us).
    static std::function<void (const juce::String&)>& releaseFromHolderHook()
    {
        static std::function<void (const juce::String&)> hook;
        return hook;
    }
    /// True once the standalone app has claimed ownership of MIDI input.
    static std::atomic<bool>& active()
    {
        static std::atomic<bool> a { false };
        return a;
    }

    explicit MidiDeviceRouter (RoutedMidiQueue& q) : queue_ (q) {}
    ~MidiDeviceRouter() { closeAll(); }

    /// Message thread: bring the open devices in line with `routing`.
    void sync (MidiRouting& routing)
    {
        // close anything no longer wanted
        for (auto it = open_.begin(); it != open_.end();)
        {
            auto* r = routing.findDevice ((*it)->identifier);
            if (r == nullptr || ! r->enabled) { (*it)->input.reset(); it = open_.erase (it); }
            else { (*it)->mask.store (r->mask); ++it; }
        }
        // open anything newly wanted. The holder re-enables every input it sees
        // whenever the device list changes (hot-plug), so the release hook is
        // called on EVERY sync for the devices we own, not only on first open.
        for (auto& d : routing.devices())
        {
            if (! d.enabled) continue;
            if (auto& rh = releaseFromHolderHook()) rh (d.identifier);
            bool already = false;
            for (auto& o : open_) if (o->identifier == d.identifier) already = true;
            if (already) continue;
            auto slot = std::make_unique<OpenDevice> (queue_, d.identifier, d.mask);
            slot->input = juce::MidiInput::openDevice (d.identifier, slot.get());
            if (slot->input == nullptr) continue;                 // unplugged / in use
            slot->input->start();
            open_.push_back (std::move (slot));
        }
    }

    void closeAll() { open_.clear(); }
    int numOpen() const { return (int) open_.size(); }

private:
    struct OpenDevice : juce::MidiInputCallback
    {
        OpenDevice (RoutedMidiQueue& q, juce::String id, juce::uint8 m)
            : queue (q), identifier (std::move (id)), mask (m) {}
        // MIDI input thread: atomic read + lock-free push, nothing else.
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
        {
            queue.push (m, mask.load (std::memory_order_relaxed));
        }
        RoutedMidiQueue& queue;
        juce::String identifier;
        std::atomic<juce::uint8> mask;
        std::unique_ptr<juce::MidiInput> input;
    };

    RoutedMidiQueue& queue_;
    std::vector<std::unique_ptr<OpenDevice>> open_;
};

} // namespace jm
