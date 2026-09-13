# -*- coding: utf-8 -*-
"""Diff two `bpmcore_test batch` runs.

Built for one question: does narrowing the spectral stage to float cost
anything a listener would notice? Two builds, the same list of tracks, and
this reports what moved.

    cmake -S . -B build/prec-double -A x64 -DBPMCORE_FFT_SCALAR=double
    cmake -S . -B build/prec-float  -A x64 -DBPMCORE_FFT_SCALAR=float
    cmake --build build/prec-double --config Release --target bpmcore_test
    cmake --build build/prec-float  --config Release --target bpmcore_test

    build/prec-double/.../bpmcore_test batch tracks.txt > double.tsv
    build/prec-float/.../bpmcore_test  batch tracks.txt > float.tsv
    python compare_batch.py double.tsv float.tsv

Accuracy against the ground truth is not the measure here, and reporting it
alone would hide the thing worth knowing: a change can leave accuracy exactly
where it was while moving individual tracks underneath it. So this counts the
moves. Of them the metrical level is the one that matters most - a flip there
is the difference between 60 and 120 BPM, not a rounding.

The yardstick is in docs/tango-analysis.md: refitting the model under a
different fold split, a nuisance change that cannot carry meaning, moves 287 of
12,160 tracks. A difference smaller than that is below the noise the model
already has.
"""
import sys
from collections import Counter

REFIT_NOISE = 287          # tracks moved by a meaningless refit
REFIT_TOTAL = 12160


def read(path):
    rows = {}
    # utf-8-sig rather than utf-8: PowerShell's Out-File writes a BOM, which
    # would otherwise hide the '#' that marks the header line.
    with open(path, encoding='utf-8-sig') as f:
        for line in f:
            line = line.rstrip('\n').rstrip('\r')
            if not line or line.startswith('#'):
                continue
            c = line.split('\t')
            if len(c) < 11:
                continue
            rows[c[0]] = {
                'ok': c[1] == '1',
                'bpm': float(c[2]),
                'rhythm': c[3],
                'confidence': float(c[4]),
                'beat_bpm': float(c[5]),
                'meter': int(c[6]),
                'initial_bpm': float(c[7]),
            }
    return rows


def level_flip(a, b):
    """True when the two BPMs are the same tempo read at different levels.

    A ratio near 2, 3, 1/2 or 1/3 is the grid choosing a different multiple,
    which is a different answer rather than a slightly different one. The 3%
    window is wide enough to cover the interpolation moving a little as well.
    """
    if a <= 0 or b <= 0:
        return False
    r = b / a
    return any(abs(r - t) / t < 0.03 for t in (2.0, 0.5, 3.0, 1.0 / 3.0))


def quantiles(xs, ps):
    if not xs:
        return [0.0] * len(ps)
    s = sorted(xs)
    out = []
    for p in ps:
        i = min(len(s) - 1, max(0, int(round(p * (len(s) - 1)))))
        out.append(s[i])
    return out


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    a_name, b_name = argv[1], argv[2]
    A, B = read(a_name), read(b_name)

    shared = [p for p in A if p in B]
    if not shared:
        sys.stderr.write('the two runs share no tracks\n')
        return 2

    only_a = [p for p in shared if A[p]['ok'] and not B[p]['ok']]
    only_b = [p for p in shared if B[p]['ok'] and not A[p]['ok']]
    both = [p for p in shared if A[p]['ok'] and B[p]['ok']]

    bpm_delta, conf_delta = [], []
    flips, classes, meters = [], [], []
    class_moves = Counter()
    for p in both:
        a, b = A[p], B[p]
        bpm_delta.append(b['bpm'] - a['bpm'])
        conf_delta.append(b['confidence'] - a['confidence'])
        if a['rhythm'] != b['rhythm']:
            classes.append(p)
            class_moves[a['rhythm'] + ' -> ' + b['rhythm']] += 1
        if a['meter'] != b['meter']:
            meters.append(p)
        if level_flip(a['bpm'], b['bpm']):
            flips.append(p)

    moved = set(classes) | set(flips) | set(meters)

    print('%s  ->  %s' % (a_name, b_name))
    print('%d tracks in both runs, %d analysed by both' % (len(shared), len(both)))
    if only_a or only_b:
        print('%d analysed only by the first, %d only by the second'
              % (len(only_a), len(only_b)))
    print()

    abs_bpm = [abs(d) for d in bpm_delta]
    q = quantiles(abs_bpm, [0.5, 0.9, 0.99, 1.0])
    print('BPM   |delta|   median %.6f   p90 %.6f   p99 %.6f   max %.6f'
          % (q[0], q[1], q[2], q[3]))
    print('      over 0.05 BPM: %d     over 0.5 BPM: %d'
          % (sum(1 for d in abs_bpm if d > 0.05), sum(1 for d in abs_bpm if d > 0.5)))

    abs_conf = [abs(d) for d in conf_delta]
    q = quantiles(abs_conf, [0.5, 0.9, 0.99, 1.0])
    print('conf  |delta|   median %.6f   p90 %.6f   p99 %.6f   max %.6f'
          % (q[0], q[1], q[2], q[3]))
    print()

    print('metrical level flipped : %d' % len(flips))
    print('meter changed          : %d' % len(meters))
    print('rhythm class changed   : %d' % len(classes))
    for move, n in class_moves.most_common():
        print('    %-24s %d' % (move, n))
    print()

    # The whole point of the exercise, in one line.
    scaled = REFIT_NOISE * len(both) / REFIT_TOTAL if both else 0.0
    print('%d of %d tracks moved at all (%.2f%%)'
          % (len(moved), len(both), 100.0 * len(moved) / max(1, len(both))))
    print('a meaningless refit moves %d of %d (%.2f%%), so on this many tracks: %.0f'
          % (REFIT_NOISE, REFIT_TOTAL, 100.0 * REFIT_NOISE / REFIT_TOTAL, scaled))
    print('verdict: %s the noise the model already carries'
          % ('BELOW' if len(moved) <= scaled else 'ABOVE'))

    if flips or classes:
        print()
        print('tracks that moved:')
        for p in sorted(moved):
            a, b = A[p], B[p]
            note = []
            if p in flips:
                note.append('LEVEL')
            if p in classes:
                note.append('CLASS')
            if p in meters:
                note.append('METER')
            print('  [%s] %s' % (','.join(note), p))
            print('        %.4f %s (%.4f)  ->  %.4f %s (%.4f)'
                  % (a['bpm'], a['rhythm'], a['confidence'],
                     b['bpm'], b['rhythm'], b['confidence']))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
