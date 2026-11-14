#!/usr/bin/env bash
# make_release.sh - lay out a release package and zip it.
#
# usage: tools/make_release.sh <windows|linux> <version> <folder with the built programs> [out folder]
#
# The package unzips straight into the player's game folder (the one with
# DN1.EXE), so everything is at the top level of the archive:
#
#   techbot(.exe), shrapnel(.exe)   (+ SDL2.dll on Windows)
#   widescreen/                     the widescreen art (difference files)
#   PATCHES/                        where .PAT files go (with a note)
#   LEVELS/                         where .LEV / .EPI files go (with a note)
#
# Windows: techbot-<version>-windows-x64.zip, plus
#          techbot-<version>-windows-x64-symbols.zip (techbot.pdb, for
#          opening crash dumps; not for players).
# Linux:   techbot-<version>-linux-x64.tar.gz (a tarball keeps the
#          programs executable).
#
# Used by .github/workflows/release.yml; runs the same way by hand.
set -euo pipefail

platform=${1:?platform: windows or linux}
version=${2:?version, e.g. v1.0.0}
bin=${3:?folder with the built programs}
out=${4:-dist}

root=$(cd "$(dirname "$0")/.." && pwd)
name="techbot-${version}-${platform}-x64"
stage="$out/$name"

rm -rf "$stage"
mkdir -p "$stage"

case "$platform" in
    windows)
        for f in techbot.exe shrapnel.exe SDL2.dll; do
            cp "$bin/$f" "$stage/"
        done
        ;;
    linux)
        for f in techbot shrapnel; do
            cp "$bin/$f" "$stage/"
            strip "$stage/$f" 2>/dev/null || true
            chmod 755 "$stage/$f"
        done
        ;;
    *)
        echo "unknown platform: $platform" >&2; exit 1 ;;
esac

# the widescreen art: only the PNGs (the difference files)
mkdir -p "$stage/widescreen"
shopt -s nullglob nocaseglob
art=("$root"/widescreen/*.png)
shopt -u nocaseglob
if [ ${#art[@]} -eq 0 ]; then
    echo "error: no PNG files in $root/widescreen (the widescreen art)" >&2
    exit 1
else
    cp "${art[@]}" "$stage/widescreen/"
fi

# the PATCHES and LEVELS folders, each with its note
cp -R "$root/release/PATCHES" "$root/release/LEVELS" "$stage/"

# archive: the contents of the stage folder at the top level
mkdir -p "$out"
outabs=$(cd "$out" && pwd)
case "$platform" in
    windows)
        rm -f "$outabs/$name.zip"
        if command -v 7z >/dev/null; then
            (cd "$stage" && 7z a -tzip -mx=9 "$outabs/$name.zip" . >/dev/null)
        else
            (cd "$stage" && zip -qr -9 "$outabs/$name.zip" .)
        fi
        if [ -f "$bin/techbot.pdb" ]; then
            rm -f "$outabs/$name-symbols.zip"
            if command -v 7z >/dev/null; then
                (cd "$bin" && 7z a -tzip "$outabs/$name-symbols.zip" techbot.pdb >/dev/null)
            else
                (cd "$bin" && zip -q "$outabs/$name-symbols.zip" techbot.pdb)
            fi
        fi
        ;;
    linux)
        tar -C "$stage" -czf "$outabs/$name.tar.gz" .
        ;;
esac

echo "packaged $name:"
(cd "$stage" && find . -type f | sort | sed 's|^\./|  |' | awk 'NR<=12{print} END{if(NR>12) print "  ... (" NR " files)"}')
