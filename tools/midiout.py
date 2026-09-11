"""Escritor de Standard MIDI File, formato 1. Sem dependencia."""

import struct


def _vlq(v):
    out = bytearray([v & 0x7F])
    v >>= 7
    while v:
        out.insert(0, (v & 0x7F) | 0x80)
        v >>= 7
    return bytes(out)


def _track(events, name=None):
    """events: [(tick, status, d1, d2)] em ticks absolutos."""
    data = bytearray()

    if name:
        nm = name.encode()
        data += _vlq(0) + b"\xFF\x03" + _vlq(len(nm)) + nm

    last = 0
    for tick, status, d1, d2 in sorted(events, key=lambda e: (e[0], e[1] & 0xF0)):
        data += _vlq(tick - last)
        data += bytes([status, d1, d2])
        last = tick

    data += _vlq(0) + b"\xFF\x2F\x00"
    return b"MTrk" + struct.pack(">I", len(data)) + bytes(data)


def write(path, tracks, ppq=480, bpm=140):
    """tracks: [(nome, [(pos_em_semicolcheias, pitch, dur, vel)])]"""
    chunks = []

    tempo = int(round(60_000_000 / bpm))
    head = bytearray()
    head += _vlq(0) + b"\xFF\x51\x03" + bytes([(tempo >> 16) & 0xFF, (tempo >> 8) & 0xFF, tempo & 0xFF])
    head += _vlq(0) + b"\xFF\x58\x04" + bytes([4, 2, 24, 8])
    head += _vlq(0) + b"\xFF\x2F\x00"
    chunks.append(b"MTrk" + struct.pack(">I", len(head)) + bytes(head))

    step = ppq // 4

    for ch, (name, notes) in enumerate(tracks):
        if ch >= 9:
            ch += 1                      # pula o canal de bateria
        ev = []
        for pos, pitch, dur, vel in notes:
            pitch = max(0, min(127, int(pitch)))
            vel = max(1, min(127, int(vel)))
            on = int(pos * step)
            off = on + max(step // 4, int(dur * step))
            ev.append((on, 0x90 | ch, pitch, vel))
            ev.append((off, 0x80 | ch, pitch, 0))
        chunks.append(_track(ev, name))

    hdr = b"MThd" + struct.pack(">Ihhh", 6, 1, len(chunks), ppq)

    with open(path, "wb") as f:
        f.write(hdr + b"".join(chunks))
