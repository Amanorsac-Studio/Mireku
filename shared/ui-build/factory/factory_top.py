# Top End (guitar) factory presets. Amp models: 0 Plexi 68, 1 Black Panel,
# 2 Tweed 57, 3 Top Boost 30, 4 Modern Hi-Gain, 5 Solid Clean. Channel 0/1/2 =
# Clean/Crunch/Lead. Cab: 0 1x12 open, 1 2x12 alnico, 2 4x12 V30.
# Delay sync divisions: 0 1/32, 1 1/16, 2 3/32, 3 1/8, 4 dotted 1/8, 5 1/4, 6 dotted 1/4, 7 1/2.
CATS = ['Worship', 'Clean', 'Crunch', 'Rock', 'Blues', 'Metal', 'Funk', 'Country', 'Jazz', 'Ambient', 'Lead', 'Strings', 'Pop']
COL = {'Worship': '7FA88A', 'Clean': '8AA6CF', 'Crunch': 'D8A64A', 'Rock': 'B04A3A', 'Blues': '4F6FA8', 'Metal': '3B3D40',
       'Funk': 'B2D235', 'Country': 'C9A35D', 'Jazz': '8E6F52', 'Ambient': '5F86A6', 'Lead': 'D07A3A', 'Strings': '8A3A1F', 'Pop': 'D9789B'}

def gate(th=-65, rel=120): return ('noiseGate', {'threshold': th, 'release': rel})
def comp(s=50, tone=50, lvl=50, mix=100): return ('optoComp', {'sustain': s, 'tone': tone, 'level': lvl, 'mix': mix})
def fet(th=-18, ratio=1): return ('fetComp', {'threshold': th, 'ratio': ratio})
def clear(d=30, t=55, l=50, bl=100): return ('clearDrive', {'drive': d, 'tone': t, 'level': l, 'blend': bl})
def tube(d=40, t=55, l=55, tb=0): return ('tubeOd', {'drive': d, 'tone': t, 'level': l, 'tube': tb})
def fuzz(f=60, t=50, l=40, g=20): return ('fuzz', {'fuzz': f, 'tone': t, 'level': l, 'gate': g})
def boost(g=6, p=50, low=0): return ('boost', {'gain': g, 'presence': p, 'low': low})
def geq(a=0, b=0, c=0, d=0, e=0, lvl=0): return ('graphicEq', {'g100': a, 'g250': b, 'g800': c, 'g2k': d, 'g5k': e, 'level': lvl})
def env(s=55, r=60, q=55, up=0): return ('envFilter', {'sens': s, 'range': r, 'q': q, 'direction': up})
def octv(s1=60, s2=0, dry=100, tone=50): return ('octaver', {'sub1': s1, 'sub2': s2, 'dry': dry, 'tone': tone})
def bow(sw=55, vib=35, body=60, rosin=20, voice=0, lvl=50, mode=0): return ('bow', {'swell': sw, 'vibrato': vib, 'body': body, 'rosin': rosin, 'voice': voice, 'level': lvl, 'mode': mode})
def wah(pos=50): return ('wah', {'position': pos})
def chorus(rate=.5, depth=50, mix=35, mode=0, width=80): return ('chorus', {'rate': rate, 'depth': depth, 'mix': mix, 'mode': mode, 'width': width})
def flanger(rate=.2, depth=50, fb=30, man=2.0): return ('flanger', {'rate': rate, 'depth': depth, 'feedback': fb, 'manual': man})
def phaser(rate=.3, depth=60, fb=40, st=1): return ('phaser', {'rate': rate, 'depth': depth, 'feedback': fb, 'stages': st})
def trem(rate=4.5, depth=50, shape=0, spread=70): return ('tremolo', {'rate': rate, 'depth': depth, 'shape': shape, 'spread': spread})
def peq(bass=0, mf=1000, mg=0, treb=0): return ('paraEq', {'bass': bass, 'midFreq': mf, 'midGain': mg, 'treble': treb})
def tape(div=4, fb=35, mix=25, wow=25, tone=5000, pp=0): return ('analogDelay', {'syncDivision': div, 'feedback': fb, 'mix': mix, 'wowFlutter': wow, 'tone': tone, 'pingpong': pp})
def dig(l=4, r=4, fb=30, mix=20, lp=20000, hp=20): return ('digitalDelay', {'syncDivisionL': l, 'syncDivisionR': r, 'feedback': fb, 'mix': mix, 'filterLp': lp, 'filterHp': hp})
def plate(size=50, decay=2.2, mix=25, pre=20, damp=40): return ('roomVerb', {'size': size, 'decay': decay, 'mix': mix, 'predelay': pre, 'damp': damp})
def hall(size=70, decay=4.0, shim=0, mix=25, mod=30): return ('ambientVerb', {'size': size, 'decay': decay, 'shimmer': shim, 'mix': mix, 'mod': mod})

