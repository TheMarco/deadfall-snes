#!/usr/bin/env python3
"""Convert an AddmusicK MML score (the .txt inside the song zips) into a chiptune
.it module for snesmod/smconv, plus a WAV preview to audition.

Why this exists: the songs/ zips ship as .spc dumps -- 64KB snapshots of the
SNES audio RAM that carry AddmusicK's own sound driver. Uploading one would
replace snesmod and kill the game's 12 sound effects (single APU). But every zip
also ships the AddmusicK *MML* (the score). MML is pure composition (notes +
timing), exactly like the MIDI that mid2it already turns into a snesmod chiptune.
So we parse the MML into the same per-channel note events mid2it uses, give each
voice a tiny synthesised waveform, and emit an .it the existing engine plays --
which keeps SFX working.

  python3 mml2it.py "path/song.txt" level1   -> res/music_level1.it + preview wav

AddmusicK MML handled: channels #0..#7, notes a-g with +/- and lengths + dots,
rests r, ties ^, octave o/>/<, default length l, tempo t, volume v / master w,
instrument @, transpose h, quantise q (skipped), pan y (skipped), loops [..]n
incl. nesting, labelled-loop recall (N)[..] .. (N), "NAME=.." macros, $XX engine
bytes (skipped), ; comments, / loop point (ignored -- snesmod loops the module).
Percussion channels (mostly @>=30) route to drum one-shots; the rest are tuned
synth voices. Timbre is an approximation; pitches/timing are the real song.
"""
import os
import re
import sys

# reuse the proven synth bank + pattern/IT writer + WAV preview from mid2it
from mid2it import (instruments, to_patterns, render_wav, R, NOTE_OFF,
                    TRI, SQR, P25, P12, SAW, KICK, SNARE, HAT, CRASH)
from make_it import write_it

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "res")

WHOLE = 192                 # ticks in a whole note (l1); quarter = 48 = R rows
TICKS_PER_ROW = WHOLE // (4 * R)            # 48 ticks/quarter / R rows = 6
SEMI = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}
OCT_BASE = 12              # note value = octave*12 + semitone + OCT_BASE; o4 c = 60
DEFAULT_LEN = 8           # MML default note length when no `l` seen
# AddmusicK/N-SPC tempo -> BPM. Anchored on ct-gameover's own MML comment
# ("t65 ; 155 bpm" => x2.385). speed=3 means no IT-tempo clamp until 255bpm, so
# we can honour it directly. Per-track override available in build_songs.py.
AMK_T_TO_BPM = 2.385
IT_SPEED = 3              # ticks/row; with R=8 rows/quarter, IT tempo == BPM (no
                         # 255 clamp until 255bpm, vs speed 6 clamping at 127bpm)
MAX_TICKS = 200 * WHOLE   # safety cap on a channel's length (snesmod loops anyway)

# AMK channel index -> default melodic synth voice (cosmetic; pitch is what reads)
CH_VOICE = [TRI, SQR, P25, P12, SAW, TRI, P25, SAW]


# ------------------------------------------------------------- preprocessing --
def preprocess(text):
    """Return {channel_index: mml_body_string}. Strips comments, brace blocks,
    directives and expands "NAME=value" macros."""
    text = text.lstrip('﻿')
    # drop ; comments to end of line
    text = re.sub(r';[^\n]*', '', text)
    # collect + remove "NAME=value" macros (value is the rest inside the quotes)
    macros = {}
    for m in re.finditer(r'"\s*([^"=\s]+)\s*=\s*([^"]*)"', text):
        macros[m.group(1)] = m.group(2)
    text = re.sub(r'"[^"]*"', '', text)
    # remove balanced {..} blocks (#spc/#samples/#instruments bodies)
    out = []
    depth = 0
    for ch in text:
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth = max(0, depth - 1)
        elif depth == 0:
            out.append(ch)
    text = ''.join(out)
    # expand macros (longest name first so substrings don't clash)
    for name in sorted(macros, key=len, reverse=True):
        text = re.sub(r'(?<![A-Za-z0-9_])' + re.escape(name) + r'(?![A-Za-z0-9_])',
                      ' ' + macros[name] + ' ', text)
    # slice into channels on ^#<digit> headers; everything else (#amk, $.. preamble) ignored
    chans = {}
    pieces = re.split(r'(?m)^[ \t]*#([0-7])\b', text)
    preamble = pieces[0]              # global setup (tempo etc.) before any channel
    # pieces[0] = preamble; then (digit, body) pairs
    for i in range(1, len(pieces), 2):
        idx = int(pieces[i])
        body = pieces[i + 1]
        # a body runs until the next #directive line (already split on #<digit>;
        # also cut at any other #word that slipped in)
        body = re.split(r'(?m)^[ \t]*#[A-Za-z]', body)[0]
        chans.setdefault(idx, '')
        chans[idx] += ' ' + body
    return chans, preamble


