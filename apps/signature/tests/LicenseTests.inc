// LicenseTests.inc — the studio licence client (shared/studio-license).
//
// The proofs come from tests/LicenseVectors.h, signed by Node's OpenSSL with a
// THROWAWAY P-256 key pair, so the curve arithmetic and the proof rules are checked
// against an independent implementation. The client tests swap the network for a
// scripted server and the clock for a fixed one; nothing here touches the internet
// or the real studio key's private half (which nobody here has).
#include "LicenseVectors.h"
#include "../../../shared/studio-license/LicenseClient.h"

namespace {

juce::MemoryBlock fromHex (const char* h)
{
    juce::MemoryBlock m;
    m.loadFromHexString (h);
    return m;
}

amanorsac::license::Product testProduct()
{
    return { "MIREKU-TEST", "MIRE", "MIREKU-TEST/license/v1",
             "https://example.invalid/buy", "https://example.invalid/account" };
}

/// A scripted licence server: answers with whatever `next` holds, and counts calls.
struct FakeServer
{
    int status = 200;
    juce::String body;
    int calls = 0;
    juce::String lastPath, lastKey, lastDevice;

    amanorsac::license::Client::Transport transport()
    {
        return [this] (const juce::String& path, const juce::var& b) -> std::pair<int, juce::String>
        {
            ++calls; lastPath = path;
            lastKey = b["licenseKey"].toString(); lastDevice = b["deviceKey"].toString();
            return { status, body };
        };
    }
    void answerProof (const char* proof) { status = 200; body = juce::String ("{\"proof\":\"") + proof + "\"}"; }
    void answerError (int s, const juce::String& json) { status = s; body = json; }
    void offline() { status = 0; body = {}; }
};

/// Let the client's background call finish and its answer reach the message thread.
void settle (const amanorsac::license::Client& c)
{
    const auto end = juce::Time::getMillisecondCounter() + 20000u;
    while (c.busy() && juce::Time::getMillisecondCounter() < end) juce::Thread::sleep (2);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
}

} // namespace

