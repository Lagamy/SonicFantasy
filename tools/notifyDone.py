"""
Plays a short ring at 10% volume, to tell the user a prompt is done.

Run from anywhere:  python3 tools/notifyDone.py [volume]      (volume 0..1, default 0.1)
The ring (two decaying tones, twice) is written once to ~/.cache/sonicsunset-notify/ring.wav and played with pw-play (PipeWire), paplay or ffplay, whichever is installed first;
the player's own volume is set, so the file stays at full scale. Nothing else is touched. Exits silently if no player exists.
"""
import math, os, shutil, struct, subprocess, sys, wave

CACHE = os.path.expanduser("~/.cache/sonicsunset-notify/")
RING = CACHE + "ring.wav"
RATE = 44100


def make_ring():
    os.makedirs(CACHE, exist_ok=True)
    frames = bytearray()
    for start in (0.0, 0.28):
        for i in range(int(RATE * 0.5)):
            t = i / RATE
            envelope = math.exp(-6.0 * t) * min(1.0, t * 200.0)
            sample = envelope * (0.6 * math.sin(2 * math.pi * 880 * t) + 0.4 * math.sin(2 * math.pi * 1320 * t))
            index = int((start + t) * RATE) * 2
            while len(frames) < index + 2:
                frames += b"\0\0"
            old = struct.unpack_from("<h", frames, index)[0]
            struct.pack_into("<h", frames, index, max(-32768, min(32767, old + int(sample * 20000))))
    with wave.open(RING, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(frames))


def main():
    volume = float(sys.argv[1]) if len(sys.argv) > 1 else 0.1
    if not os.path.exists(RING):
        make_ring()
    if shutil.which("pw-play"):
        command = ["pw-play", f"--volume={volume}", RING]
    elif shutil.which("paplay"):
        command = ["paplay", f"--volume={int(volume * 65536)}", RING]
    elif shutil.which("ffplay"):
        command = ["ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet", "-volume", str(int(volume * 100)), RING]
    else:
        return
    subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


if __name__ == "__main__":
    main()
