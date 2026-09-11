"""Leitor de Standard MIDI File. Sem dependencia nenhuma.

Devolve notas com inicio e duracao em ticks absolutos, ja casando cada note-on
com o seu note-off -- que e o detalhe que a tentativa anterior errou e fez tudo
emendar sem pausa.
"""

import struct
from collections import defaultdict


class Note:
    __slots__ = ("pitch", "start", "dur", "vel", "chan", "track")

    def __init__(self, pitch, start, dur, vel, chan, track):
        self.pitch = pitch
        self.start = start
        self.dur = dur
        self.vel = vel
        self.chan = chan
        self.track = track

    def __repr__(self):
        return f"N({self.pitch},{self.start},{self.dur})"


class MidiFile:
    def __init__(self, notes, ppq, time_sigs, tempos, n_tracks):
        self.notes = notes
        self.ppq = ppq
        self.time_sigs = time_sigs      # [(tick, num, den)]
        self.tempos = tempos            # [(tick, usec_per_quarter)]
        self.n_tracks = n_tracks

    @property
    def bpm(self):
        if not self.tempos:
            return None
        return 60_000_000.0 / self.tempos[0][1]


def _vlq(data, i):
    v = 0
    while True:
        b = data[i]
        i += 1
        v = (v << 7) | (b & 0x7F)
        if not (b & 0x80):
            return v, i


def read(path):
    with open(path, "rb") as f:
        data = f.read()

    if data[:4] != b"MThd":
        raise ValueError("nao e MIDI")

    (hdr_len,) = struct.unpack(">I", data[4:8])
    fmt, ntrks, division = struct.unpack(">hhh", data[8:14])

    if division <= 0:
        raise ValueError("SMPTE nao suportado")

    ppq = division
    i = 8 + hdr_len

    notes = []
    time_sigs = []
    tempos = []
    track_idx = 0

    while i < len(data) - 8:
        if data[i:i + 4] != b"MTrk":
            # chunk desconhecido: pula pelo tamanho declarado
            (ln,) = struct.unpack(">I", data[i + 4:i + 8])
            i += 8 + ln
            continue

        (trk_len,) = struct.unpack(">I", data[i + 4:i + 8])
        j = i + 8
        end = min(j + trk_len, len(data))

        tick = 0
        status = 0
        open_notes = defaultdict(list)   # (chan, pitch) -> [(start, vel)]

        while j < end:
            delta, j = _vlq(data, j)
            tick += delta

            if j >= end:
                break

            b = data[j]

            if b & 0x80:
                status = b
                j += 1
            # senao: running status, reaproveita o anterior

            ev = status & 0xF0
            chan = status & 0x0F

            if status == 0xFF:
                meta = data[j]
                j += 1
                ln, j = _vlq(data, j)
                payload = data[j:j + ln]
                j += ln

                if meta == 0x51 and ln == 3:
                    tempos.append((tick, (payload[0] << 16) | (payload[1] << 8) | payload[2]))
                elif meta == 0x58 and ln >= 2:
                    time_sigs.append((tick, payload[0], 1 << payload[1]))
                elif meta == 0x2F:
                    break

            elif status in (0xF0, 0xF7):
                ln, j = _vlq(data, j)
                j += ln

            elif ev in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                d1 = data[j]
                d2 = data[j + 1]
                j += 2

                if ev == 0x90 and d2 > 0:
                    open_notes[(chan, d1)].append((tick, d2))
                elif ev == 0x80 or (ev == 0x90 and d2 == 0):
                    stack = open_notes.get((chan, d1))
                    if stack:
                        start, vel = stack.pop(0)
                        if tick > start:
                            notes.append(Note(d1, start, tick - start, vel, chan, track_idx))

            elif ev in (0xC0, 0xD0):
                j += 1

            else:
                # status invalido: aborta a track em vez de ler lixo
                break

        i = i + 8 + trk_len
        track_idx += 1

    notes.sort(key=lambda n: (n.start, n.pitch))
    return MidiFile(notes, ppq, time_sigs, tempos, track_idx)
