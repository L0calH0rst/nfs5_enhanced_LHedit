"""Extrahiert Geräusche aus einer EA-Soundbank (BNKl v4, PT-Header, EA-XA v1, mono/stereo) als WAV.

Aufruf: python -I extract_bnk_sound.py <fesfx.bnk> <Nummer> <Ausgabe.wav>
        python -I extract_bnk_sound.py <fesfx.bnk> alle <Ordner>
"""
import os
import struct
import sys
import wave

EA_XA_TABLE = [0, 240, 460, 392, 0, 0, -208, -220]


def parse_pt(d, p):
    """PT-Header ab p ('PT\\0\\0'), liefert {Tag: Wert}. Tags: 0x80 Version, 0x82 Kanäle, 0x83 Codec,
    0x84 Rate, 0x85 Samples, 0x88 Datenoffset."""
    assert d[p:p + 2] == b"PT", "kein PT-Header"
    q = p + 4
    vals = {}
    while q < len(d):
        b = d[q]
        q += 1
        if b == 0xFF:
            break
        if b in (0xFC, 0xFD, 0xFE):
            continue
        ln = d[q]
        q += 1
        vals[b] = int.from_bytes(d[q:q + ln], "big")
        q += ln
    return vals


def xa_sample(nib, shift, c1, c2, h1, h2):
    s = (nib << 28) & 0xFFFFFFFF
    if s & 0x80000000:
        s -= 1 << 32
    s >>= shift
    s = (s + h1 * c1 + h2 * c2 + 128) >> 8
    return max(-32768, min(32767, s))


def decode_ea_xa(d, off, samples, channels):
    """EA-XA v1. Rahmen je Kanal 15 Byte = Kopf (Koeffizient<<4 | Shift) + 14 Byte Nibbles (hoch zuerst).
    Stereo: Rahmen L und R folgen direkt aufeinander (30 Byte je 28 Samples, aus den Daten abgelesen).
    Liefert verschachtelte 16-Bit-Samples."""
    chans = [[] for _ in range(channels)]
    hist = [[0, 0] for _ in range(channels)]
    while len(chans[0]) < samples and off + 15 * channels <= len(d):
        for ch in range(channels):
            frame = off + 15 * ch
            info = d[frame]
            c1, c2, shift = EA_XA_TABLE[(info >> 4) & 3], EA_XA_TABLE[((info >> 4) & 3) + 4], (info & 0x0F) + 8
            h1, h2 = hist[ch]
            for i in range(28):
                byte = d[frame + 1 + i // 2]
                s = xa_sample((byte >> 4) if i % 2 == 0 else (byte & 0x0F), shift, c1, c2, h1, h2)
                h2, h1 = h1, s
                chans[ch].append(s)
            hist[ch] = [h1, h2]
        off += 15 * channels
    n = min(samples, len(chans[0]))
    return [chans[ch][i] for i in range(n) for ch in range(channels)]


def entries(d):
    count = struct.unpack_from("<H", d, 6)[0]
    for index in range(count):
        entry = 0x10 + 4 * index
        offset = struct.unpack_from("<I", d, entry)[0]
        pos = entry + offset
        if offset and pos + 2 <= len(d) and d[pos:pos + 2] == b"PT":
            yield index, parse_pt(d, pos)


def write_wav(path, pcm, channels, rate):
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack(f"<{len(pcm)}h", *pcm))


def extract(d, index, vals, wav):
    channels = vals.get(0x82, 1)
    codec = vals.get(0x83, -1)
    rate = vals.get(0x84, 22050)
    samples = vals[0x85]
    assert codec in (-1, 0x0A), f"Codec {codec} nicht umgesetzt"
    pcm = decode_ea_xa(d, vals[0x88], samples, channels)
    write_wav(wav, pcm, channels, rate)
    peak = max((abs(x) for x in pcm), default=0)
    print(f"Nr. {index:2d}: {channels} Kanal/Kanäle, {rate} Hz, {samples / rate:5.2f} s, Daten {vals[0x88]}, Spitze {peak}"
          f" -> {wav}")


def main(bnk, which, out):
    """which = Nummer -> out ist die WAV-Datei; which = 'alle' -> out ist ein Ordner (<bank>_NN.wav)."""
    d = open(bnk, "rb").read()
    assert d[:4] == b"BNKl"
    found = dict(entries(d))
    if which == "alle":
        os.makedirs(out, exist_ok=True)
        stem = os.path.splitext(os.path.basename(bnk))[0]
        for index, vals in found.items():
            extract(d, index, vals, os.path.join(out, f"{stem}_{index:02d}.wav"))
    else:
        extract(d, int(which), found[int(which)], out)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