def amp(model, ch, gain, **kw): d = {'model': model, 'channel': ch, 'gain': gain}; d.update(kw); return d
def cab(model, **kw): d = {'model': model}; d.update(kw); return d

def P(name, cat, pre, post, a, c, key=None, col=None, **extra):
    p = {'name': name, 'cat': cat, 'pre': pre, 'post': post, 'amp': a, 'cab': c, 'col': col or COL[cat]}
    if key: p['key'] = key
    p.update(extra)
    return p

PRESETS = [
    # ---- the first library (permanent keys from v1) --------------------------------
    P('Call to Worship', 'Worship', [gate(), comp()], [chorus(mix=25), hall(mix=30)], amp(1, 0, 45, bright=1), cab(1), key='v1-call-to-worship', col='7FA88A'),
    P('Glass Clean', 'Clean', [gate(), comp(), clear(d=30)], [dig(l=4, r=4, mix=22), hall()], amp(0, 1, 40, mid=62), cab(1), key='v1-glass-clean', col='D8A64A'),
    P('Bridge Drive', 'Worship', [gate(), tube(d=45)], [plate()], amp(0, 1, 62, presence=55), cab(2), key='v1-bridge-drive', col='9C4A3F'),
    P('Swell Pad', 'Ambient', [gate(), comp()], [tape(fb=45, mix=30), hall(decay=9, shim=35, mix=45)], amp(5, 0, 40), cab(1), key='v1-swell-pad', col='5F86A6'),
    P('Lead Dotted 8th', 'Lead', [gate(), boost(), tube(d=50)], [dig(l=4, r=4, fb=38, mix=26), plate()], amp(3, 2, 55), cab(1), key='v1-lead-dotted-8th', col='4F8F8A'),
    P('Altar Call', 'Worship', [gate(), comp()], [trem(rate=3.2, depth=35), hall(mix=32)], amp(2, 0, 38), cab(0), key='v1-altar-call', col='D9CFBD'),
    P('Fuzz Wall', 'Rock', [gate(), fuzz(f=72)], [plate()], amp(0, 1, 50), cab(2), key='v1-fuzz-wall', col='B04A3A'),
    P('Modern Rhythm', 'Metal', [gate(th=-55), boost()], [], amp(4, 2, 58, tight=1, mid=48), cab(2), key='v1-modern-rhythm', col='3B3D40'),
    P('Funky Wah', 'Funk', [gate(), fet(), env(s=60)], [], amp(1, 0, 42, bright=1), cab(0), key='v1-funky-wah', col='B2D235'),
    P('Practice Clean', 'Clean', [gate()], [], amp(1, 0, 35), cab(0), key='v1-practice-clean', col='8AA6CF'),

    # ---- Worship -------------------------------------------------------------------
    P('Dotted 8th Anthem', 'Worship', [gate(), comp(s=40), tube(d=35, l=60)], [dig(l=4, r=4, fb=40, mix=28), hall(mix=30)], amp(3, 1, 48), cab(1)),
    P('Morning Glory Swell', 'Worship', [gate(), comp(s=60)], [tape(div=5, fb=40, mix=28), hall(decay=7, shim=25, mix=40)], amp(5, 0, 38), cab(1)),
    P('Holy Ground Pad', 'Worship', [gate(), comp(s=55)], [chorus(rate=.3, depth=40, mix=30), hall(decay=10, shim=45, mix=50)], amp(5, 0, 35), cab(1)),
    P('Shimmer Benediction', 'Worship', [gate(), comp()], [dig(l=6, r=6, fb=35, mix=20), hall(decay=8, shim=55, mix=42)], amp(1, 0, 40, bright=1), cab(1)),
    P('Prayer Room', 'Worship', [gate(), comp(s=45)], [plate(size=60, decay=3.2, mix=30)], amp(2, 0, 36), cab(0)),
    P('Spirit Lead', 'Worship', [gate(), boost(g=5), tube(d=55, l=58)], [dig(l=4, r=5, fb=42, mix=30), hall(mix=32)], amp(0, 1, 58), cab(2)),
    P('Sunday Rhythm', 'Worship', [gate(), comp(s=35), clear(d=40)], [plate(mix=18)], amp(0, 1, 50), cab(1)),
    P('Big Chorus Drive', 'Worship', [gate(), clear(d=45), tube(d=40)], [dig(l=4, r=4, fb=30, mix=20), hall(mix=25)], amp(0, 1, 60, presence=55), cab(2)),
    P('Quiet Verse', 'Worship', [gate(), comp(s=60, lvl=55)], [chorus(rate=.4, depth=30, mix=20), plate(mix=28)], amp(1, 0, 34, bright=1), cab(1)),
    P('Ambient Ballad', 'Worship', [gate(), comp()], [tape(div=4, fb=45, mix=30, wow=35), hall(decay=6, shim=20, mix=38)], amp(3, 0, 40), cab(1)),
    P('Pad Over Keys', 'Worship', [gate(), comp(s=65)], [trem(rate=2.0, depth=20), hall(decay=12, shim=40, mix=55)], amp(5, 0, 32), cab(1)),
    P('Build to the Bridge', 'Worship', [gate(), boost(g=8), tube(d=50, l=60)], [dig(l=4, r=4, fb=35, mix=24), hall(mix=28)], amp(3, 2, 52), cab(1)),
    P('Octave Lift', 'Worship', [gate(), comp()], [dig(l=4, r=4, fb=40, mix=25), hall(shim=60, mix=40)], amp(1, 0, 40), cab(1)),
    P('Clean Picking Lines', 'Worship', [gate(), comp(s=45), clear(d=20)], [dig(l=3, r=4, fb=28, mix=22), plate()], amp(3, 0, 42), cab(1)),
    P('Altar Echoes', 'Worship', [gate(), comp()], [tape(div=6, fb=50, mix=32, wow=30), hall(decay=8, mix=40)], amp(2, 0, 40), cab(0)),
    P('Response Lead', 'Worship', [gate(), tube(d=60, t=60)], [dig(l=4, r=4, fb=45, mix=30), hall(decay=5, mix=30)], amp(0, 2, 55), cab(2)),
    P('Communion', 'Worship', [gate(), comp(s=55)], [chorus(rate=.25, depth=35, mix=25), hall(decay=6, mix=36)], amp(5, 0, 36), cab(0)),

    # ---- Clean ---------------------------------------------------------------------
    P('Black Panel Sparkle', 'Clean', [gate(), comp(s=40)], [plate(mix=18)], amp(1, 0, 38, bright=1, treble=60), cab(0)),
    P('Studio Clean DI', 'Clean', [gate(), comp(s=45)], [], amp(5, 0, 35), cab(1, lowCut=100)),
    P('Chime Clean', 'Clean', [gate()], [plate(mix=15)], amp(3, 0, 40, treble=62), cab(1)),
    P('Warm Tweed Clean', 'Clean', [gate()], [plate(mix=18)], amp(2, 0, 30, bass=55), cab(0)),
    P('Chorus Clean', 'Clean', [gate(), comp()], [chorus(rate=.6, depth=55, mix=40), plate(mix=18)], amp(5, 0, 36), cab(1)),
    P('Surf Spring', 'Clean', [gate()], [trem(rate=5.5, depth=45), plate(size=40, decay=2.8, mix=35, damp=20)], amp(1, 0, 42, bright=1), cab(0)),
    P('Compressed Pop Clean', 'Clean', [gate(), fet(th=-22, ratio=2)], [chorus(mix=20)], amp(5, 0, 38), cab(1)),
    P('Arpeggio Clean', 'Clean', [gate(), comp()], [dig(l=3, r=5, fb=30, mix=22), hall(mix=22)], amp(1, 0, 38), cab(1)),

    # ---- Crunch --------------------------------------------------------------------
    P('Edge of Breakup', 'Crunch', [gate()], [plate(mix=15)], amp(2, 1, 45), cab(0)),
    P('Brit Crunch', 'Crunch', [gate()], [plate(mix=12)], amp(0, 1, 55), cab(2)),
    P('Chime Crunch', 'Crunch', [gate(), clear(d=25)], [plate(mix=15)], amp(3, 1, 48), cab(1)),
    P('Pushed Tweed', 'Crunch', [gate(), boost(g=6)], [plate(mix=15)], amp(2, 1, 55), cab(0)),
    P('Indie Jangle Crunch', 'Crunch', [gate(), comp(s=35)], [chorus(rate=.5, depth=30, mix=20), plate(mix=15)], amp(3, 1, 42), cab(1)),
    P('Rhythm Crunch', 'Crunch', [gate(), clear(d=35)], [], amp(0, 1, 50), cab(2)),
    P('Low Gain Strum', 'Crunch', [gate(), comp(s=30)], [plate(mix=18)], amp(1, 1, 40), cab(1)),
    P('Plexi Bite', 'Crunch', [gate(), boost(g=8, p=60)], [plate(mix=12)], amp(0, 1, 58, presence=60), cab(2)),
    P('Garage Crunch', 'Crunch', [gate(), fuzz(f=35, l=45)], [plate(mix=12)], amp(2, 1, 50), cab(0)),

    # ---- Rock ----------------------------------------------------------------------
    P('Classic Rock Stack', 'Rock', [gate(th=-60)], [plate(mix=14)], amp(0, 2, 60), cab(2)),
    P('Arena Rhythm', 'Rock', [gate(th=-60), tube(d=35, l=60)], [plate(mix=14)], amp(0, 2, 62), cab(2)),
    P('Hard Rock Riff', 'Rock', [gate(th=-58), boost(g=8)], [], amp(4, 1, 55), cab(2)),
    P('Seventies Fuzz', 'Rock', [gate(), fuzz(f=65, t=45)], [plate(mix=15)], amp(0, 1, 45), cab(2)),
    P('Alt Rock Wall', 'Rock', [gate(th=-60), fuzz(f=55), tube(d=30)], [plate(mix=12)], amp(0, 2, 50), cab(2)),
    P('Power Chords', 'Rock', [gate(th=-58)], [], amp(4, 1, 58, mid=55), cab(2)),
    P('Stadium Lead', 'Rock', [gate(th=-60), tube(d=55)], [dig(l=5, r=5, fb=30, mix=20), plate(mix=15)], amp(0, 2, 65), cab(2)),
    P('Phaser Rock', 'Rock', [gate(th=-60), phaser(rate=.5, depth=55)], [plate(mix=12)], amp(0, 2, 58), cab(2)),
    P('Punk Downstrokes', 'Rock', [gate(th=-55)], [], amp(0, 2, 70), cab(2)),
    P('Grunge Drop', 'Rock', [gate(th=-55), fuzz(f=60, t=40)], [chorus(rate=.4, depth=40, mix=20)], amp(4, 1, 52), cab(2)),
    P('Southern Rock', 'Rock', [gate(), tube(d=40, t=60)], [plate(mix=14)], amp(2, 2, 55), cab(1)),

    # ---- Blues ---------------------------------------------------------------------
    P('Texas Blues', 'Blues', [gate(), tube(d=50, t=58)], [plate(mix=18)], amp(1, 1, 52, bright=1), cab(1)),
    P('Chicago Shuffle', 'Blues', [gate()], [plate(mix=20)], amp(2, 1, 50), cab(0)),
    P('Slow Blues Lead', 'Blues', [gate(), tube(d=60, t=50)], [tape(div=5, fb=25, mix=18), plate(mix=20)], amp(2, 2, 55), cab(1)),
    P('Delta Slide', 'Blues', [gate(), comp(s=40)], [plate(size=55, mix=28)], amp(2, 1, 48), cab(0)),
    P('Blues Rock Stack', 'Blues', [gate(), clear(d=45)], [plate(mix=16)], amp(0, 1, 56), cab(2)),
    P('Smoky Bar', 'Blues', [gate(), tube(d=35, t=45)], [trem(rate=4.0, depth=25), plate(mix=22)], amp(1, 1, 48), cab(0)),
    P('Soul Blues Clean', 'Blues', [gate(), comp(s=40)], [plate(mix=20)], amp(1, 0, 45), cab(1)),
    P('Harp Tone', 'Blues', [gate(), tube(d=45, t=35)], [tape(div=1, fb=20, mix=15)], amp(2, 1, 52, treble=40), cab(0)),
    P('Late Night Minor', 'Blues', [gate(), tube(d=55)], [hall(mix=22)], amp(0, 1, 52), cab(1)),

    # ---- Metal ---------------------------------------------------------------------
    P('Tight Modern Metal', 'Metal', [gate(th=-50, rel=60), boost(g=10, p=55)], [], amp(4, 2, 62, tight=1, mid=45), cab(2)),
    P('Djent Chug', 'Metal', [gate(th=-48, rel=50), boost(g=12, p=60, low=-4)], [], amp(4, 2, 58, tight=1, mid=52), cab(2)),
    P('Thrash Rhythm', 'Metal', [gate(th=-50, rel=70), tube(d=20, l=70)], [], amp(4, 2, 65, tight=1, mid=40), cab(2)),
    P('Doom Fuzz', 'Metal', [gate(th=-55), fuzz(f=85, t=35, l=45)], [plate(size=70, mix=15)], amp(4, 1, 50), cab(2)),
    P('Metal Lead', 'Metal', [gate(th=-52), boost(g=8)], [dig(l=5, r=5, fb=30, mix=18), plate(mix=12)], amp(4, 2, 65), cab(2)),
    P('Scooped Eighties', 'Metal', [gate(th=-52), geq(a=4, b=0, c=-6, d=2, e=4)], [chorus(mix=15)], amp(4, 2, 60), cab(2)),
    P('Metalcore Breakdown', 'Metal', [gate(th=-46, rel=40), boost(g=12, low=-6)], [], amp(4, 2, 60, tight=1), cab(2)),
    P('Melodic Metal Lead', 'Metal', [gate(th=-52), tube(d=30, l=65)], [dig(l=4, r=5, fb=35, mix=22), hall(mix=18)], amp(4, 2, 62), cab(2)),

    # ---- Funk ----------------------------------------------------------------------
    P('Chicken Scratch', 'Funk', [gate(), fet(th=-22, ratio=2)], [], amp(1, 0, 40, bright=1, treble=62), cab(0)),
    P('Auto Wah Strut', 'Funk', [gate(), fet(), env(s=65, r=70, q=65)], [plate(mix=10)], amp(1, 0, 42), cab(0)),
    P('Quack Rhythm', 'Funk', [gate(), env(s=70, r=55, q=75)], [], amp(5, 0, 40), cab(1)),
    P('Disco Strum', 'Funk', [gate(), fet(th=-20, ratio=2)], [chorus(rate=.7, depth=35, mix=25)], amp(5, 0, 38), cab(1)),
    P('Neo Soul Clean', 'Funk', [gate(), comp(s=45)], [chorus(rate=.4, depth=35, mix=25), plate(mix=20)], amp(1, 0, 38), cab(1)),
    P('Wah Funk', 'Funk', [gate(), wah(pos=55), fet()], [plate(mix=12)], amp(1, 1, 42), cab(0)),
    P('Phase Groove', 'Funk', [gate(), fet(), phaser(rate=.8, depth=50)], [], amp(1, 0, 42), cab(0)),

    # ---- Country -------------------------------------------------------------------
    P('Telly Twang', 'Country', [gate(), fet(th=-20, ratio=2)], [tape(div=1, fb=20, mix=18), plate(mix=15)], amp(1, 0, 42, bright=1, treble=65), cab(0)),
    P('Chicken Pickin', 'Country', [gate(), fet(th=-24, ratio=3), clear(d=20)], [tape(div=1, fb=15, mix=15)], amp(1, 0, 44, bright=1), cab(0)),
    P('Nashville Clean', 'Country', [gate(), comp(s=45)], [plate(mix=22)], amp(1, 0, 38, bright=1), cab(1)),
    P('Slapback Rockabilly', 'Country', [gate()], [tape(div=0, fb=10, mix=28, wow=15), plate(mix=12)], amp(2, 1, 42), cab(0)),
    P('Pedal Steel Swell', 'Country', [gate(), comp(s=70)], [tape(div=5, fb=25, mix=18), hall(decay=4, mix=30)], amp(1, 0, 38), cab(1)),
    P('Outlaw Crunch', 'Country', [gate(), tube(d=30, t=60)], [plate(mix=16)], amp(2, 1, 48), cab(0)),
    P('Western Tremolo', 'Country', [gate()], [trem(rate=4.8, depth=55), plate(size=45, decay=2.8, mix=30)], amp(1, 0, 40), cab(0)),

    # ---- Jazz ----------------------------------------------------------------------
    P('Warm Archtop', 'Jazz', [gate()], [plate(mix=15)], amp(5, 0, 30, treble=35, bass=58), cab(0)),
    P('Bebop Clean', 'Jazz', [gate(), comp(s=30)], [plate(mix=12)], amp(1, 0, 32, treble=38), cab(1)),
    P('Fusion Lead', 'Jazz', [gate(), comp(s=50), tube(d=45, t=50)], [dig(l=5, r=5, fb=25, mix=18), hall(mix=20)], amp(0, 1, 48), cab(1)),
    P('Chord Melody', 'Jazz', [gate()], [hall(size=50, decay=2.5, mix=18)], amp(2, 0, 28, treble=40), cab(0)),
    P('Smooth Jazz Chorus', 'Jazz', [gate(), comp(s=45)], [chorus(rate=.35, depth=45, mix=30), plate(mix=18)], amp(5, 0, 32), cab(1)),
    P('Gypsy Swing', 'Jazz', [gate()], [plate(size=35, decay=1.5, mix=15)], amp(5, 0, 34, mid=62), cab(0)),

    # ---- Ambient -------------------------------------------------------------------
    P('Infinite Shimmer', 'Ambient', [gate(), comp(s=60)], [hall(size=95, decay=18, shim=70, mix=60, mod=40)], amp(5, 0, 32), cab(1)),
    P('Tape Haze', 'Ambient', [gate(), comp()], [tape(div=6, fb=60, mix=38, wow=60, tone=3000), hall(decay=9, mix=45)], amp(2, 0, 34), cab(0)),
    P('Frozen Lake', 'Ambient', [gate(), comp(s=65)], [chorus(rate=.15, depth=60, mix=35), hall(size=90, decay=15, shim=30, mix=55)], amp(5, 0, 30), cab(1)),
    P('Granular Dusk', 'Ambient', [gate(), fuzz(f=20, l=35)], [tape(div=7, fb=65, mix=40, wow=70, tone=2500), hall(decay=12, mix=50)], amp(5, 0, 30), cab(1)),
    P('Swell and Wash', 'Ambient', [gate(), comp(s=70)], [dig(l=6, r=7, fb=55, mix=35), hall(decay=14, shim=40, mix=52)], amp(5, 0, 32), cab(1)),
    P('Slow Tremolo Dream', 'Ambient', [gate(), comp()], [trem(rate=1.2, depth=45, shape=1), hall(decay=10, mix=48)], amp(1, 0, 34), cab(0)),
    P('Cinematic Drone', 'Ambient', [gate(), octv(s1=40, s2=0, dry=80)], [flanger(rate=.08, depth=40, fb=20), hall(size=95, decay=20, shim=50, mix=62)], amp(5, 0, 30), cab(1)),
    P('Reverse Dreams', 'Ambient', [gate(), comp()], [dig(l=5, r=6, fb=45, mix=40, lp=6000), hall(decay=9, shim=25, mix=50)], amp(3, 0, 36), cab(1)),
    P('Ocean Floor', 'Ambient', [gate(), comp(s=60)], [phaser(rate=.1, depth=50), hall(size=85, decay=13, mix=55)], amp(5, 0, 32), cab(1)),

    # ---- Lead ----------------------------------------------------------------------
    P('Singing Lead', 'Lead', [gate(), comp(s=55), tube(d=60, t=55)], [dig(l=5, r=5, fb=32, mix=22), hall(mix=22)], amp(0, 2, 60), cab(2)),
    P('Violin Swell Lead', 'Lead', [gate(), comp(s=75), tube(d=50, t=40)], [dig(l=5, r=6, fb=30, mix=22), hall(decay=5, mix=30)], amp(0, 2, 55, treble=45), cab(1)),
    P('Eighties Shred', 'Lead', [gate(th=-55), boost(g=10)], [chorus(rate=.6, depth=35, mix=18), dig(l=4, r=4, fb=30, mix=20)], amp(4, 2, 62), cab(2)),
    P('Woman Tone', 'Lead', [gate(), fuzz(f=50, t=25, l=45)], [plate(mix=18)], amp(0, 2, 55, treble=35), cab(2)),
    P('Fluid Legato', 'Lead', [gate(), comp(s=60), tube(d=45)], [dig(l=4, r=5, fb=35, mix=24), hall(mix=22)], amp(4, 2, 55), cab(2)),
    P('Octave Down Fuzz', 'Lead', [gate(), octv(s1=45, dry=100), fuzz(f=70, t=55)], [plate(mix=15)], amp(0, 2, 50), cab(2)),
    P('Wah Solo', 'Lead', [gate(), wah(pos=60), tube(d=55)], [dig(l=5, r=5, fb=28, mix=18), plate(mix=16)], amp(0, 2, 58), cab(2)),
    P('Floating Lead', 'Lead', [gate(), boost(g=6), tube(d=50)], [tape(div=4, fb=45, mix=30, wow=30), hall(decay=7, shim=20, mix=35)], amp(3, 2, 52), cab(1)),

    # ---- Strings: the guitar as a bowed instrument (the Bow pedal) ---------------------
    P('Solo Violin', 'Strings', [gate(th=-62), comp(s=55), bow(sw=45, vib=45, body=70, rosin=22, voice=0)], [hall(size=60, decay=2.8, mix=26)], amp(5, 0, 32, treble=52), cab(1)),
    P('Violin Section', 'Strings', [gate(th=-62), comp(s=50), bow(sw=60, vib=30, body=60, rosin=12, voice=0)], [chorus(rate=.3, depth=25, mix=22), hall(size=80, decay=4.5, mix=34)], amp(5, 0, 30), cab(1)),
    P('Viola Ballad', 'Strings', [gate(th=-62), comp(s=55), bow(sw=55, vib=38, body=65, rosin=18, voice=1)], [hall(size=70, decay=3.5, mix=30)], amp(5, 0, 32), cab(1)),
    P('Cello Low Strings', 'Strings', [gate(th=-62), comp(s=60), octv(s1=40, dry=100, tone=40), bow(sw=60, vib=32, body=70, rosin=15, voice=2)], [hall(size=75, decay=4, mix=28)], amp(5, 0, 30, bass=58), cab(1)),
    P('String Pad Swell', 'Strings', [gate(th=-62), comp(s=65), bow(sw=85, vib=25, body=55, rosin=8, voice=0)], [chorus(rate=.2, depth=35, mix=28), hall(size=90, decay=9, shim=15, mix=45)], amp(5, 0, 30), cab(1)),
    P('Worship Strings', 'Strings', [gate(th=-62), comp(s=60), bow(sw=70, vib=30, body=60, rosin=10, voice=1)], [dig(l=5, r=6, fb=25, mix=15), hall(size=85, decay=6, shim=20, mix=40)], amp(5, 0, 30), cab(1)),
    P('Fiddle Reel', 'Strings', [gate(th=-62), comp(s=45), bow(sw=10, vib=20, body=75, rosin=35, voice=0)], [plate(size=40, decay=1.4, mix=14)], amp(5, 0, 34, treble=56), cab(0)),
    P('Cinematic Cello', 'Strings', [gate(th=-62), comp(s=65), bow(sw=75, vib=40, body=75, rosin=14, voice=2)], [hall(size=95, decay=12, mix=48)], amp(5, 0, 30), cab(1)),
    P('Lead Violin Solo', 'Strings', [gate(th=-62), comp(s=55), bow(sw=40, vib=45, body=70, rosin=20, voice=0, mode=1)], [dig(l=4, r=4, fb=22, mix=12), hall(size=65, decay=3, mix=26)], amp(5, 0, 30), cab(1)),
    P('Viola Solo Line', 'Strings', [gate(th=-62), comp(s=55), bow(sw=50, vib=40, body=65, rosin=18, voice=1, mode=1)], [hall(size=70, decay=3.5, mix=28)], amp(5, 0, 30), cab(1)),
    P('Solo Cello Melody', 'Strings', [gate(th=-62), comp(s=60), bow(sw=55, vib=38, body=72, rosin=16, voice=2, mode=1)], [hall(size=80, decay=4.5, mix=30)], amp(5, 0, 30, bass=56), cab(1)),

    # ---- Pop -----------------------------------------------------------------------
    P('Bright Pop Strum', 'Pop', [gate(), fet(th=-20, ratio=2)], [chorus(mix=18), plate(mix=16)], amp(5, 0, 38, treble=60), cab(1)),
    P('Pop Rock Crunch', 'Pop', [gate(), clear(d=40)], [plate(mix=15)], amp(0, 1, 52), cab(1)),
    P('Synth Pop Clean', 'Pop', [gate(), comp()], [chorus(rate=.8, depth=50, mix=35, mode=1), dig(l=3, r=4, fb=25, mix=18)], amp(5, 0, 36), cab(1)),
    P('Indie Pop Jangle', 'Pop', [gate(), comp(s=40)], [chorus(rate=.5, depth=30, mix=20), plate(mix=20)], amp(3, 0, 42, treble=62), cab(1)),
    P('Ballad Clean', 'Pop', [gate(), comp(s=50)], [dig(l=5, r=5, fb=25, mix=18), hall(mix=30)], amp(1, 0, 36), cab(1)),
    P('Dance Rhythm', 'Pop', [gate(), fet(th=-22, ratio=2)], [trem(rate=6.0, depth=40, shape=2), plate(mix=12)], amp(5, 0, 38), cab(1)),
]

V1 = ['v1-call-to-worship', 'v1-glass-clean', 'v1-bridge-drive', 'v1-swell-pad', 'v1-lead-dotted-8th',
      'v1-altar-call', 'v1-fuzz-wall', 'v1-modern-rhythm', 'v1-funky-wah', 'v1-practice-clean']
DEMOS = [('Sunday Set', 96, ['v1-call-to-worship', 'v1-glass-clean', 'v1-bridge-drive', 'v1-swell-pad', 'v1-lead-dotted-8th', 'v1-altar-call']),
         ('Rock Night', 128, ['v1-bridge-drive', 'v1-fuzz-wall', 'v1-modern-rhythm', 'v1-lead-dotted-8th'])]
