// VelocityResponse.inc — the two-stage velocity response (engine/Sampler.h).
//
// Stage one (CURVE) runs BEFORE layer selection and stage two (DEPTH/OFFSET)
// runs after, so the tests check both that each stage does what it claims AND
// that the neutral settings are bit-for-bit the old behaviour — that last one
// is the promise to every preset already saved on the artist's machine.

static void testVelocityCurveShapes()
{
    std::printf ("\n== VELOCITY (1) the curve shapes the played velocity ==\n");
    using namespace engine_math;

    bool identity = true;
    for (int v = 1; v <= 127; ++v) identity &= shapeVelocity (v, VelCurve::normal) == v;
    CHECK (identity, "NORMAL is the identity, so an untouched install is unchanged");

    // Soft gives MORE for the same effort, hard gives less. Checked across the
    // whole range rather than at one convenient point.
    bool softUp = true, hardDown = true;
    for (int v = 2; v <= 126; ++v)
    {
        softUp   &= shapeVelocity (v, VelCurve::soft) >= v;
        hardDown &= shapeVelocity (v, VelCurve::hard) <= v;
    }
    CHECK (softUp, "SOFT >= NORMAL everywhere in between");
    CHECK (hardDown, "HARD <= NORMAL everywhere in between");
    CHECK (kVelCurveCount == 5, "five curves, the same five as the client's Montage");
    CHECK (velCurveFromId ("soft1") == VelCurve::soft && velCurveFromId ("hard2") == VelCurve::hard,
           "a settings.json from the seven-curve first cut still maps onto the five");

    // WIDE is the only one that bends both ways: down low, up high. That is
    // precisely what an exponent cannot do, which is why it has its own branch.
    CHECK (shapeVelocity (32, VelCurve::wide) < 32 && shapeVelocity (96, VelCurve::wide) > 96,
           "WIDE pushes soft playing softer and hard playing harder");

    CHECK (shapeVelocity (1, VelCurve::wide) >= 1 && shapeVelocity (127, VelCurve::wide) <= 127,
           "WIDE still lands inside 1..127 at both ends");

    bool fixedOk = true;
    for (int v = 1; v <= 127; ++v) fixedOk &= shapeVelocity (v, VelCurve::fixed, 77) == 77;
    CHECK (fixedOk, "FIXED ignores the played velocity entirely");

    // Every curve must stay a legal MIDI velocity, because the shaped number is
    // what picks the sample layer. A 0 here would select nothing and the note
    // would silently not sound.
    bool inRange = true;
    for (int c = 0; c < kVelCurveCount; ++c)
        for (int v = 1; v <= 127; ++v)
        {
            const int y = shapeVelocity (v, (VelCurve) c, 100);
            inRange &= (y >= 1 && y <= 127);
        }
    CHECK (inRange, "every curve returns a legal 1..127 velocity for every input");

    // Monotonic: harder playing can never produce a quieter result, or the
    // keyboard feels broken. (FIXED is flat by definition and excluded.)
    bool mono = true;
    for (int c = 0; c < kVelCurveCount; ++c)
    {
        if ((VelCurve) c == VelCurve::fixed) continue;
        for (int v = 2; v <= 127; ++v)
            mono &= shapeVelocity (v, (VelCurve) c) >= shapeVelocity (v - 1, (VelCurve) c);
    }
    CHECK (mono, "no curve ever goes backwards as you play harder");

    bool ids = true;
    for (int c = 0; c < kVelCurveCount; ++c)
        ids &= (velCurveFromId (velCurveId ((VelCurve) c)) == (VelCurve) c);
    CHECK (ids, "curve ids round-trip, so settings.json survives a restart");
    CHECK (velCurveFromId ("nonsense") == VelCurve::normal,
           "an unknown id falls back to NORMAL rather than to garbage");
}

static void testVelocityDepthAndOffset()
{
    std::printf ("\n== VELOCITY (2) depth and offset shape the level ==\n");
    using namespace engine_math;

    bool neutral = true;
    for (int v = 0; v <= 127; ++v) neutral &= levelVelocity (v, 64, 64) == v;
    CHECK (neutral, "64/64 is neutral: every preset saved before this sounds the same");

    // Yamaha publishes no equation, only worked examples. All five of the ones
    // their documentation describes are asserted here; between them they pin
    // both the Depth scale and the factor of two on Offset.
    CHECK (levelVelocity (64, 127, 64) >= 127,
           "Depth 127: full output arrives at velocity 64, i.e. half the effort");
    bool silentBelow64 = true, soundsAbove64 = true;
    for (int v = 0; v <= 64; ++v)  silentBelow64 &= levelVelocity (v, 127, 0) == 0;
    for (int v = 70; v <= 127; ++v) soundsAbove64 &= levelVelocity (v, 127, 0) > 0;
    CHECK (silentBelow64 && soundsAbove64,
           "Depth 127 / Offset 0: silent until velocity 64, then rises - Yamaha's crossfade case");
    CHECK (levelVelocity (40, 64, 86) == 84 && levelVelocity (100, 64, 86) >= 127,
           "Offset 86: full output for very little effort");
    bool neverTops = true;
    for (int v = 0; v <= 127; ++v) neverTops &= levelVelocity (v, 64, 49) < 127;
    CHECK (neverTops, "Offset 49: the part never reaches the top of its range");

    bool clamped = true;
    for (int d = 0; d <= 127; d += 7)
        for (int o = 0; o <= 127; o += 7)
            for (int v = 0; v <= 127; v += 7)
            {
                const int y = levelVelocity (v, d, o);
                clamped &= (y >= 0 && y <= 127);
            }
    CHECK (clamped, "no combination of depth and offset can leave 0..127");

    CHECK (levelVelocity (100, 0, 64) == 0,
           "Depth 0 silences the part rather than fixing it at full, as the name implies");
}

static void testVelocityStateAndPresetRoundTrip()
{
    std::printf ("\n== VELOCITY (3) what is saved, and what deliberately is not ==\n");
    AppState a;
    a.slots[1].vel.depth = 101;
    a.slots[1].vel.offset = 21;
    AppState b;
    b.slotFromVar (1, a.slotToVar (1));
    CHECK (b.slots[1].vel.depth == 101 && b.slots[1].vel.offset == 21,
           "depth and offset survive a slot save/load, so they live in a performance");

    // The promise to existing installs: a slot written before this feature
    // existed has no "velocity" object at all and must come back neutral.
    auto v = a.slotToVar (0);
    v.getDynamicObject()->removeProperty ("velocity");
    AppState c;
    c.slotFromVar (0, v);
    CHECK (c.slots[0].vel.depth == 64 && c.slots[0].vel.offset == 64,
           "a slot saved before velocity response existed loads neutral, i.e. unchanged");

    CHECK (AppState().slots[0].vel.depth == 64 && AppState().slots[0].vel.offset == 64,
           "a fresh slot starts neutral");
}
