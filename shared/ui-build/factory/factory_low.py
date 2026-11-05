# Low End (bass) factory presets. Amp models: 0 Studio Clean, 1 Classic Tube,
# 2 Flip Top, 3 Monolith. Cab: 0 1x15, 1 2x10, 2 4x10, 3 8x10, 4 DI.
# Graphic EQ bands: g50 g120 g400 g800 g4k. Synth waves 0..3; route 1 = after the amp.
CATS = ['Worship', 'Clean', 'Drive', 'Rock', 'Funk', 'Slap', 'Metal', 'Gospel', 'Reggae', 'Jazz', 'Synth', 'Ambient', 'Strings']
COL = {'Worship': '3FA9A0', 'Clean': '2E7DF7', 'Drive': 'E2574C', 'Rock': 'C0392B', 'Funk': 'B2D235', 'Slap': '2BC4C0',
       'Metal': '3B3D40', 'Gospel': 'D08B4F', 'Reggae': '27AE60', 'Jazz': '8E6F52', 'Synth': 'E056A0', 'Ambient': '6EC1E4', 'Strings': '8A3A1F'}

def gate(th=-65, rel=120): return ('noiseGate', {'threshold': th, 'release': rel})
def comp(s=50, tone=50, lvl=50, mix=100): return ('optoComp', {'sustain': s, 'tone': tone, 'level': lvl, 'mix': mix})
def fet(th=-18, ratio=1): return ('fetComp', {'threshold': th, 'ratio': ratio})
def od(d=35, t=50, l=50, bl=100): return ('bassOd', {'drive': d, 'tone': t, 'level': l, 'blend': bl})
def tube(d=40, t=55, l=55, tb=0): return ('tubeOd', {'drive': d, 'tone': t, 'level': l, 'tube': tb})
def fuzz(f=60, t=50, l=40, g=20, bl=100): return ('fuzz', {'fuzz': f, 'tone': t, 'level': l, 'gate': g, 'blend': bl})
def boost(g=6, p=50, low=0): return ('boost', {'gain': g, 'presence': p, 'low': low})
def geq(a=0, b=0, c=0, d=0, e=0, lvl=0): return ('graphicEq', {'g50': a, 'g120': b, 'g400': c, 'g800': d, 'g4k': e, 'level': lvl})
def env(s=55, r=60, q=55, up=0): return ('envFilter', {'sens': s, 'range': r, 'q': q, 'direction': up})
def octv(s1=60, s2=0, dry=100, tone=50, growl=25): return ('octaver', {'sub1': s1, 'sub2': s2, 'dry': dry, 'tone': tone, 'growl': growl})
def chorus(rate=.5, depth=50, mix=35, mode=0, width=80): return ('bassChorus', {'rate': rate, 'depth': depth, 'mix': mix, 'mode': mode, 'width': width})
def flanger(rate=.2, depth=50, fb=30, man=2.0): return ('flanger', {'rate': rate, 'depth': depth, 'feedback': fb, 'manual': man})
def phaser(rate=.3, depth=60, fb=40, st=1): return ('phaser', {'rate': rate, 'depth': depth, 'feedback': fb, 'stages': st})
def trem(rate=4.5, depth=50, shape=0, spread=70): return ('tremolo', {'rate': rate, 'depth': depth, 'shape': shape, 'spread': spread})
def peq(bass=0, mf=800, mg=0, treb=0): return ('paraEq', {'bass': bass, 'midFreq': mf, 'midGain': mg, 'treble': treb})
def tape(div=4, fb=35, mix=25, wow=25, tone=5000, pp=0): return ('analogDelay', {'syncDivision': div, 'feedback': fb, 'mix': mix, 'wowFlutter': wow, 'tone': tone, 'pingpong': pp})
def dig(l=4, r=4, fb=30, mix=20, lp=20000, hp=20): return ('digitalDelay', {'syncDivisionL': l, 'syncDivisionR': r, 'feedback': fb, 'mix': mix, 'filterLp': lp, 'filterHp': hp})
def plate(size=40, decay=1.8, mix=15, pre=20, damp=50): return ('roomVerb', {'size': size, 'decay': decay, 'mix': mix, 'predelay': pre, 'damp': damp})
def bow(sw=55, vib=35, body=60, rosin=20, voice=0, lvl=50, mode=0): return ('bow', {'swell': sw, 'vibrato': vib, 'body': body, 'rosin': rosin, 'voice': voice, 'level': lvl, 'mode': mode})
def hall(size=70, decay=4.0, shim=0, mix=25, mod=30): return ('ambientVerb', {'size': size, 'decay': decay, 'shimmer': shim, 'mix': mix, 'mod': mod})

