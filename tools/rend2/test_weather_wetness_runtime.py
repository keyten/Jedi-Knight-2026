"""Real-map validation and GPU profiling for persistent world wetness.

The installed game and PK3s are read-only.  Each run uses an isolated home
under build/weather-wetness-runtime and the freshly built renderer DLL.
"""
import argparse
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import time


VIEW_COURTYARD = "setviewpos -80 -4240 620 205"
VIEW_RETURN = "setviewpos -1050 2650 850 108"


def write_cfg(base: Path, visual):
    commands = [
        "wait 120", "cam_disable", "wait 10", "noclip", "god",
        "set cg_draw2D 0", "set cg_drawGun 0", "set r_drawentities 0",
        "set con_notifytime 0", "r_we heavyrain", VIEW_COURTYARD,
        "centerview", "wait 180",
    ]
    if visual == "reset":
        commands += [
            "wait 120", "devmap yavin1b", "wait 120", "cam_disable", "noclip", "god",
            "set cg_draw2D 0", "set cg_drawGun 0", "set r_drawentities 0",
            VIEW_COURTYARD, "centerview", "r_weatherWetnessPersistenceDebug 1", "wait 12",
            "screenshot_tga wetness-after-map-reset-debug",
        ]
    elif visual == "puddle_debug":
        commands += [
            "setviewpos -80 -4240 900 205", "centerview", "+lookdown", "wait 12", "-lookdown",
            "wait 120", "r_weatherSurfaceDebug 6", "wait 12",
            "screenshot_tga wetness-puddle-mask-after-rain",
        ]
    elif visual == "focus":
        commands += [
            "setviewpos -80 -4240 900 205", "centerview", "+lookdown", "wait 12", "-lookdown",
            "wait 120", "screenshot_tga wetness-puddles-focus",
            "r_weatherWetnessPersistenceDebug 1", "wait 8",
            "screenshot_tga wetness-before-map-reset-debug",
            "devmap yavin1b", "wait 120", "cam_disable", "noclip", "god",
            "set cg_draw2D 0", "set cg_drawGun 0", "set r_drawentities 0",
            "setviewpos -80 -4240 900 205", "centerview", "+lookdown", "wait 12", "-lookdown",
            "r_weatherWetnessPersistenceDebug 1", "wait 8",
            "screenshot_tga wetness-after-map-reset-debug",
        ]
    elif visual:
        commands += [
            "r_weatherWetnessPersistenceDebug 1", "wait 8",
            "screenshot_tga wetness-raining-debug",
            "r_weatherWetnessPersistenceDebug 0", "wait 8",
            "screenshot_tga wetness-raining-shaded",
            "setviewpos -80 -4240 900 205", "centerview", "+lookdown", "wait 12", "-lookdown",
            "r_weatherPuddleRippleDebug 1", "wait 8",
            "screenshot_tga ripple-impact-impulses",
            "r_weatherPuddleRippleDebug 2", "wait 8",
            "screenshot_tga ripple-height",
            "r_weatherPuddleRippleDebug 3", "wait 8",
            "screenshot_tga ripple-velocity",
            "r_weatherPuddleRippleDebug 4", "wait 8",
            "screenshot_tga ripple-final-normal",
            "r_weatherPuddleRippleDebug 0", "wait 8",
            "r_we clear", "wait 30",
            "screenshot_tga wetness-rain-stopped",
            "r_weatherWetnessPersistenceDebug 1", "wait 8",
            "screenshot_tga wetness-drying-debug",
            VIEW_RETURN, "centerview", "wait 20",
            "screenshot_tga wetness-return-courtyard-debug",
            "r_weatherWetnessPersistenceDebug 0", "r_weatherSurfaceDebug 22", "wait 8",
            "screenshot_tga wetness-steep-surface-classes",
            "r_weatherSurfaceDebug 0", "wait 8",
            "screenshot_tga wetness-puddles-after-rain",
            "devmap yavin1b", "wait 120", "cam_disable", "noclip", "god",
            "set cg_draw2D 0", "set cg_drawGun 0", "set r_drawentities 0",
            VIEW_COURTYARD, "centerview", "wait 30",
            "r_weatherWetnessPersistenceDebug 1", "wait 8",
            "screenshot_tga wetness-map-reset-debug",
        ]
    else:
        commands += ["r_speeds 100", "wait 360", "r_speeds 0", "wait 8"]
    # Screenshot writes are queued on the render thread; allow them to finish
    # before shutdown (otherwise the final capture can be announced but absent).
    commands += ["wait 30", "echo WEATHER_WETNESS_RUNTIME_DONE", "quit"]
    (base / "wetness.cfg").write_text("\n".join(commands) + "\n", encoding="utf-8")
    (base / "autoexec_sp.cfg").write_text(
        "wait 300\ndevmap yavin1b\nwait 90\nexec wetness.cfg\n", encoding="utf-8")


