#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32 TURBO BUILDER
arduino-cli compiler dengan tampilan modern + animasi.
Tanpa dependensi tambahan (pure Python + ANSI truecolor), jalan di Termux/proot.

Mode build:
  1) Hanya file .ino
  2) File .ino + tambahan (.h / .cpp) yang di-#include "..." dari .ino

Pemakaian:
  python build.py            -> tampil menu pilih mode
  python build.py --ino      -> langsung mode 1
  python build.py --full     -> langsung mode 2
"""
import atexit
import math
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from collections import deque

# ═══════════════════════════════════════════════════════════
# KONFIGURASI UTAMA
# ═══════════════════════════════════════════════════════════
DOWNLOAD_DIR = "/sdcard/Download/Upload"
SKETCH_DIR = "/root/ESP32_SmartDisplay_MotoNav_Combined"
INO_FILENAME = "ESP32_SmartDisplay_MotoNav_Combined.ino"
FQBN = "esp32:esp32:esp32"
PARTITION = "huge_app"

BUILD_DIR = os.path.join(SKETCH_DIR, "build", FQBN.replace(":", "."))
MERGED_BIN = INO_FILENAME + ".merged.bin"

CPU_JOBS = "5"                              # aman untuk RAM 5.5GB
CACHE_DIR = "/root/arduino_build_cache"     # cache agar compile berikutnya ngebut
LOG_FILE = "/root/last_compile.log"         # log lengkap compile terakhir

src_ino = os.path.join(DOWNLOAD_DIR, INO_FILENAME)
dest_ino = os.path.join(SKETCH_DIR, INO_FILENAME)

# ═══════════════════════════════════════════════════════════
# MODE BUILD
# ═══════════════════════════════════════════════════════════
MODE_INO, MODE_FULL = "ino", "full"
MODE_LABEL = {
    MODE_INO: "Hanya file .ino",
    MODE_FULL: "File .ino + tambahan (.h/.cpp)",
}
EXT_SRC = (".h", ".hpp", ".c", ".cpp", ".cc", ".S")
RE_INC = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)
MODE = MODE_INO
EXTRAS_COPIED = []      # file tambahan yang berhasil disalin
EXTRAS_MISSING = []     # #include "..." yang tidak ditemukan di Download

# ═══════════════════════════════════════════════════════════
# WARNA & GRADIENT
# ═══════════════════════════════════════════════════════════
TTY = sys.stdout.isatty()
RESET = "\033[0m" if TTY else ""
BOLD = "\033[1m" if TTY else ""


def fg(r, g, b):
    return f"\033[38;2;{int(r)};{int(g)};{int(b)}m" if TTY else ""


PALETTE = [(0, 229, 255), (124, 77, 255), (255, 64, 129)]   # cyan -> violet -> pink
GREEN = fg(80, 250, 123)
RED = fg(255, 85, 85)
YELLOW = fg(255, 203, 107)
WHITE = fg(235, 235, 245)
GRAY = fg(110, 110, 135)
DARK = fg(55, 55, 78)


def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def grad(t):
    t = min(max(t, 0.0), 1.0) * (len(PALETTE) - 1)
    i = min(int(t), len(PALETTE) - 2)
    return lerp(PALETTE[i], PALETTE[i + 1], t - i)


def gtext(text, shift=0.0, bold=False):
    """Teks dengan gradient yang bisa 'bergeser' (efek shimmer)."""
    n = max(len(text) - 1, 1)
    out = []
    for i, ch in enumerate(text):
        u = (i / n + shift) % 1.0
        out.append(fg(*grad(1 - abs(2 * u - 1))) + ch)
    return (BOLD if bold else "") + "".join(out) + RESET


def term_width():
    return min(shutil.get_terminal_size((60, 20)).columns, 70)


def fmt_dur(d):
    if d < 60:
        return f"{d:.1f}s"
    return f"{int(d // 60)}m{int(d % 60):02d}s"


def fmt_clock(d):
    m, s = divmod(int(d), 60)
    return f"{m:02d}:{s:02d}"


def mini_bar(pct, width=22):
    pct = max(0, min(100, pct))
    full = int(round(pct / 100 * width))
    col = GREEN if pct < 70 else (YELLOW if pct < 90 else RED)
    return col + "█" * full + DARK + "░" * (width - full) + RESET


# ═══════════════════════════════════════════════════════════
# UI ANIMASI
# ═══════════════════════════════════════════════════════════
STEPS = [
    "Validasi file sumber",
    "Sinkronisasi sketch",
    "Analisis library",
    "Kompilasi sketch",
    "Kompilasi library",
    "Kompilasi core ESP32",
    "Linking firmware",
    "Membuat merged.bin",
    "Export ke Download",
]
NAMEW = max(len(s) for s in STEPS)

# step -> (persen awal, persen akhir, tau detik)
STAGES = {
    0: (0, 3, 2), 1: (3, 8, 2), 2: (8, 18, 6), 3: (18, 40, 12), 4: (40, 58, 12),
    5: (58, 82, 14), 6: (82, 91, 5), 7: (91, 96, 5), 8: (96, 99, 2),
}
SPIN = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏"
PART = " ▏▎▍▌▋▊▉"

RE_FLASH = re.compile(r"Sketch uses (\d+) bytes \((\d+)%\).*?Maximum is (\d+) bytes")
RE_RAM = re.compile(r"Global variables use (\d+) bytes \((\d+)%\).*?Maximum is (\d+) bytes")
RE_OUT = re.compile(r"\s-o\s+(\S+)")


class UI:
    def __init__(self):
        self.steps = [{"name": n, "st": "pending", "t0": None, "t1": None} for n in STEPS]
        self.cur = -1
        self.step_t = time.time()
        self.t_start = time.time()
        self.disp = 0.0
        self.frame = 0
        self.log = "menunggu..."
        self.warn = 0
        self.err = 0
        self.mem = {}
        self.done = False
        self.failed = False
        self.running = False
        self.drawn = 0
        self.thread = None
        self.lock = threading.Lock()

    # ---------- kontrol ----------
    def start(self):
        self.t_start = time.time()
        if not TTY:
            return
        sys.stdout.write("\033[?25l")           # sembunyikan kursor
        sys.stdout.flush()
        atexit.register(lambda: sys.stdout.write("\033[?25h"))
        self.running = True
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.thread.start()

    def set_step(self, i):
        with self.lock:
            if i <= self.cur:
                return
            now = time.time()
            for j in range(0, i):
                s = self.steps[j]
                if s["st"] in ("pending", "active"):
                    s["st"] = "done"
                    s["t0"] = s["t0"] or now
                    s["t1"] = now
            self.steps[i]["st"] = "active"
            self.steps[i]["t0"] = now
            self.cur = i
            self.step_t = now
        if not TTY:
            print(f"[{i + 1}/{len(STEPS)}] {STEPS[i]}")

    def finish(self, ok):
        with self.lock:
            now = time.time()
            if ok:
                for s in self.steps:
                    if s["st"] != "done":
                        s["t0"] = s["t0"] or now
                        s["t1"] = now
                        s["st"] = "done"
                self.done = True
            else:
                self.failed = True
                if 0 <= self.cur < len(self.steps):
                    s = self.steps[self.cur]
                    s["st"] = "fail"
                    s["t1"] = now

    def stop(self):
        if not TTY:
            return
        if self.done:                           # biarkan bar menuntaskan ke 100%
            end = time.time() + 2.0
            while self.disp < 99.6 and time.time() < end:
                time.sleep(0.04)
        time.sleep(0.12)
        self.running = False
        if self.thread:
            self.thread.join()
        self._draw()
        sys.stdout.write("\033[?25h")
        sys.stdout.flush()

    # ---------- parsing output arduino-cli ----------
    def feed(self, line):
        low = line.lower()
        if "detecting libraries" in low or "generating function prototypes" in low:
            self.set_step(2)
        elif "compiling sketch" in low:
            self.set_step(3)
        elif "compiling libraries" in low:
            self.set_step(4)
        elif "compiling core" in low:
            self.set_step(5)
        elif "linking everything" in low:
            self.set_step(6)
        elif self.cur >= 6 and ("merged" in low or "esptool" in low or
                                ("creating" in low and ".bin" in low)):
            self.set_step(7)

        if re.search(r"\bwarning:", low):
            self.warn += 1
        if re.search(r"\berror:", low):
            self.err += 1

        m = RE_FLASH.search(line)
        if m:
            self.mem["flash"] = tuple(int(x) for x in m.groups())
        m = RE_RAM.search(line)
        if m:
            self.mem["ram"] = tuple(int(x) for x in m.groups())

        txt = line.strip()
        if txt:
            m = RE_OUT.search(line)
            if m:
                txt = "build  " + os.path.basename(m.group(1))
            elif low.startswith("using library"):
                txt = "lib    " + txt.split()[2]
            self.log = " ".join(txt.split())

    # ---------- render ----------
    def _target(self):
        if self.done:
            return 100.0
        if self.cur < 0:
            return 0.0
        s, e, tau = STAGES[self.cur]
        t = time.time() - self.step_t
        return s + (e - s) * 0.92 * (1 - math.exp(-t / tau))

    def _loop(self):
        while self.running:
            self._draw()
            time.sleep(1 / 15)

    def _render(self):
        W = term_width()
        f = self.frame
        now = time.time()
        lines = []

        for s in self.steps:
            st = s["st"]
            if st == "done":
                icon, name = GREEN + "✔", WHITE + s["name"].ljust(NAMEW) + RESET
                ts = fmt_dur(s["t1"] - s["t0"]) if s["t0"] else ""
            elif st == "active":
                icon = fg(*grad((f % 24) / 23)) + SPIN[f % len(SPIN)]
                name = gtext(s["name"].ljust(NAMEW), shift=(f * 0.03) % 1.0, bold=True)
                ts = fmt_dur(now - s["t0"])
            elif st == "fail":
                icon, name = RED + "✖", RED + s["name"].ljust(NAMEW) + RESET
                ts = fmt_dur(s["t1"] - s["t0"]) if s["t0"] else ""
            else:
                icon, name = DARK + "○", DARK + s["name"].ljust(NAMEW) + RESET
                ts = ""
            lines.append(f"  {icon}{RESET} {name} {GRAY}{ts}{RESET}")

        lines.append("")

        # progress bar (gradient + shimmer)
        target = self._target()
        rate = 0.25 if self.done else 0.10
        if target > self.disp:
            self.disp += (target - self.disp) * rate
        pct = min(self.disp, 100.0)
        bw = max(10, min(34, W - 24))
        total = int(pct / 100 * bw * 8)
        full, rem = divmod(total, 8)
        bar = []
        for i in range(bw):
            if i < full or (i == full and rem):
                r, g, b = grad(i / max(bw - 1, 1))
                glow = max(0.0, math.sin(f * 0.35 - i * 0.5)) ** 6 * 0.6
                r, g, b = r + (255 - r) * glow, g + (255 - g) * glow, b + (255 - b) * glow
                ch = "█" if i < full else PART[rem]
                bar.append(fg(r, g, b) + ch)
            else:
                bar.append(DARK + "░")
        bar = "".join(bar) + RESET
        pct_col = GREEN if self.done else (RED if self.failed else WHITE)
        lines.append(f"  {bar} {pct_col}{BOLD}{int(round(pct)):3d}%{RESET}  "
                     f"{GRAY}⏱ {fmt_clock(now - self.t_start)}{RESET}")

        # log live
        maxlen = max(10, W - 6)
        txt = self.log if len(self.log) <= maxlen else self.log[: maxlen - 1] + "…"
        lines.append(f"  {GRAY}↳ {txt}{RESET}")

        # counter warning / error
        if self.warn or self.err:
            c = f"  {YELLOW}▲ {self.warn} warning{RESET}"
            if self.err:
                c += f"   {RED}✖ {self.err} error{RESET}"
            lines.append(c)
        else:
            lines.append("")
        return lines

    def _draw(self):
        with self.lock:
            lines = self._render()
            self.frame += 1
        out = ""
        if self.drawn:
            out += f"\033[{self.drawn}F"
        for ln in lines:
            out += "\033[2K" + ln + "\n"
        self.drawn = len(lines)
        sys.stdout.write(out)
        sys.stdout.flush()


# ═══════════════════════════════════════════════════════════
# PILIH MODE & FILE TAMBAHAN
# ═══════════════════════════════════════════════════════════
def choose_mode():
    args = sys.argv[1:]
    if "--ino" in args:
        return MODE_INO
    if "--full" in args:
        return MODE_FULL
    if not sys.stdin.isatty():
        return MODE_INO
    print()
    print(gtext("  PILIH MODE BUILD", bold=True))
    print(f"  {GREEN}1{RESET} {WHITE}Hanya file .ino{RESET}")
    print(f"  {GREEN}2{RESET} {WHITE}File .ino + tambahan (.h / .cpp){RESET}")
    while True:
        try:
            c = input(f"  {GRAY}Pilih [1/2] (default 1): {RESET}").strip() or "1"
        except (EOFError, KeyboardInterrupt):
            print()
            sys.exit(130)
        if c == "1":
            return MODE_INO
        if c == "2":
            return MODE_FULL
        print(f"  {RED}Masukkan 1 atau 2{RESET}")


def clean_stale_sources():
    """Hapus file source tambahan lama di folder sketch (level atas saja)."""
    for f in os.listdir(SKETCH_DIR):
        p = os.path.join(SKETCH_DIR, f)
        if os.path.isfile(p) and f.endswith(EXT_SRC):
            os.remove(p)


def collect_extras():
    """Telusuri #include "..." dari .ino secara rekursif, cari di folder Download."""
    found, missing, seen = [], [], set()
    queue = [src_ino]
    while queue:
        cur = queue.pop()
        try:
            with open(cur, encoding="utf-8", errors="replace") as fh:
                text = fh.read()
        except OSError:
            continue
        base_rel = os.path.relpath(os.path.dirname(cur), DOWNLOAD_DIR)
        for inc in RE_INC.findall(text):
            rel = os.path.normpath(os.path.join(base_rel, inc))
            if rel.startswith("..") or rel in seen:
                continue
            seen.add(rel)
            if os.path.isfile(os.path.join(DOWNLOAD_DIR, rel)):
                found.append(rel)
                queue.append(os.path.join(DOWNLOAD_DIR, rel))
                stem = os.path.splitext(rel)[0]          # pasangan .cpp/.c
                for ext in (".cpp", ".c", ".cc"):
                    pair = stem + ext
                    if pair not in seen and os.path.isfile(os.path.join(DOWNLOAD_DIR, pair)):
                        seen.add(pair)
                        found.append(pair)
                        queue.append(os.path.join(DOWNLOAD_DIR, pair))
            else:
                missing.append(rel)
    return found, missing