static void testLicenseCrypto()
{
    std::printf ("\n== STUDIO LICENCE: proof verification ==\n");
    using namespace amanorsac::license;
    using namespace licensevectors;

    // ---- the curve arithmetic, against OpenSSL
    int good = 0, rejectedTamper = 0, rejectedOtherKey = 0;
    const int n = (int) (sizeof (kRaw) / sizeof (kRaw[0]));
    for (auto& v : kRaw)
    {
        const auto* pub = v.key == 0 ? kPub : kOtherPub;
        const auto* wrong = v.key == 0 ? kOtherPub : kPub;
        auto msg = fromHex (v.msg), sig = fromHex (v.sig);
        if (verifyP256 (msg.getData(), msg.getSize(), sig.getData(), sig.getSize(), pub, 65)) ++good;
        if (! verifyP256 (msg.getData(), msg.getSize(), sig.getData(), sig.getSize(), wrong, 65)) ++rejectedOtherKey;
        static_cast<juce::uint8*> (msg.getData())[0] ^= 0x80;
        if (! verifyP256 (msg.getData(), msg.getSize(), sig.getData(), sig.getSize(), pub, 65)) ++rejectedTamper;
    }
    CHECK (good == n, ("every OpenSSL signature verifies (" + juce::String (good) + "/" + juce::String (n) + ")").toRawUTF8());
    CHECK (rejectedTamper == n, "a message changed by one bit is rejected, every time");
    CHECK (rejectedOtherKey == n, "a signature under another key is rejected, every time");

    // ---- the studio key itself (Standard §5, acceptance A11)
    CHECK (kStudioPublicKey[0] == 0x04 && kStudioPublicKey[1] == 0xcd && kStudioPublicKey[2] == 0xa5
           && kStudioPublicKey[3] == 0x7d && kStudioPublicKey[64] == 0x52, "the studio key is the §5 bytes");
    {
        detail::Curve c;
        CHECK (detail::onCurve (c, detail::fromBytes (kStudioPublicKey + 1, 32), detail::fromBytes (kStudioPublicKey + 33, 32)),
               "and is a point on P-256");
        juce::uint8 offCurve[65];
        std::memcpy (offCurve, kStudioPublicKey, 65);
        offCurve[64] ^= 1;
        auto msg = fromHex (kRaw[1].msg), sig = fromHex (kRaw[1].sig);
        CHECK (! verifyP256 (msg.getData(), msg.getSize(), sig.getData(), sig.getSize(), offCurve, 65),
               "a key that is not on the curve verifies nothing");
    }

    // ---- what a proof means (Standard §5)
    const juce::String dev (kDevice), key (kKey);
    auto at = [&] (const char* proof, juce::int64 now) { return evaluateProof (proof, dev, key, now, kPub, 65); };
    using V = Proof::Verdict;

    auto p = at (kGood, kIssued + kHour);
    CHECK (p.ok(), "a genuine proof for this device and key is licensed");
    CHECK (p.expiresAt == kIssued + 48 * kHour && p.graceUntil == kIssued + 30 * kDay, "and carries its times");
    CHECK (! p.offlineGrace (kIssued + kHour), "inside 48 hours it is fresh");
    CHECK (at (kGood, kIssued + 3 * kDay).ok() && at (kGood, kIssued + 3 * kDay).offlineGrace (kIssued + 3 * kDay),
           "after 48 hours it still works, as offline grace");
    CHECK (at (kGood, kIssued + 30 * kDay).ok(), "on the last instant of grace it still works");
    CHECK (at (kGood, kIssued + 30 * kDay + 1).verdict == V::stale, "one millisecond later it does not (R8)");
    CHECK (at (kGood, kIssued - 47 * kHour).ok(), "a clock up to 48 hours behind is tolerated");
    CHECK (at (kGood, kIssued - 49 * kHour).verdict == V::clockBehind, "a clock wound back further is not");
    CHECK (at (kOtherDevice, kIssued).verdict == V::wrongDevice, "a proof for another device is refused (R5)");
    CHECK (at (kOtherKey, kIssued).verdict == V::wrongDevice, "a proof for another key is refused (R5)");
    CHECK (evaluateProof (kGood, dev, key, kIssued, kOtherPub, 65).verdict == V::badSignature,
           "a proof checked against another key is refused");
    CHECK (at (kForeign, kIssued).verdict == V::badSignature, "a proof signed by anyone else is refused (R3)");
    CHECK (at (kTamperedBody, kIssued).verdict == V::badSignature, "an edited body is refused");
    CHECK (at (kTamperedSig, kIssued).verdict == V::badSignature, "an edited signature is refused");
    CHECK (at (kReserialised, kIssued).verdict == V::badSignature, "the same JSON written differently is refused (bytes, not meaning)");
    CHECK (at (kDer, kIssued).verdict == V::malformed, "a DER signature is refused: raw r||s only");
    CHECK (at (kSTooBig, kIssued).verdict == V::badSignature, "an s at or above the group order is refused");
    CHECK (at (kZeroSig, kIssued).verdict == V::badSignature, "a zero signature is refused");
    for (auto* junk : { "", ".", "abc", "abc.", ".abc", "a.b.c", "!!!.???", "eyJ9.AAAA" })
        CHECK (at (junk, kIssued).verdict == V::malformed, ("junk is malformed: \"" + juce::String (junk) + "\"").toRawUTF8());
    CHECK (evaluateProof (juce::String ("  ") + kGood + "\n", dev, key, kIssued, kPub, 65).ok(), "surrounding whitespace is ignored");

    auto t = at (kTrial, kIssued + kDay);
    CHECK (t.ok() && t.licenseKind == "trial" && t.licenseEndsAt == kIssued + 5 * kDay,
           "a trial proof reads its end and its kind, and ignores a field it does not know");
    CHECK (at (kTrial, kIssued + 5 * kDay + 1).verdict == V::stale, "and stops at its end");

    // ---- keys as people type them (Standard §3)
    CHECK (normaliseKey ("  mire-1a2b-\n3c4d-5e6f ") == "MIRE-1A2B-3C4D-5E6F", "keys are trimmed, unwrapped and upper-cased");
    CHECK (keyLooksRight ("MIRE-1A2B-3C4D-5E6F", "MIRE"), "a MIREKU key is accepted");
    CHECK (! keyLooksRight ("SECO-610E-6070-3493", "MIRE"), "another product's key is refused before anything is sent");
    CHECK (! keyLooksRight ("MIRE-1A2B-3C4D-5E6", "MIRE"), "a short key is refused");
    CHECK (! keyLooksRight ("MIRE-1A2B-3C4D-5E6G", "MIRE"), "a key with a non-hex character is refused");
    CHECK (! keyLooksRight ("MIRE1A2B-3C4D-5E6F0", "MIRE"), "a key without its hyphens is refused");
}