def run(repo: Path, installation: Path, label: str, resolution: int, hz: int,
        visual: bool, timeout: int):
    root = repo / "build" / "weather-wetness-runtime" / label
    home = root / "home"
    base = home / "OpenJK"
    shots = base / "screenshots"
    shots.mkdir(parents=True, exist_ok=True)
    for old in shots.glob("wetness-*.tga"):
        old.unlink()
    binaries = repo / "build/msvc-all/RelWithDebInfo"
    shutil.copy2(binaries / "openjk_sp.x86_64.exe", root / "openjk_sp.x86_64.exe")
    shutil.copy2(repo / "lib/SDL2/bin/x64/SDL2.dll", root / "SDL2.dll")
    shutil.copy2(repo / "build/msvc/Release/rdsp-rend2_x86_64.dll",
                 root / "rdsp-rend2_x86_64.dll")
    shutil.copy2(binaries / "jagamex86_64.dll", base / "jagamex86_64.dll")
    write_cfg(base, visual)
    log_path = base / "qconsole.log"
    if log_path.exists():
        log_path.unlink()

    settings = dict(
        fs_basepath=str(installation), fs_homepath=str(home), fs_game="OpenJK",
        cl_renderer="rdsp-rend2", r_fullscreen="0", r_mode="6", s_initsound="0",
        r_glslCache="1", r_cubeMapping="0", r_diffuseIBL="0", r_ssr="0",
        developer="1", logfile="2", com_maxfps="60",
        r_weatherWetness="1", r_weatherWetnessPersistence="1",
        r_weatherWetnessResolution=str(resolution), r_weatherWetnessUpdateHz=str(hz),
        r_weatherWetnessAccumulationRate="0.20", r_weatherWetnessDryRate="0.02",
        r_weatherWetnessSteepDryScale="2", r_weatherPuddles="1",
        r_weatherRunoff="1", r_weatherPuddleRipples="2",
        r_weatherPuddleRippleResolution=str(resolution), r_rainSplashes="0",
    )
    command = [str(root / "openjk_sp.x86_64.exe")]
    for key, value in settings.items():
        command += ["+set", key, value]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    with (root / "launch.log").open("w") as stream:
        process = subprocess.Popen(command, cwd=root, startupinfo=startup,
                                   stdout=stream, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + timeout
        while process.poll() is None and time.monotonic() < deadline:
            if log_path.exists() and "WEATHER_WETNESS_RUNTIME_DONE" in log_path.read_text(errors="replace"):
                break
            time.sleep(0.5)
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    log = log_path.read_text(errors="replace") if log_path.exists() else ""
    assert "WEATHER_WETNESS_RUNTIME_DONE" in log, log[-6000:]
    assert not re.search(r"GL_INVALID|GLSL shader compile error|GLSL shader link error", log), log[-6000:]
    return root, log


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installation", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--visual-only", action="store_true",
                        help="run the screenshot/GL-error pass without the performance matrix")
    args = parser.parse_args()
    installation = args.installation.resolve()

    visual_root, _ = run(repo, installation, "visual-256-10", 256, 10, True, args.timeout)
    if args.visual_only:
        print("PASS: yavin1b ripple debug sequence completed")
        print("Screenshots:", visual_root / "home/OpenJK/screenshots")
        print("Visual log:", visual_root / "home/OpenJK/qconsole.log")
        return
    results = []
    for resolution, hz in ((128, 5), (256, 10), (512, 15)):
        root, log = run(repo, installation, f"perf-{resolution}-{hz}", resolution, hz,
                        False, args.timeout)
        samples = [float(v) for v in re.findall(
            r"Weather wetness persistence: ([0-9.]+)ms", log)]
        assert samples, f"missing wetness GPU timer samples in {root}"
        results.append((resolution, hz, len(samples), statistics.median(samples),
                        statistics.quantiles(samples, n=20)[18] if len(samples) >= 20 else max(samples)))

    print("PASS: yavin1b visual sequence, rain stop, return, slope, puddles and map reset completed")
    print("Per-update GPU timestamps:")
    for resolution, hz, count, median, p95 in results:
        print(f"  {resolution} @ {hz} Hz: median {median:.4f} ms, p95 {p95:.4f} ms, n={count}")
    print("Screenshots:", visual_root / "home/OpenJK/screenshots")
    print("Visual log:", visual_root / "home/OpenJK/qconsole.log")


if __name__ == "__main__":
    main()