def preamble_tempo(preamble):
    """Some scores set `t<n>` (and `w<n>`) globally before the first channel;
    those would otherwise be dropped, leaving the song at the default tempo."""
    for tag, *rest in tokenize(preamble):
        if tag == 'tempo' and rest[0]:
            return rest[0]
    return None


# ----------------------------------------------------------------- tokenizer --
def tokenize(body):
    """Flat token list. Tokens are tuples; first element is a tag string."""
    s = body.lower()
    i, n = 0, len(body)
    toks = []

    def num(j):                       # read decimal int at j -> (value|None, j)
        k = j
        while k < n and s[k].isdigit():
            k += 1
        return (int(s[j:k]) if k > j else None), k

    def dots(j):
        d = 0
        while j < n and s[j] == '.':
            d += 1; j += 1
        return d, j

    while i < n:
        c = s[i]
        if c in ' \t\r\n':
            i += 1
        elif c in SEMI:               # note
            i += 1
            acc = 0
            while i < n and s[i] in '+-=':   # sharp / flat / natural
                if s[i] == '+':
                    acc += 1
                elif s[i] == '-':
                    acc -= 1
                i += 1
            ln, i = num(i)
            d, i = dots(i)
            toks.append(('note', SEMI[c] + acc, ln, d))
        elif c == 'r':
            i += 1; ln, i = num(i); d, i = dots(i)
            toks.append(('rest', ln, d))
        elif c == '^':
            i += 1; ln, i = num(i); d, i = dots(i)
            toks.append(('tie', ln, d))
        elif c == 'o':
            i += 1; v, i = num(i); toks.append(('oct', v if v is not None else 4))
        elif c == '>':
            i += 1; toks.append(('octup',))
        elif c == '<':
            i += 1; toks.append(('octdn',))
        elif c == 'l':
            i += 1; v, i = num(i); d, i = dots(i)
            toks.append(('deflen', v if v else DEFAULT_LEN, d))
        elif c == 't':
            i += 1; v, i = num(i); toks.append(('tempo', v))
        elif c == 'v':
            i += 1; v, i = num(i); toks.append(('vol', v))
        elif c == 'w':
            i += 1; v, i = num(i); toks.append(('gvol', v))
        elif c == 'y':                # pan (optionally y a,b,c) - consumed, ignored
            i += 1; _, i = num(i)
            while i < n and s[i] == ',':
                i += 1; _, i = num(i)
        elif c == '@':
            i += 1; v, i = num(i); toks.append(('inst', v if v is not None else 0))
        elif c == 'h':                # transpose (signed)
            i += 1; sign = 1
            if i < n and s[i] in '+-':
                sign = -1 if s[i] == '-' else 1; i += 1
            v, i = num(i); toks.append(('transpose', sign * (v or 0)))
        elif c == 'q':                # quantise XY (skip 2 hex digits)
            i += 1; k = i
            while k < n and k < i + 2 and s[k] in '0123456789abcdef':
                k += 1
            i = k
        elif c == 'p':                # pitch/vibrato params p a,b,c (skip)
            i += 1; _, i = num(i)
            while i < n and s[i] == ',':
                i += 1; _, i = num(i)
        elif c == 'n':                # explicit note number n<dd> (skip safely)
            i += 1; _, i = num(i)
        elif c == '$':                # engine command byte (skip up to 2 hex)
            i += 1; k = i
            while k < n and k < i + 2 and s[k] in '0123456789abcdef':
                k += 1
            i = k
        elif c == '[':
            i += 1; toks.append(('lstart',))
        elif c == ']':
            i += 1; v, i = num(i); toks.append(('lend', v if v else 1))
        elif c == '(':
            i += 1
            bang = False
            if i < n and s[i] == '!':
                bang = True; i += 1
            v, i = num(i)
            # consume optional )
            if i < n and s[i] == ')':
                i += 1
            cnt, i = num(i)               # optional recall count: (N)<count>
            toks.append(('label', v if v is not None else 0, bang, cnt))
        else:                          # & / | * ) etc -> ignore
            i += 1
    return toks


# ------------------------------------------------------------ bracket mapping --
def match_brackets(toks):
    """open_index -> (close_index, count)."""
    stack, m = [], {}
    for i, tk in enumerate(toks):
        if tk[0] == 'lstart':
            stack.append(i)
        elif tk[0] == 'lend':
            o = stack.pop()
            m[o] = (i, tk[1])
    return m