static void testLicenseClient()
{
    std::printf ("\n== STUDIO LICENCE: client behaviour ==\n");
    using namespace amanorsac::license;
    using namespace licensevectors;
    using S = Client::State;

    juce::ScopedJuceInitialiser_GUI gui;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mireku-licence-test-" + juce::Uuid().toString());
    dir.createDirectory();
    dir.getChildFile ("device.id").replaceWithText (kDevice);    // the device the vectors were signed for

    juce::int64 now = kIssued + kHour;
    auto clock = [&now] { return now; };
    FakeServer server;

    {
        Client c (testProduct(), dir, server.transport(), clock, kPub);
        settle (c);
        CHECK (c.state() == S::noKey && ! c.isLicensed() && server.calls == 0, "a fresh install asks for a key and calls nobody (A1)");

        c.activate ("SECO-610E-6070-3493");
        settle (c);
        CHECK (c.state() == S::failed && server.calls == 0 && c.message().contains ("MIRE-"),
               "another product's key is refused here, with the prefix to look for");

        server.answerError (404, "{\"error\":\"no_such_license\"}");
        c.activate ("MIRE-0000-0000-0000");
        settle (c);
        CHECK (c.state() == S::failed && ! c.isLicensed() && c.message().contains ("not found"), "an unknown key says so (A2)");
        CHECK (! dir.getChildFile ("license-proof.dat").exists(), "and nothing is stored as proof");

        server.answerProof (kForeign);
        c.activate (kKey);
        settle (c);
        CHECK (! c.isLicensed() && c.message().contains ("could not be verified"),
               "a 200 whose proof does not verify is NOT reported as activated (R4)");
        CHECK (! dir.getChildFile ("license-proof.dat").exists(), "and is not stored");

        server.answerProof (kGood);
        c.activate (juce::String (" mire-1a2b-3c4d-5e6f "));
        settle (c);
        CHECK (c.state() == S::licensed && c.isLicensed(), "the real key activates, without a restart (A3)");
        CHECK (server.lastPath == "/licenses/activate" && server.lastKey == kKey && server.lastDevice == kDevice,
               "it sent the normalised key and this device's id");
        CHECK (dir.getChildFile ("license-proof.dat").existsAsFile() && dir.getChildFile ("license-key.dat").existsAsFile(),
               "license-proof.dat and license-key.dat now exist");
    }

    {
        server.offline();
        const int before = server.calls;
        now = kIssued + 3 * kDay;
        Client c (testProduct(), dir, server.transport(), clock, kPub);
        CHECK (c.isLicensed(), "reopened offline, it is licensed at once from the stored proof (A5)");
        settle (c);
        CHECK (server.calls == before + 1, "and it tried one heartbeat in the background");
        CHECK (c.isLicensed() && c.state() == S::licensed && c.message().isEmpty(),
               "a failed heartbeat keeps the proof and says nothing (R9)");
        CHECK (c.needsConnection(), "past 48 hours it notes that it should go online soon");
    }

    {
        server.answerError (409, "{\"error\":\"device_limit_reached\",\"max_devices\":2,"
                                 "\"devices\":[{\"device_name\":\"STUDIO-PC\"},{\"device_name\":\"LAPTOP\"}]}");
        now = kIssued + 31 * kDay;
        Client c (testProduct(), dir, server.transport(), clock, kPub);
        settle (c);
        CHECK (! c.isLicensed(), "past grace with no new proof, it stops (R8)");
        CHECK (c.message().contains ("STUDIO-PC") && c.message().contains ("LAPTOP"), "a full licence names the computers using it (A8)");
        CHECK (! dir.getChildFile ("license-proof.dat").exists(), "and the stale proof is gone");
        CHECK (c.key() == kKey, "but the key is kept, to try again");

        now = kIssued + kHour;
        server.answerProof (kGood);
        c.heartbeat();
        settle (c);
        CHECK (c.isLicensed(), "a later successful activation unlocks it again");

        server.answerError (403, "{\"error\":\"license_expired\",\"trial\":true,\"message\":\"Your MIREKU trial ended on 10 November.\"}");
        c.heartbeat();
        settle (c);
        CHECK (! c.isLicensed() && c.message() == "Your MIREKU trial ended on 10 November.",
               "an ended licence locks at once and shows the server's own words");
        CHECK (! dir.getChildFile ("license-proof.dat").exists(), "and its proof is deleted");

        server.answerError (418, "{\"error\":\"a_code_from_the_future\"}");
        c.activate (kKey);
        settle (c);
        CHECK (c.state() == S::failed && c.message().isNotEmpty(), "an unknown error code is a plain failure, not a crash");

        server.answerProof (kGood);
        c.activate (kKey);
        settle (c);
        server.answerError (502, "{\"error\":\"upstream_error\"}");
        c.deactivate();
        settle (c);
        CHECK (c.isLicensed() && c.message().contains ("did not go through"), "a deactivation that fails changes nothing locally");

        server.status = 200; server.body = "{\"ok\":true}";
        c.deactivate();
        settle (c);
        CHECK (server.lastPath == "/licenses/deactivate", "deactivate calls /licenses/deactivate");
        CHECK (c.state() == S::noKey && ! c.isLicensed() && c.key().isEmpty(), "and the activation screen returns (A9)");
        CHECK (! dir.getChildFile ("license-proof.dat").exists() && ! dir.getChildFile ("license-key.dat").exists(),
               "with the key and the proof gone");
    }

    {
        now = kIssued + kHour;
        Client c (testProduct(), dir, server.transport(), clock, kPub);
        server.answerProof (kTrial);
        c.activate (kKey);
        settle (c);
        now = kIssued + kDay + 1;
        CHECK (c.isLicensed() && c.endNote() == "Trial: 4 days left", ("a trial counts its days up (" + c.endNote() + ")").toRawUTF8());
        CHECK (dir.getChildFile ("device.id").loadFileAsString().trim() == kDevice, "the device id never changes");
    }

    // ---- reply parsing on its own
    auto r = parseReply (409, "{\"error\":\"device_limit_reached\",\"max_devices\":3,\"devices\":[{\"device_name\":\"A\"}]}");
    CHECK (r.error == "device_limit_reached" && r.maxDevices == 3 && r.devices.size() == 1, "a 409 reply is read whole");
    CHECK (parseReply (500, "<html>oops</html>").error.isEmpty(), "a non-JSON reply does not crash");
    CHECK (describeFailure (Reply {}, testProduct()).contains ("internet"), "no answer at all speaks of the connection");

    dir.deleteRecursively();
}