# ═══════════════════════════════════════════════════════════
# BANNER & RINGKASAN
# ═══════════════════════════════════════════════════════════
def accent(i, n):
    return fg(*grad(i / max(n - 1, 1))) + "┃" + RESET


def banner():
    W = term_width()
    info = [
        ("Mode", MODE_LABEL[MODE]),
        ("Board", FQBN),
        ("Partisi", PARTITION),
        ("CPU jobs", f"{CPU_JOBS} core"),
        ("Cache", CACHE_DIR),
    ]
    rows = 2 + 1 + len(info)
    print()
    print(gtext("━" * W))
    title = "⚡ ESP32 TURBO BUILDER"
    if TTY:                                     # efek shimmer pada judul
        for k in range(16):
            sys.stdout.write("\r" + accent(0, rows) + "  " + gtext(title, k * 0.04, True))
            sys.stdout.flush()
            time.sleep(0.035)
        print()
    else:
        print(accent(0, rows) + "  " + title)
    seq = [GRAY + "SmartDisplay · MotoNav Combined" + RESET, ""]
    seq += [f"{GRAY}{k.ljust(9)}{RESET}{WHITE}{v}{RESET}" for k, v in info]
    for i, row in enumerate(seq, start=1):
        print(accent(i, rows) + "  " + row)
        if TTY:
            time.sleep(0.04)
    print(gtext("━" * W, shift=0.5))
    print()