# ---------------------------------------------------------------- interpreter --
class ChannelState:
    __slots__ = ('octave', 'deflen', 'defdots', 'vol', 'gvol', 'inst',
                 'transpose', 'tempo', 't', 'events', 'last')

    def __init__(self):
        self.octave = 4
        self.deflen = DEFAULT_LEN
        self.defdots = 0
        self.vol = 200
        self.gvol = 255
        self.inst = 0
        self.transpose = 0
        self.tempo = None
        self.t = 0                      # current time in ticks
        self.events = []                # (start_tick, end_tick, note, inst, vol)
        self.last = None                # index into events of last real note


def len_ticks(value, dots, st):
    if value is None:
        value, dots0 = st.deflen, st.defdots
        dots = dots or dots0
    base = WHOLE / value
    total, add = base, base
    for _ in range(dots):
        add /= 2; total += add
    return total


def build_global_labels(chan_tokens):
    """Labelled loops (N)[..] are GLOBAL in AddmusicK: a phrase defined in one
    channel can be recalled with (N) from any channel. Collect every definition
    into one table so cross-channel recalls resolve (else those channels silently
    drop their notes). label id -> (body_tokens, body_bracketmap)."""
    glabels = {}
    for idx in sorted(chan_tokens):
        tk = chan_tokens[idx]
        bm = match_brackets(tk)
        for i, t in enumerate(tk):
            if t[0] == 'label' and i + 1 < len(tk) and tk[i + 1][0] == 'lstart':
                close, _ = bm[i + 1]
                body = tk[i + 2:close]          # tokens inside the [...]
                glabels[t[1]] = (body, match_brackets(body))
    return glabels


def interpret(toks, bm, glabels=None):
    st = ChannelState()
    glabels = glabels or {}

    def itvol():
        v = (st.vol / 255.0) * (st.gvol / 255.0) * 64.0
        return max(1, min(64, int(round(v))))

    def loop(tk, bmap, lo, hi, count):      # run tk[lo:hi] `count` times, no drift
        so, sl, sd = st.octave, st.deflen, st.defdots
        for _ in range(max(1, count)):
            st.octave, st.deflen, st.defdots = so, sl, sd
            run(tk, bmap, lo, hi)
            if st.t > MAX_TICKS:
                break

    def run(tk, bmap, lo, hi):
        i = lo
        while i < hi:
            if st.t > MAX_TICKS:
                return
            it = tk[i]; tag = it[0]
            if tag == 'note':
                dur = len_ticks(it[2], it[3], st)
                note = st.octave * 12 + it[1] + OCT_BASE + st.transpose
                while note > 119:
                    note -= 12
                while note < 0:
                    note += 12
                st.events.append([st.t, st.t + dur, note, st.inst, itvol()])
                st.last = len(st.events) - 1
                st.t += dur
            elif tag == 'rest':
                st.t += len_ticks(it[1], it[2], st)
                st.last = None
            elif tag == 'tie':
                dur = len_ticks(it[1], it[2], st)
                if st.last is not None:
                    st.events[st.last][1] += dur
                st.t += dur
            elif tag == 'oct':
                st.octave = it[1]
            elif tag == 'octup':
                st.octave += 1
            elif tag == 'octdn':
                st.octave -= 1
            elif tag == 'deflen':
                st.deflen, st.defdots = it[1], it[2]
            elif tag == 'tempo':
                if st.tempo is None:
                    st.tempo = it[1]
            elif tag == 'vol':
                st.vol = it[1]
            elif tag == 'gvol':
                st.gvol = it[1]
            elif tag == 'inst':
                st.inst = it[1]
            elif tag == 'transpose':
                st.transpose = it[1]
            elif tag == 'lstart':
                close, count = bmap[i]
                # Restore octave/length each iteration so loops whose >/< don't
                # net to zero can't drift the pitch an octave every repeat.
                loop(tk, bmap, i + 1, close, count)
                i = close + 1
                continue
            elif tag == 'label':
                nxt = i + 1
                if nxt < hi and tk[nxt][0] == 'lstart':       # (N)[..] definition
                    close, count = bmap[nxt]
                    loop(tk, bmap, nxt + 1, close, count)
                    i = close + 1
                    continue
                elif it[1] in glabels:                        # (N)<count> recall
                    body, bbm = glabels[it[1]]
                    cnt = it[3] if len(it) > 3 and it[3] else 1
                    loop(body, bbm, 0, len(body), cnt)
            i += 1

    run(toks, bm, 0, len(toks))
    return st


# ----------------------------------------------------------------- assembly ---
DRUM_CYCLE = [KICK, HAT, SNARE, CRASH]   # for single-pitch drum channels


