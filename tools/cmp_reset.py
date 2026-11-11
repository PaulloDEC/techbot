#!/usr/bin/env python3
"""cmp_reset.py FRESH RESET N - compare a fresh headless --dump of a level with
the part of a --reset-test dump after its reset at frame N (frame numbers are
ignored; everything else in each record must match).  See BUILDING.md."""
import sys

REC = 4 + 4 + 0x10000 + 0x5A00 + 4 * 8000     # "FRM0", frame no, DS image, map, display page


def records(path):
    d = open(path, 'rb').read()
    return [d[i + 8:i + REC] for i in range(0, len(d) - REC + 1, REC)]


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    fresh, reset = records(sys.argv[1]), records(sys.argv[2])
    after = reset[int(sys.argv[3]):]
    n = min(len(fresh), len(after))
    bad = [i for i in range(n) if fresh[i] != after[i]]
    print(f"compared {n} frames after the reset: {len(bad)} differ" + (f" (first at {bad[0]})" if bad else ""))
    sys.exit(1 if bad or n == 0 else 0)


if __name__ == '__main__':
    main()
