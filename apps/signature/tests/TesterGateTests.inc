// TesterGateTests.inc — the test-build key gate.
//
// Ported from apps/lowend/tests/LowEndTests.cpp, which exercises the same
// verification code (now shared/tester-gate/TesterGate.h). The tokens come from
// tests/TesterVectors.h and were signed by Node's crypto with a throwaway key
// pair, so this checks the C++ against an INDEPENDENT implementation of
// RSASSA-PKCS1-v1_5 rather than against itself.
#include "TesterVectors.h"

static void testTesterGate()
{
    std::printf ("\n== TEST-BUILD KEY GATE ==\n");
    using namespace amanorsac::tester;
    constexpr const char* kProduct = "mireku-tester";
    using namespace testervectors;
    const juce::String mod (kModulus), otherMod (kOtherModulus);
    const auto now = kIssuedMs + 3 * kDayMs;      // three days after issue

    // ---- a good key
    auto c = evaluate (kGood, now, 0, mod, kProduct);
    CHECK (c.status == Status::ok, "a good key is accepted");
    CHECK (c.daysLeft == 27, "and reports 27 days left three days into 30 (" + juce::String (c.daysLeft) + ")");
    CHECK (c.expiresMs == kGoodExpiresMs, "and reports its expiry");

    // ---- how it may be written down
    juce::String wrapped;
    for (int i = 0; i < juce::String (kGood).length(); ++i)
    {
        wrapped += juce::String (kGood).substring (i, i + 1);
        if (i % 60 == 59) wrapped += "\r\n  ";      // an email client wrapping the line
    }
    CHECK (evaluate (wrapped, now, 0, mod, kProduct).status == Status::ok, "line breaks and spaces from an email are ignored");
    CHECK (evaluate ("  " + juce::String (kGood) + "\n", now, 0, mod, kProduct).status == Status::ok, "so is padding");

    // ---- days left, at the edges
    CHECK (evaluate (kGood, kGoodExpiresMs - 1, 0, mod, kProduct).daysLeft == 1, "one millisecond before expiry still counts as a day");
    CHECK (evaluate (kGood, kGoodExpiresMs, 0, mod, kProduct).status == Status::expired, "at the expiry instant it is expired");
    CHECK (evaluate (kGood, kIssuedMs, 0, mod, kProduct).daysLeft == 30, "on the day of issue, 30 days");

    // ---- what must NOT open it
    CHECK (evaluate ("", now, 0, mod, kProduct).status == Status::none, "an empty box is 'nothing entered'");
    for (auto* junk : { "abc", "a.b", ".", "..", "abc.", ".abc", "a.b.c", "!!!.???", "hello world" })
        CHECK (evaluate (junk, now, 0, mod, kProduct).status == Status::malformed, juce::String ("not a key: ") + junk);
    CHECK (evaluate (juce::String::repeatedString ("A", 900), now, 0, mod, kProduct).status == Status::malformed, "a long run of nonsense is not a key");

    const juce::String good (kGood);
    const int dot = good.indexOfChar ('.');
    auto flip = [] (juce::String s, int at)
    {
        const auto c = s[at];
        return s.replaceSection (at, 1, juce::String::charToString (c == 'A' ? 'B' : 'A'));
    };
    CHECK (evaluate (flip (good, 5), now, 0, mod, kProduct).status == Status::badSignature, "changing one character of the body breaks it");
    CHECK (evaluate (flip (good, dot + 40), now, 0, mod, kProduct).status == Status::badSignature, "changing one character of the signature breaks it");
    CHECK (evaluate (good.substring (0, good.length() - 4), now, 0, mod, kProduct).status == Status::malformed, "a key cut short is refused");
    CHECK (evaluate (good.substring (dot + 1) + "." + good.substring (0, dot), now, 0, mod, kProduct).status != Status::ok, "body and signature swapped is refused");
    CHECK (evaluate (kGood, now, 0, otherMod, kProduct).status == Status::badSignature, "a key checked against the wrong public key is refused");
    CHECK (evaluate (kSignedByOther, now, 0, mod, kProduct).status == Status::badSignature, "a well-formed key signed by anyone else is refused");

    // ---- signed by the studio, but not a usable key
    CHECK (evaluate (kWrongProduct, now, 0, mod, kProduct).status == Status::wrongProduct, "a genuine signature for another product is refused");
    CHECK (evaluate (kNoDates, now, 0, mod, kProduct).status == Status::malformed, "a key with no dates is refused");
    CHECK (evaluate (kBackwardsDates, now, 0, mod, kProduct).status == Status::malformed, "a key that expires before it begins is refused");

    // ---- time
    c = evaluate (kExpired, now, 0, mod, kProduct);
    CHECK (c.status == Status::expired && c.expiresMs == kExpiredExpiresMs, "an expired key says so, and when");
    CHECK (evaluate (kFromFuture, now, 0, mod, kProduct).status == Status::clockBehind, "a key issued in the far future means the clock is wrong");
    CHECK (evaluate (kLong, now, 0, mod, kProduct).status == Status::ok, "a far-future expiry is fine");
    // The clock is wound back after the key has expired: the remembered time wins.
    CHECK (evaluate (kGood, kGoodExpiresMs - kDayMs, kGoodExpiresMs + kDayMs, mod, kProduct).status == Status::expired,
           "winding the clock back does not revive an expired key");

    // ---- the stored key and the remembered time
    auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                  .getChildFile ("MirekuTester-" + juce::Uuid().toDashedString());
    juce::int64 fake = now;
    auto clock = [&fake] { return fake; };
    {
        Gate g (temp, mod, kProduct, clock);
        CHECK (g.current().status == Status::none, "a new machine has no key");
        CHECK (g.submit ("nonsense").status == Status::malformed, "a bad key is refused");
        CHECK (! temp.getChildFile ("tester-key.txt").existsAsFile(), "and is not stored");
        CHECK (g.submit (kExpired).status == Status::expired, "an expired key is refused");
        CHECK (g.current().status == Status::none, "and does not unlock anything");
        CHECK (g.submit (kGood).status == Status::ok && g.current().status == Status::ok, "a good key unlocks");
    }
    {
        Gate g (temp, mod, kProduct, clock);
        CHECK (g.current().status == Status::ok, "and is still unlocked after a restart");
        CHECK (g.submit ("nonsense").status == Status::malformed && g.current().status == Status::ok,
               "a bad key typed later does not lock a working one out");
        fake = kGoodExpiresMs + kDayMs;
        CHECK (g.refresh().status == Status::expired, "it locks when the key expires while MIREKU is open");
    }
    {
        fake = kGoodExpiresMs - 5 * kDayMs;                        // someone winds the clock back to before expiry
        Gate g (temp, mod, kProduct, clock);
        CHECK (g.current().status == Status::expired, "restarting with the clock wound back is still locked");
        CHECK (g.refresh().status == Status::expired, "and stays locked");
    }
    temp.getChildFile ("tester-seen.txt").replaceWithText ("garbage that is not a number");
    {
        fake = now;
        Gate g (temp, mod, kProduct, clock);                                 // must not crash on a damaged file
        CHECK (g.current().status == Status::ok, "a damaged remembered-time file is treated as no history");
    }
    temp.getChildFile ("tester-key.txt").replaceWithText ("");
    {
        Gate g (temp, mod, kProduct, clock);
        CHECK (g.current().status == Status::none, "an empty stored key is treated as no key");
    }
    temp.deleteRecursively();
}