def find_merged_bin():
    """Cari *.merged.bin: lokasi standar dulu, lalu scan folder build."""
    standar = os.path.join(BUILD_DIR, MERGED_BIN)
    if os.path.exists(standar):
        return standar
    for root, _, files in os.walk(os.path.join(SKETCH_DIR, "build")):
        for f in files:
            if f.endswith(".merged.bin"):
                return os.path.join(root, f)
    return None


def panel(color, title, rows):
    print()
    print(color + "┃ " + BOLD + title + RESET)
    for r in rows:
        print(color + "┃ " + RESET + r)
    print()


def notify(ok, text):
    """Notifikasi + getar Termux (diabaikan kalau Termux:API tidak ada)."""
    try:
        if shutil.which("termux-notification"):
            subprocess.Popen(
                ["termux-notification", "--title",
                 "Build ESP32 " + ("berhasil ✔" if ok else "gagal ✖"), "--content", text],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if shutil.which("termux-vibrate"):
            subprocess.Popen(["termux-vibrate", "-d", "250" if ok else "700"],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception:
        pass


def show_failure(ui, tail, reason):
    W = term_width()
    errs = [l for l in tail if re.search(r"\berror\b", l, re.I) and "-Werror" not in l]
    show = (errs or list(tail))[-8:]
    rows = []
    for l in show:
        l = " ".join(l.split())
        rows.append(RED + (l if len(l) <= W - 4 else l[: W - 5] + "…") + RESET)
    if MODE == MODE_FULL and EXTRAS_MISSING:
        rows.append("")
        rows.append(f"{YELLOW}Include tidak ditemukan di Download:{RESET}")
        for e in EXTRAS_MISSING:
            rows.append(f"{YELLOW}? {e}{RESET}")
    rows.append("")
    rows.append(f"{GRAY}Log lengkap : {WHITE}{LOG_FILE}{RESET}")
    panel(RED, f"✖ BUILD GAGAL — {reason}", rows)
    notify(False, reason)


def show_success(ui, path, elapsed):
    ukuran = os.path.getsize(path) / 1024
    rows = [
        f"{GRAY}{'Mode'.ljust(9)}{RESET}{WHITE}{MODE_LABEL[MODE]}{RESET}",
        f"{GRAY}{'File'.ljust(9)}{RESET}{WHITE}{os.path.basename(path)}{RESET}",
        f"{GRAY}{'Ukuran'.ljust(9)}{RESET}{WHITE}{ukuran:,.0f} KB{RESET}",
        f"{GRAY}{'Lokasi'.ljust(9)}{RESET}{WHITE}Download/{RESET}",
        f"{GRAY}{'Waktu'.ljust(9)}{RESET}{WHITE}{fmt_dur(elapsed)}{RESET}",
    ]
    if MODE == MODE_FULL:
        rows.append(f"{GRAY}{'Tambahan'.ljust(9)}{RESET}{WHITE}{len(EXTRAS_COPIED)} file{RESET}")
        for e in EXTRAS_COPIED:
            rows.append(f"{GRAY}{''.ljust(9)}{RESET}{GREEN}+ {e}{RESET}")
        for e in EXTRAS_MISSING:
            rows.append(f"{GRAY}{''.ljust(9)}{RESET}{YELLOW}? {e} (tidak ada di Download){RESET}")
    if "flash" in ui.mem:
        used, pct, mx = ui.mem["flash"]
        rows.append("")
        rows.append(f"{GRAY}{'Flash'.ljust(9)}{RESET}{mini_bar(pct)} {WHITE}{pct}%{RESET}")
        rows.append(f"{GRAY}{''.ljust(9)}{used:,} / {mx:,} byte{RESET}")
    if "ram" in ui.mem:
        used, pct, mx = ui.mem["ram"]
        rows.append(f"{GRAY}{'RAM'.ljust(9)}{RESET}{mini_bar(pct)} {WHITE}{pct}%{RESET}")
        rows.append(f"{GRAY}{''.ljust(9)}{used:,} / {mx:,} byte{RESET}")
    if ui.warn:
        rows.append("")
        rows.append(f"{YELLOW}▲ {ui.warn} warning (lihat {LOG_FILE}){RESET}")
    panel(GREEN, "✔ SEMUA PROSES BERHASIL — siap di-flash!", rows)
    notify(True, f"{os.path.basename(path)} ({ukuran:,.0f} KB)")


# ═══════════════════════════════════════════════════════════
# MAIN
# ═══════════════════════════════════════════════════════════
def main():
    global MODE
    MODE = choose_mode()
    banner()
    ui = UI()
    ui.start()
    tail = deque(maxlen=400)
    t0 = time.time()
    proc = None

    try:
        # 1. Validasi
        ui.set_step(0)
        time.sleep(0.3 if TTY else 0)
        if not os.path.exists(src_ino):
            ui.finish(False)
            ui.stop()
            show_failure(ui, [], f"'{INO_FILENAME}' tidak ada di folder Download HP")
            return 1

        # 2. Sinkronisasi
        ui.set_step(1)
        os.makedirs(SKETCH_DIR, exist_ok=True)
        os.makedirs(CACHE_DIR, exist_ok=True)
        clean_stale_sources()                   # buang .h/.cpp lama
        shutil.copy2(src_ino, dest_ino)
        # testing
        shutil.copy2(os.path.join(DOWNLOAD_DIR, "gif.zip"), os.path.join(SKETCH_DIR, "gif.zip"))
        if MODE == MODE_FULL:
            extras, missing = collect_extras()
            for rel in extras:
                dst = os.path.join(SKETCH_DIR, rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(os.path.join(DOWNLOAD_DIR, rel), dst)
            EXTRAS_COPIED.extend(extras)
            EXTRAS_MISSING.extend(missing)
            ui.log = f"sinkron {len(extras)} file tambahan"
        lama = find_merged_bin()
        if lama:
            os.remove(lama)                     # cegah salin hasil compile lama
        time.sleep(0.3 if TTY else 0)

        # 3. Compile
        ui.set_step(2)
        cmd = [
            "arduino-cli", "compile",
            "--fqbn", f"{FQBN}:PartitionScheme={PARTITION}",
            SKETCH_DIR,
            "--build-cache-path", CACHE_DIR,
            "--jobs", CPU_JOBS,
            "-v",
            "-e",
        ]
        logf = open(LOG_FILE, "w", encoding="utf-8")
        proc = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", bufsize=1)

        def pump():
            for raw in proc.stdout:
                logf.write(raw)
                line = raw.rstrip("\n")
                tail.append(line)
                ui.feed(line)

        reader = threading.Thread(target=pump, daemon=True)
        reader.start()
        proc.wait()
        reader.join()
        logf.close()

        if proc.returncode != 0:
            ui.finish(False)
            ui.stop()
            show_failure(ui, tail, f"arduino-cli keluar dengan kode {proc.returncode}")
            return proc.returncode

        # 4. Export
        ui.set_step(8)
        bin_path = find_merged_bin()
        if not bin_path:
            ui.finish(False)
            ui.stop()
            show_failure(ui, [], ".merged.bin tidak ditemukan di folder build")
            return 1
        dest_bin = os.path.join(DOWNLOAD_DIR, os.path.basename(bin_path))
        shutil.copy2(bin_path, dest_bin)
        time.sleep(0.2 if TTY else 0)

        ui.finish(True)
        ui.stop()
        show_success(ui, dest_bin, time.time() - t0)
        return 0

    except KeyboardInterrupt:
        if proc and proc.poll() is None:
            proc.terminate()
        ui.finish(False)
        ui.stop()
        panel(YELLOW, "■ Dibatalkan oleh pengguna", [])
        return 130
    finally:
        if TTY:
            sys.stdout.write("\033[?25h")
            sys.stdout.flush()


if __name__ == "__main__":
    sys.exit(main())
