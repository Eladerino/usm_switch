#!/usr/bin/env python3
"""
Ultimate Spider-Man (Nintendo Switch) - Standalone NSP Builder
Packages the compiled NSO executable, packaging assets, and user's PC game files
into an installable standalone NSP for Nintendo Switch using hacbrewpack.
"""

import argparse
import os
import shutil
import subprocess
import sys

TITLE_ID = "01c483a5b2b53000"
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PACKAGING_DIR = os.path.join(REPO_ROOT, "packaging", "nsp")

def find_hacbrewpack(custom_path=None):
    if custom_path and os.path.isfile(custom_path):
        return custom_path
    
    which_path = shutil.which("hacbrewpack") or shutil.which("hacbrewpack.exe")
    if which_path:
        return which_path

    # Check common local directories
    candidates = [
        os.path.join(REPO_ROOT, "tools", "hacbrewpack.exe"),
        os.path.join(REPO_ROOT, "tools", "hacbrewpack"),
        os.path.join(REPO_ROOT, "hacbrewpack.exe"),
        r"D:\ROMS\Carafe\hacbrewpack.exe",
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    return None

def main():
    parser = argparse.ArgumentParser(description="Build Standalone NSP for Ultimate Spider-Man (Switch)")
    parser.add_argument("--game-dir", required=True, help="Path to PC Ultimate Spider-Man folder (containing Data/ and USM.exe)")
    parser.add_argument("--keys", required=True, help="Path to Nintendo Switch prod.keys file")
    parser.add_argument("--hacbrewpack", help="Path to hacbrewpack executable")
    parser.add_argument("--out-dir", default=os.path.join(REPO_ROOT, "dist"), help="Output directory for the NSP")
    parser.add_argument("--titleid", default=TITLE_ID, help=f"Title ID (default: {TITLE_ID})")
    args = parser.parse_args()

    game_dir = os.path.abspath(args.game_dir)
    keys_file = os.path.abspath(args.keys)
    out_dir = os.path.abspath(args.out_dir)

    print("=== Ultimate Spider-Man NSP Builder ===")
    print(f"Game Directory : {game_dir}")
    print(f"Keys File      : {keys_file}")
    print(f"Output Dir     : {out_dir}")

    # Validate game directory
    if not os.path.isdir(game_dir):
        print(f"ERROR: Game directory does not exist: {game_dir}", file=sys.stderr)
        sys.exit(1)

    usm_exe = os.path.join(game_dir, "USM.exe")
    if not os.path.isfile(usm_exe):
        # Check case-insensitive
        found = False
        for f in os.listdir(game_dir):
            if f.lower() == "usm.exe":
                usm_exe = os.path.join(game_dir, f)
                found = True
                break
        if not found:
            print("ERROR: USM.exe not found in game directory!", file=sys.stderr)
            sys.exit(1)

    data_dir = os.path.join(game_dir, "Data")
    if not os.path.isdir(data_dir):
        for d in os.listdir(game_dir):
            if d.lower() == "data":
                data_dir = os.path.join(game_dir, d)
                break
        if not os.path.isdir(data_dir):
            print("ERROR: Data/ directory not found in game directory!", file=sys.stderr)
            sys.exit(1)

    if not os.path.isfile(keys_file):
        print(f"ERROR: Keys file not found: {keys_file}", file=sys.stderr)
        sys.exit(1)

    hacbrewpack = find_hacbrewpack(args.hacbrewpack)
    if not hacbrewpack:
        print("ERROR: hacbrewpack executable not found! Specify with --hacbrewpack <path>", file=sys.stderr)
        sys.exit(1)

    # Locate compiled NSO binary ('main')
    main_nso = os.path.join(REPO_ROOT, "build-switch", "main")
    if not os.path.isfile(main_nso):
        main_nso = os.path.join(REPO_ROOT, "build_standalone_nsp", "exefs", "main")
    if not os.path.isfile(main_nso):
        print("ERROR: Compiled NSO binary ('main') not found! Please run ./build_switch.sh first.", file=sys.stderr)
        sys.exit(1)

    # Prepare staging directories
    stage_dir = os.path.join(REPO_ROOT, "build_nsp_stage")
    if os.path.exists(stage_dir):
        shutil.rmtree(stage_dir)
    os.makedirs(stage_dir, exist_ok=True)

    stage_exefs = os.path.join(stage_dir, "exefs")
    stage_control = os.path.join(stage_dir, "control")
    stage_logo = os.path.join(stage_dir, "logo")
    stage_romfs = os.path.join(stage_dir, "romfs")

    os.makedirs(stage_exefs, exist_ok=True)
    os.makedirs(stage_control, exist_ok=True)
    os.makedirs(stage_logo, exist_ok=True)
    os.makedirs(stage_romfs, exist_ok=True)

    print("\n[1/4] Staging ExeFS, Control, and Logo...")
    shutil.copy2(main_nso, os.path.join(stage_exefs, "main"))
    shutil.copy2(os.path.join(PACKAGING_DIR, "exefs", "main.npdm"), os.path.join(stage_exefs, "main.npdm"))
    shutil.copy2(os.path.join(PACKAGING_DIR, "control", "control.nacp"), os.path.join(stage_control, "control.nacp"))
    shutil.copy2(os.path.join(PACKAGING_DIR, "control", "icon_AmericanEnglish.dat"), os.path.join(stage_control, "icon_AmericanEnglish.dat"))
    shutil.copy2(os.path.join(PACKAGING_DIR, "logo", "NintendoLogo.png"), os.path.join(stage_logo, "NintendoLogo.png"))
    shutil.copy2(os.path.join(PACKAGING_DIR, "logo", "StartupMovie.gif"), os.path.join(stage_logo, "StartupMovie.gif"))

    print("[2/4] Staging RomFS assets...")
    shutil.copy2(usm_exe, os.path.join(stage_romfs, "USM.exe"))
    
    # Copy or symlink defaults
    defaults_dir = os.path.join(PACKAGING_DIR, "defaults")
    for f in ["settings.ini", "resolution.txt", "registry.txt"]:
        src_default = os.path.join(defaults_dir, f)
        if os.path.isfile(src_default):
            shutil.copy2(src_default, os.path.join(stage_romfs, f))

    # Link/Copy Data directory into RomFS
    dest_data = os.path.join(stage_romfs, "Data")
    try:
        if sys.platform == "win32":
            # Use directory junction on Windows to avoid copying 1.5 GB
            subprocess.check_call(f'cmd /c mklink /J "{dest_data}" "{data_dir}"', shell=True)
        else:
            os.symlink(data_dir, dest_data, target_is_directory=True)
        print("  Created fast link to Data/ folder.")
    except Exception as e:
        print(f"  Note: Linking failed ({e}), falling back to copying Data/...")
        shutil.copytree(data_dir, dest_data)

    print("\n[3/4] Packaging Standalone NSP with hacbrewpack...")
    os.makedirs(out_dir, exist_ok=True)
    cmd = [
        hacbrewpack,
        "--titleid", args.titleid,
        "-k", keys_file,
        "--nspdir", out_dir,
        "--exefsdir", stage_exefs,
        "--controldir", stage_control,
        "--logodir", stage_logo,
        "--romfsdir", stage_romfs,
    ]
    res = subprocess.run(cmd, cwd=stage_dir)
    if res.returncode != 0:
        print("ERROR: hacbrewpack packaging failed!", file=sys.stderr)
        sys.exit(res.returncode)

    # Rename output NSP to friendly title
    raw_nsp = os.path.join(out_dir, f"{args.titleid}.nsp")
    pretty_nsp = os.path.join(out_dir, f"Ultimate Spider-Man [{args.titleid}][v0].nsp")
    if os.path.isfile(raw_nsp):
        shutil.copy2(raw_nsp, pretty_nsp)

    print("\n[4/4] Cleaning up staging files...")
    if sys.platform == "win32" and os.path.exists(dest_data):
        # Remove junction safely without deleting source Data contents
        subprocess.call(f'cmd /c rmdir "{dest_data}"', shell=True)
    shutil.rmtree(stage_dir, ignore_errors=True)

    print("\n========================================================")
    print("SUCCESS: Standalone NSP created successfully!")
    print(f"File: {pretty_nsp}")
    print("========================================================")

if __name__ == "__main__":
    main()