def classify_drum(inst, note):
    """Drum sample for a percussion note. Custom samples (@30+) are explicit
    kit pieces; default-set drum notes pick a piece by pitch (low=kick ..)."""
    if inst == 30:
        return KICK
    if inst == 31:
        return SNARE
    if inst == 32:
        return HAT
    if inst >= 33:
        return CRASH
    if note <= 58:
        return KICK
    if note <= 64:
        return SNARE
    return HAT


def channel_is_perc(st):
    """Decide if a channel is percussion vs a melodic line. The hard case is a
    one/two-pitch melodic ostinato (e.g. a c-g arp) which looks narrow but is NOT
    drums -- turning it into drum hits both loses melody and adds wrong drums. So:
      - a single repeated pitch (range<=1) IS a drum hammer;
      - custom drum samples (@>=30) over a narrow range ARE drums;
      - a narrow band that SWITCHES instruments is a kit (e.g. @21/@27 here).
    A narrow line on ONE pitched instrument is treated as melodic, not drums."""
    notes = [e[2] for e in st.events]
    n = len(notes)
    if n < 8:
        return False
    rng = max(notes) - min(notes)
    distinct = len(set(notes))
    low = min(notes)
    insts = set(e[3] for e in st.events)
    ge30 = sum(1 for e in st.events if e[3] >= 30) / n
    if rng <= 1:                                       # single-pitch hammer
        return True
    if ge30 > 0.5 and rng <= 8:                        # custom drum samples
        return True
    if rng <= 7 and len(insts) >= 2 and distinct <= 4 and low >= 54:
        return True                                    # kit: switches drum pieces
    return False


def build_module(mml_path, outname, bpm_override=None):
    text = open(mml_path, 'r', encoding='utf-8', errors='ignore').read()
    chans, preamble = preprocess(text)

    tempo = preamble_tempo(preamble)    # global `t` before the first channel
    chan_tokens = {idx: tokenize(body) for idx, body in chans.items()}
    glabels = build_global_labels(chan_tokens)   # cross-channel (N) recalls
    raw = {}                            # ch -> ChannelState
    for idx, toks in sorted(chan_tokens.items()):
        bm = match_brackets(toks)
        st = interpret(toks, bm, glabels)
        raw[idx] = st
        if tempo is None and st.tempo:
            tempo = st.tempo
    if tempo is None:
        tempo = 60

    # classify each channel melodic vs percussion; render drums as one-shots.
    ev = {c: [] for c in range(8)}
    drum_rank = 0                    # single-pitch drum channels get distinct kits
    for idx, st in sorted(raw.items()):
        if idx > 7 or not st.events:
            continue
        if channel_is_perc(st):
            notes = [e[2] for e in st.events]
            single = (max(notes) - min(notes)) <= 1
            fixed = None
            if single:               # one pitch -> one kit piece (kick, hat, ..)
                fixed = DRUM_CYCLE[drum_rank % len(DRUM_CYCLE)]
                drum_rank += 1
            for (s_t, e_t, note, inst, vol) in st.events:
                sr = int(round(s_t / TICKS_PER_ROW))
                drum = fixed if fixed is not None else classify_drum(inst, note)
                ev[idx].append((sr, sr + 1, 60, drum, vol))   # native-pitch hit
        else:
            for (s_t, e_t, note, inst, vol) in st.events:
                sr = int(round(s_t / TICKS_PER_ROW))
                er = int(round(e_t / TICKS_PER_ROW))
                if er <= sr:
                    er = sr + 1
                ev[idx].append((sr, er, note, CH_VOICE[idx], vol))

    bpm = bpm_override or max(40, min(240, round(tempo * AMK_T_TO_BPM)))
    it_tempo = max(32, min(255, bpm))   # speed=3, R=8 rows/quarter -> tempo==bpm
    patterns, orders = to_patterns(ev)
    smps = instruments()

    it_path = os.path.join(RES, f"music_{outname}.it")
    size = write_it(it_path, f"DEADFALL {outname.upper()}"[:25], smps,
                    patterns=patterns, orders=orders, channels=8,
                    speed=IT_SPEED, tempo=it_tempo)
    wav_path = os.path.join(RES, f"{outname}_preview.wav")
    render_wav(wav_path, smps, ev, it_tempo, speed=IT_SPEED)

    counts = {c: len(ev[c]) for c in range(8) if ev[c]}
    rows = max((e[1] for c in range(8) for e in ev[c]), default=0)
    print(f"{outname:10s} src={os.path.basename(mml_path)}")
    print(f"  amk tempo {tempo} -> {bpm}bpm / IT tempo {it_tempo}, "
          f"{len(patterns)} patterns, {rows} rows")
    print(f"  events/chan {counts}")
    print(f"  wrote {it_path} ({size} bytes), preview {os.path.basename(wav_path)}")
    return size


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit("usage: mml2it.py <song.txt> <outname>")
    build_module(sys.argv[1], sys.argv[2])
