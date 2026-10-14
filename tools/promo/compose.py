#!/usr/bin/env python3
"""Builds the promo video from a recording (record.sh) and the cards in card.html:
docs/promo/superstrike-studio.mp4 (1920x1080, ~45 s) and docs/promo/superstrike-studio.gif.
Usage: compose.py REC_PREFIX OUT_DIR    (needs ffmpeg and Chromium: $CHROME or chromium)"""
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
REC, OUT = sys.argv[1], sys.argv[2]
CHROME = os.environ.get("CHROME") or shutil.which("chromium") or shutil.which("google-chrome")
FADE = 0.4
LAG = 0.5  # the recorded app shows a click about this much after drive.py marks it
CAPTIONS = {  # scene: (title, text)
    "hits": ("HITS tuning, per button", "Actuation point, rapid trigger and click haptics, previewed live on the mouse"),
    "test": ("Feel it right away", "Live click view and a test pad"),
    "save": ("Save to mouse", "Everything goes into the mouse's onboard memory, haptics included"),
    "buttons": ("Remap any button", "Mouse, keyboard, media, DPI and profile functions"),
    "sensor": ("DPI 100–48 000, polling up to 8 000 Hz", "Five DPI stages, switch them live"),
    "profiles": ("Five onboard profiles", "They work on any PC and in any game, with no software running"),
}
ORDER = ["card:title:3.5", "hits", "test", "save", "card:stats:4.5", "buttons", "sensor", "profiles", "card:outro:5"]


def run(*cmd):
    subprocess.run(cmd, check=True)


def card(work, name, query):
    png = os.path.join(work, name + ".png")
    url = "file://" + os.path.join(HERE, "card.html") + "?" + query
    run(CHROME, "--headless=new", "--no-sandbox", "--disable-gpu", "--hide-scrollbars",
        "--allow-file-access-from-files", "--window-size=1920,1080", "--virtual-time-budget=2000",
        "--screenshot=" + png, url)
    return png


def main():
    marks = {}
    for line in open(REC + ".marks"):
        _, name, t = line.split()
        marks[name] = float(t)
    names = list(marks)
    work = tempfile.mkdtemp()
    enc = ["-c:v", "libx264", "-preset", "medium", "-crf", "16", "-pix_fmt", "yuv420p", "-r", "30"]
    segs = []
    for item in ORDER:
        seg = os.path.join(work, f"seg{len(segs):02d}.mp4")
        if item.startswith("card:"):
            _, name, dur = item.split(":")
            png = card(work, name, "s=" + name)
            run("ffmpeg", "-loglevel", "error", "-y", "-loop", "1", "-t", dur, "-i", png, *enc, seg)
            segs.append((seg, float(dur)))
        else:
            start, end = marks[item], marks[names[names.index(item) + 1]]
            h, p = CAPTIONS[item]
            bg = card(work, "bg-" + item, urllib.parse.urlencode({"s": "scene", "h": h, "p": p}))
            dur = end - start + 0.3
            run("ffmpeg", "-loglevel", "error", "-y", "-loop", "1", "-t", f"{dur:.2f}", "-i", bg,
                "-ss", f"{start + LAG - 0.15:.2f}", "-t", f"{dur:.2f}", "-i", REC + ".mp4",
                "-filter_complex", "[0][1]overlay=305:196:shortest=1", *enc, seg)
            segs.append((seg, dur))

    # chain the segments with cross-fades
    inputs, chain, t = [], "", 0.0
    for i, (seg, dur) in enumerate(segs):
        inputs += ["-i", seg]
        if i == 0:
            prev, t = "[0:v]", dur
            continue
        t -= FADE
        label = f"[v{i}]"
        chain += f"{prev}[{i}:v]xfade=transition=fade:duration={FADE}:offset={t:.2f}{label};"
        prev, t = label, t + dur
    chain += f"{prev}fade=t=in:st=0:d=0.5,fade=t=out:st={t - 0.6:.2f}:d=0.6,format=yuv420p[out]"
    os.makedirs(OUT, exist_ok=True)
    mp4 = os.path.join(OUT, "superstrike-studio.mp4")
    run("ffmpeg", "-loglevel", "error", "-y", *inputs, "-filter_complex", chain, "-map", "[out]",
        "-c:v", "libx264", "-preset", "slow", "-crf", "20", "-pix_fmt", "yuv420p", "-movflags", "+faststart", mp4)
    # README GIF: the app part (after the title card), 960 px wide, 12 fps
    gif = os.path.join(OUT, "superstrike-studio.gif")
    run("ffmpeg", "-loglevel", "error", "-y", "-ss", "3.2", "-t", f"{t - 3.2 - 5:.2f}", "-i", mp4, "-vf",
        "fps=12,scale=960:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];"
        "[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle", gif)
    if not os.environ.get("KEEP"):
        shutil.rmtree(work)
    else:
        print(work)
    print(f"{mp4} ({t:.1f} s)\n{gif}")


main()