def amp(model, gain, **kw): d = {'model': model, 'gain': gain}; d.update(kw); return d
def cab(model, **kw): d = {'model': model}; d.update(kw); return d
def synth(wave=0, cutoff=900, reso=30, env=45, sub=40, level=-8, route=1, **kw):
    d = {'on': 1, 'wave': wave, 'cutoff': cutoff, 'reso': reso, 'envAmt': env, 'sub': sub, 'level': level, 'route': route}; d.update(kw); return d

def P(name, cat, pre, post, a, c, key=None, col=None, **extra):
    p = {'name': name, 'cat': cat, 'pre': pre, 'post': post, 'amp': a, 'cab': c, 'col': col or COL[cat]}
    if key: p['key'] = key
    p.update(extra)
    return p

PRESETS = [
    # ---- the first library (permanent keys from v1) --------------------------------
    P('Studio Clean', 'Clean', [gate(), comp(s=45)], [], amp(0, 30, bass=58, mid=52, treble=55), cab(2), key='v1-studio-clean', col='2E7DF7'),
    P('Fingerstyle Warm', 'Worship', [gate(), comp(), geq(b=2, c=-3)], [], amp(1, 35, bass=62, mid=45, deep=1), cab(0), key='v1-fingerstyle-warm', col='3FA9A0'),
    P('Pick Attack', 'Rock', [gate(), fet(), boost(g=4)], [], amp(3, 45, treble=65, ultraHi=1), cab(2), key='v1-pick-attack', col='E0A22B'),
    P('Rock Drive', 'Drive', [gate(), comp(), od(d=55)], [], amp(1, 62, sag=45, presence=55), cab(3), key='v1-rock-drive', col='E2574C'),
    P('Fuzz Monster', 'Drive', [gate(), fuzz(f=70), geq(a=3, e=-4)], [], amp(2, 55), cab(3), key='v1-fuzz-monster', col='8B5CF6'),
    P('Motown Flip', 'Gospel', [gate(), comp()], [], amp(2, 42, bass=65, treble=35, sag=60), cab(0, mic=2, highCut=4000), key='v1-motown-flip', col='D08B4F'),
    P('Slap Contour', 'Slap', [gate(), fet(), geq(c=-6, e=4)], [], amp(0, 40, contour=70, bass=68, treble=68, bright=1), cab(2), key='v1-slap-contour', col='2BC4C0'),
    P('Funk Filter', 'Funk', [gate(), comp(), env(s=65, r=70, q=65)], [], amp(0, 40), cab(1), key='v1-funk-filter', col='B2D235'),
    P('Sub Bomb', 'Synth', [gate(), octv(s1=70, dry=90, tone=35), comp()], [], amp(3, 40, ultraLo=1), cab(3), key='v1-sub-bomb', col='4C6EF5'),
    P('Synth Bass', 'Synth', [gate(), comp()], [], amp(0, 40), cab(2), key='v1-synth-bass', col='E056A0', synth=synth(wave=0, cutoff=700, reso=45, env=60, sub=55, level=-6)),
    P('Dub Echo', 'Reggae', [gate(), comp()], [tape(fb=48, wow=35, mix=28)], amp(2, 40, bass=62, treble=38), cab(0), key='v1-dub-echo', col='9B6BF5'),
    P('Ambient Bed', 'Ambient', [gate(), comp()], [chorus(mix=30), hall(mix=35)], amp(0, 25), cab(0), key='v1-ambient-bed', col='6EC1E4', loop={'quantize': 1}),

    # ---- Worship -------------------------------------------------------------------
    P('Sunday Foundation', 'Worship', [gate(), comp(s=55)], [], amp(1, 32, bass=60, mid=48), cab(0)),
    P('Swell Under the Keys', 'Worship', [gate(), comp(s=65)], [hall(size=60, decay=4, mix=20)], amp(0, 28, bass=60, treble=42), cab(0)),
    P('Driving Eighths', 'Worship', [gate(), comp(s=50), od(d=25, bl=60)], [], amp(1, 40, mid=55), cab(2)),
    P('Bridge Build Bass', 'Worship', [gate(), comp(s=45), od(d=40, bl=55)], [], amp(1, 48, presence=50), cab(3)),
    P('Ballad Round', 'Worship', [gate(), comp(s=60)], [plate(mix=12)], amp(2, 30, bass=62, treble=35), cab(0)),
    P('Octave Under Pads', 'Worship', [gate(), octv(s1=35, dry=100, tone=30), comp()], [hall(mix=18)], amp(0, 30), cab(0)),
    P('Clean Pick Worship', 'Worship', [gate(), fet(th=-20), comp(s=40)], [], amp(0, 36, treble=58), cab(2)),
    P('Big Room Low End', 'Worship', [gate(), comp(s=55), geq(a=2, b=3, c=-2)], [plate(size=55, mix=10)], amp(3, 35, deep=1), cab(3)),
    P('Altar Pulse', 'Worship', [gate(), comp()], [], amp(1, 34, bass=58, mid=50, sag=40), cab(0)),
    P('Shimmer Root Notes', 'Worship', [gate(), comp(s=60)], [chorus(rate=.3, depth=35, mix=20), hall(decay=6, shim=15, mix=22)], amp(0, 28), cab(0)),

    # ---- Clean ---------------------------------------------------------------------
    P('Modern Clean DI', 'Clean', [gate(), comp(s=45)], [], amp(0, 32, bass=56, treble=58), cab(4)),
    P('Vintage P Thump', 'Clean', [gate(), comp(s=40)], [], amp(2, 38, bass=62, treble=30, sag=50), cab(0, highCut=3500)),
    P('Jazz Bass Growl', 'Clean', [gate(), comp(s=40)], [], amp(1, 42, mid=60, midFreq=800, treble=55), cab(1)),
    P('Hi-Fi Five String', 'Clean', [gate(), comp(s=45), geq(a=2, c=-2, e=2)], [], amp(0, 30, ultraLo=1), cab(2)),
    P('Fingers and Felt', 'Clean', [gate(), comp(s=55)], [], amp(2, 30, treble=32), cab(0)),
    P('Session Clean', 'Clean', [gate(), comp(s=50), peq(bass=2, mf=700, mg=-2, treb=1)], [], amp(0, 34), cab(2)),
    P('Upright Feel', 'Clean', [gate(), comp(s=45)], [plate(size=35, mix=10)], amp(2, 28, bass=60, mid=58, treble=30), cab(0, highCut=3000)),
    P('Bright Pick Clean', 'Clean', [gate(), fet(th=-20, ratio=2)], [], amp(0, 36, treble=65, bright=1), cab(1)),
    P('Deep Studio', 'Clean', [gate(), comp(s=50)], [], amp(1, 32, bass=64, deep=1), cab(3)),

    # ---- Drive ---------------------------------------------------------------------
    P('Grit Blend', 'Drive', [gate(), comp(), od(d=45, bl=50)], [], amp(1, 45), cab(2)),
    P('Tube Hair', 'Drive', [gate(), tube(d=35, l=55)], [], amp(1, 52, sag=45), cab(3)),
    P('Overdriven Mids', 'Drive', [gate(), od(d=55, t=60, bl=70), geq(c=3, d=2)], [], amp(1, 50), cab(1)),
    P('Fuzz Blend Low', 'Drive', [gate(), fuzz(f=65, bl=55)], [], amp(2, 45), cab(3)),
    P('Clipped Pick', 'Drive', [gate(), fet(), od(d=50, t=65, bl=65)], [], amp(3, 48), cab(2)),
    P('Gravel Road', 'Drive', [gate(), od(d=65, t=45, bl=60)], [], amp(3, 55, sag=50), cab(3)),
    P('Warm Breakup', 'Drive', [gate(), comp(s=40)], [], amp(1, 60, sag=55), cab(0)),
    P('Fuzz Wall Low', 'Drive', [gate(th=-60), fuzz(f=85, t=40, bl=45)], [], amp(3, 50), cab(3)),
    P('Blended Distortion', 'Drive', [gate(), od(d=75, bl=45), geq(a=2, d=3)], [], amp(3, 52), cab(2)),

    # ---- Rock ----------------------------------------------------------------------
    P('Classic Rock Pick', 'Rock', [gate(), fet(), od(d=30, bl=60)], [], amp(1, 52, mid=55), cab(3)),
    P('Arena Rock Bass', 'Rock', [gate(), comp(s=45), od(d=45, bl=55)], [plate(size=50, mix=8)], amp(3, 50), cab(3)),
    P('Punk Downpicks', 'Rock', [gate(), od(d=55, t=60, bl=70)], [], amp(3, 55, treble=60), cab(2)),
    P('Seventies Tube Stack', 'Rock', [gate(), tube(d=40)], [], amp(1, 58, sag=50), cab(3)),
    P('Alt Rock Clank', 'Rock', [gate(), fet(th=-22, ratio=2), boost(g=5, p=65)], [], amp(3, 45, ultraHi=1), cab(2)),
    P('Grunge Low', 'Rock', [gate(th=-60), fuzz(f=60, t=45, bl=60)], [], amp(3, 50), cab(3)),
    P('Indie Pick Chorus', 'Rock', [gate(), fet()], [chorus(rate=.6, depth=40, mix=30)], amp(0, 40, treble=60), cab(2)),
    P('Stoner Fuzz', 'Rock', [gate(th=-60), fuzz(f=80, t=35, bl=50)], [], amp(3, 55, sag=60), cab(3)),
    P('Southern Groove', 'Rock', [gate(), comp(s=40)], [], amp(1, 48, mid=58), cab(0)),

    # ---- Funk ----------------------------------------------------------------------
    P('Envelope Strut', 'Funk', [gate(), comp(), env(s=70, r=65, q=70)], [], amp(0, 38), cab(1)),
    P('P-Funk Synth Filter', 'Funk', [gate(), env(s=75, r=75, q=80)], [], amp(0, 36), cab(2), synth=synth(wave=1, cutoff=900, reso=55, env=70, sub=30, level=-12)),
    P('Tight Funk Fingers', 'Funk', [gate(), fet(th=-24, ratio=2)], [], amp(0, 36, mid=55, treble=58), cab(1)),
    P('Disco Octave', 'Funk', [gate(), octv(s1=45, dry=100), fet()], [], amp(0, 36), cab(2)),
    P('Wah Funk Bass', 'Funk', [gate(), env(s=60, r=55, q=60, up=1)], [], amp(1, 40), cab(1)),
    P('Phase Funk', 'Funk', [gate(), fet(), phaser(rate=.6, depth=50)], [], amp(0, 38), cab(1)),
    P('Minneapolis Pluck', 'Funk', [gate(), fet(th=-22, ratio=3), geq(c=-3, e=3)], [], amp(0, 38, contour=40), cab(2)),
    P('Neo Soul Round', 'Funk', [gate(), comp(s=55)], [chorus(rate=.3, depth=30, mix=18)], amp(2, 32, treble=35), cab(0)),

    # ---- Slap ----------------------------------------------------------------------
    P('Modern Slap', 'Slap', [gate(), fet(th=-22, ratio=3), geq(a=2, c=-6, e=5)], [], amp(0, 38, contour=75, bright=1), cab(2)),
    P('Popped Highs', 'Slap', [gate(), fet(th=-24, ratio=2)], [], amp(0, 40, contour=60, treble=72, ultraHi=1), cab(1)),
    P('Scooped Thumb', 'Slap', [gate(), comp(s=55), geq(b=3, c=-7, d=-3, e=4)], [], amp(0, 36, contour=80), cab(2)),
    P('Seventies Slap', 'Slap', [gate(), fet(th=-20, ratio=2)], [], amp(1, 40, treble=62, mid=45), cab(1)),
    P('Gospel Chops Slap', 'Slap', [gate(), fet(th=-24, ratio=3), boost(g=4, p=60)], [plate(size=40, mix=8)], amp(0, 38, contour=65), cab(2)),
    P('Fusion Slap', 'Slap', [gate(), fet(), geq(a=1, c=-4, e=3)], [chorus(rate=.5, depth=25, mix=15)], amp(0, 38, contour=55), cab(2)),
    P('Slap Octave', 'Slap', [gate(), octv(s1=35, dry=100), fet(th=-22, ratio=3)], [], amp(0, 38, contour=70), cab(2)),

    # ---- Metal ---------------------------------------------------------------------
    P('Metal Grind', 'Metal', [gate(th=-55, rel=70), od(d=70, t=60, bl=50)], [], amp(3, 55), cab(3)),
    P('Djent Low End', 'Metal', [gate(th=-50, rel=50), fet(th=-22, ratio=3), od(d=60, bl=45)], [], amp(3, 50, ultraLo=1), cab(3)),
    P('Clanky Thrash', 'Metal', [gate(th=-52), boost(g=6, p=70), od(d=55, t=70, bl=55)], [], amp(3, 52, treble=65), cab(2)),
    P('Doom Sub', 'Metal', [gate(th=-55), fuzz(f=80, t=30, bl=55), octv(s1=40, dry=100)], [], amp(3, 55), cab(3)),
    P('Metalcore Pick', 'Metal', [gate(th=-50, rel=50), fet(th=-22, ratio=2), od(d=65, bl=50)], [], amp(3, 55, mid=45), cab(3)),
    P('Sludge', 'Metal', [gate(th=-55), fuzz(f=75, t=40, bl=50)], [], amp(1, 62, sag=60), cab(3)),
    P('Modern Prog', 'Metal', [gate(th=-55), comp(s=40), od(d=50, bl=55)], [], amp(3, 48, ultraHi=1), cab(2)),

    # ---- Gospel --------------------------------------------------------------------
    P('Gospel Chops', 'Gospel', [gate(), fet(th=-22, ratio=2), comp(s=45)], [], amp(0, 38, contour=40, treble=60), cab(2)),
    P('Church Organ Low', 'Gospel', [gate(), comp(s=60), octv(s1=30, dry=100, tone=30)], [plate(size=55, mix=12)], amp(0, 32), cab(3)),
    P('Shout Music', 'Gospel', [gate(), fet(th=-20, ratio=2), boost(g=4, p=60)], [], amp(0, 40, treble=62), cab(2)),
    P('Praise Break', 'Gospel', [gate(), fet(th=-24, ratio=3), od(d=25, bl=50)], [], amp(0, 42, contour=50), cab(2)),
    P('Sunday Morning Round', 'Gospel', [gate(), comp(s=55)], [], amp(2, 34, bass=62), cab(0)),
    P('Choir Under', 'Gospel', [gate(), comp(s=65)], [hall(decay=4, mix=15)], amp(0, 28, treble=40), cab(0)),
    P('Fast Runs', 'Gospel', [gate(), fet(th=-22, ratio=2)], [], amp(0, 38, mid=56, treble=60), cab(1)),
    P('Old School Soul', 'Gospel', [gate(), comp(s=50)], [], amp(2, 40, bass=60, treble=32, sag=55), cab(0, highCut=3800)),

    # ---- Reggae --------------------------------------------------------------------
    P('Roots Reggae', 'Reggae', [gate(), comp(s=55)], [], amp(2, 34, bass=68, treble=25), cab(0, highCut=3000)),
    P('Steppers Dub', 'Reggae', [gate(), comp(s=60)], [tape(div=5, fb=50, mix=30, wow=40, tone=3000)], amp(2, 34, bass=66, treble=28), cab(0)),
    P('Lovers Rock', 'Reggae', [gate(), comp(s=55)], [chorus(rate=.3, depth=30, mix=15)], amp(1, 32, bass=64, treble=32), cab(0)),
    P('Deep Sound System', 'Reggae', [gate(), comp(s=60), octv(s1=40, dry=100, tone=25)], [], amp(3, 34, ultraLo=1, treble=30), cab(3)),
    P('Dub Siren Echo', 'Reggae', [gate(), comp()], [tape(div=4, fb=62, mix=35, wow=50, tone=2500), plate(size=60, mix=15)], amp(2, 34), cab(0)),
    P('Afrobeat Pocket', 'Reggae', [gate(), fet(th=-20, ratio=2)], [], amp(0, 36, mid=55), cab(1)),
    P('Highlife Round', 'Reggae', [gate(), comp(s=45)], [plate(size=35, mix=10)], amp(2, 34, mid=55, treble=45), cab(0)),

    # ---- Jazz ----------------------------------------------------------------------
    P('Walking Upright', 'Jazz', [gate(), comp(s=35)], [plate(size=40, mix=10)], amp(2, 28, bass=58, mid=60, treble=30), cab(0, highCut=3200)),
    P('Fretless Mwah', 'Jazz', [gate(), comp(s=55)], [chorus(rate=.4, depth=35, mix=25), plate(mix=15)], amp(0, 34, mid=60, midFreq=900), cab(2)),
    P('Fusion Lead Bass', 'Jazz', [gate(), comp(s=50), od(d=30, bl=40)], [dig(l=5, r=5, fb=25, mix=15)], amp(0, 40, mid=60), cab(2)),
    P('Smooth Jazz', 'Jazz', [gate(), comp(s=50)], [chorus(rate=.3, depth=30, mix=18)], amp(0, 32), cab(2)),
    P('Bebop Walk', 'Jazz', [gate()], [], amp(2, 30, mid=58, treble=35), cab(0)),
    P('Latin Tumbao', 'Jazz', [gate(), comp(s=40)], [], amp(1, 34, mid=56), cab(0)),

    # ---- Synth ---------------------------------------------------------------------
    P('Square Sub', 'Synth', [gate(), comp()], [], amp(0, 30), cab(4), synth=synth(wave=2, cutoff=500, reso=20, env=30, sub=70, level=-6)),
    P('Acid Squelch', 'Synth', [gate(), comp()], [], amp(0, 30), cab(2), synth=synth(wave=0, cutoff=400, reso=75, env=80, sub=20, level=-10, decay=180)),
    P('Moog Style Round', 'Synth', [gate(), comp()], [], amp(0, 30), cab(4), synth=synth(wave=0, cutoff=650, reso=35, env=40, sub=50, level=-6)),
    P('Pulse Pad Bass', 'Synth', [gate(), comp()], [chorus(rate=.4, depth=40, mix=25)], amp(0, 28), cab(2), synth=synth(wave=1, cutoff=1200, reso=30, env=35, sub=40, level=-8, attack=40, release=400)),
    P('Glide Lead', 'Synth', [gate(), comp()], [dig(l=4, r=5, fb=30, mix=18)], amp(0, 30), cab(2), synth=synth(wave=0, cutoff=1800, reso=40, env=50, sub=10, level=-10, glide=120, octave=1)),
    P('Octave Synth Stack', 'Synth', [gate(), octv(s1=50, dry=90), comp()], [], amp(0, 30), cab(3), synth=synth(wave=2, cutoff=800, reso=30, env=40, sub=40, level=-9)),
    P('Trap 808 Feel', 'Synth', [gate(), comp()], [], amp(0, 28), cab(4), synth=synth(wave=3, cutoff=300, reso=10, env=20, sub=80, level=-5, decay=900, sustain=30)),
    P('Funky Synth Pluck', 'Synth', [gate(), fet()], [], amp(0, 32), cab(2), synth=synth(wave=0, cutoff=700, reso=55, env=75, sub=30, level=-9, decay=160, sustain=20)),

    # ---- Ambient -------------------------------------------------------------------
    P('Ambient Swells', 'Ambient', [gate(), comp(s=65)], [hall(size=85, decay=10, shim=20, mix=40)], amp(0, 26, treble=40), cab(0)),
    P('Chorus Drift', 'Ambient', [gate(), comp()], [chorus(rate=.2, depth=55, mix=40), hall(decay=7, mix=30)], amp(0, 28), cab(0)),
    P('Tape Echo Wash', 'Ambient', [gate(), comp()], [tape(div=6, fb=55, mix=35, wow=55, tone=3000), hall(decay=8, mix=35)], amp(2, 28), cab(0)),
    P('Flanged Low Drone', 'Ambient', [gate(), octv(s1=30, dry=100)], [flanger(rate=.08, depth=45, fb=25), hall(decay=12, mix=40)], amp(0, 26), cab(3)),
    P('Harmonic Bloom', 'Ambient', [gate(), comp(s=60)], [dig(l=5, r=6, fb=45, mix=30), hall(size=90, decay=14, shim=35, mix=45)], amp(0, 26), cab(0)),
    P('Phased Horizon', 'Ambient', [gate(), comp()], [phaser(rate=.08, depth=55), hall(decay=9, mix=35)], amp(0, 28), cab(0)),
    P('Tremolo Tide', 'Ambient', [gate(), comp()], [trem(rate=1.5, depth=40, shape=1), hall(decay=8, mix=35)], amp(0, 28), cab(0)),

    # ---- Strings: the bass as a bowed instrument (the Bow pedal) ----------------------
    P('Solo Cello', 'Strings', [gate(th=-62), comp(s=55), bow(sw=45, vib=42, body=70, rosin=20, voice=0, mode=1)], [hall(size=65, decay=3.2, mix=26)], amp(0, 28), cab(0)),
    P('Cello Section', 'Strings', [gate(th=-62), comp(s=55), bow(sw=60, vib=32, body=65, rosin=14, voice=0)], [chorus(rate=.3, depth=25, mix=20), hall(size=80, decay=4.5, mix=32)], amp(0, 28), cab(0)),
    P('Double Bass Arco', 'Strings', [gate(th=-62), comp(s=50), bow(sw=40, vib=25, body=75, rosin=24, voice=1)], [plate(size=55, decay=2.2, mix=20)], amp(1, 30), cab(0)),
    P('Orchestral Low End', 'Strings', [gate(th=-62), comp(s=60), octv(s1=35, dry=100, tone=40), bow(sw=65, vib=28, body=70, rosin=12, voice=1)], [hall(size=90, decay=6, mix=36)], amp(0, 26), cab(0)),
    P('Viola Melody', 'Strings', [gate(th=-62), comp(s=55), bow(sw=50, vib=40, body=65, rosin=18, voice=2, mode=1)], [hall(size=70, decay=3.5, mix=28)], amp(0, 28, treble=55), cab(1)),
    P('Worship Cello Pad', 'Strings', [gate(th=-62), comp(s=65), bow(sw=80, vib=28, body=60, rosin=8, voice=0)], [dig(l=5, r=6, fb=25, mix=14), hall(size=90, decay=9, shim=15, mix=42)], amp(0, 26), cab(0)),
    P('Cinematic Contrabass', 'Strings', [gate(th=-62), comp(s=65), bow(sw=75, vib=35, body=75, rosin=12, voice=1)], [hall(size=95, decay=12, mix=46)], amp(0, 26), cab(3)),
    P('Pizz to Bow Lead', 'Strings', [gate(th=-62), comp(s=45), bow(sw=20, vib=45, body=70, rosin=26, voice=0, mode=1)], [plate(size=45, decay=1.6, mix=16)], amp(0, 30), cab(1)),
]

V1 = ['v1-studio-clean', 'v1-fingerstyle-warm', 'v1-pick-attack', 'v1-rock-drive', 'v1-fuzz-monster', 'v1-motown-flip',
      'v1-slap-contour', 'v1-funk-filter', 'v1-sub-bomb', 'v1-synth-bass', 'v1-dub-echo', 'v1-ambient-bed']
DEMOS = [('Sunday Set', 74, ['v1-studio-clean', 'v1-fingerstyle-warm', 'v1-ambient-bed']),
         ('Rock Night', 128, ['v1-studio-clean', 'v1-rock-drive', 'v1-fuzz-monster']),
         ('Groove Session', 104, ['v1-slap-contour', 'v1-funk-filter', 'v1-synth-bass', 'v1-dub-echo'])]
