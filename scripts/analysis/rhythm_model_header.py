# -*- coding: utf-8 -*-
"""The shape the rhythm classifier takes on disk, kept apart from fitting it.

train_rhythm_model.py owns the fit; this owns the layout, because the layout is
the half that has to agree with the walk in bpmcore/rhythm.cpp, and it has to be
generatable and checkable without scikit-learn present. The fitted model is
expensive to reproduce and does not come back identical - a refit that changes
nothing meaningful still moves 287 of 12,160 tracks, see docs/tango-analysis.md
- so the layout must be changeable without touching the model.

The form both halves speak is sklearn's own: `flat`, one
(feature, left, right, threshold, value) tuple per node, with feature < 0
marking a leaf and child indices relative to the tree's own slice; and
`offsets`, where each tree starts in it.
"""
import struct

# Every index the header stores is a short. 750 trees of 15 leaves is nowhere
# near the limit, but a refit with a much larger max_iter would be, and a
# truncated child index is the kind of bug that reads as a slightly wrong
# answer rather than as a crash.
SHORT_MAX = 32767


def _as_float32(v):
    """`v` rounded to the nearest float, still as a Python float."""
    return struct.unpack('f', struct.pack('f', v))[0]


def _narrow(values, what):
    """`values` as float32, refusing to round any of them.

    Every threshold sklearn has produced here is already a float32 value
    widened to a double, because the features it bins are float32, so the
    narrowing is a change of storage and not of the model: rhythm.cpp compares
    a double against it, which widens it straight back to the number it came
    from, and the walk branches where it always branched.

    A refit that broke that would move a split by half an ulp, which reads as a
    slightly different answer on a handful of tracks rather than as a failure -
    the same kind of quiet wrongness SHORT_MAX is here to stop. Whoever refits
    can decide the half-ulp does not matter and say so here; it is not a
    decision to make silently on their behalf.
    """
    out = []
    for i, v in enumerate(values):
        f = _as_float32(v)
        if f != v:
            raise ValueError(
                '%s[%d] = %r is not a float; storing it as one would move the '
                'model. See _narrow() in %s.' % (what, i, v, __file__))
        out.append(f)
    return out


def split_layout(flat, offsets):
    """Pull `flat` apart into splits and leaves.

    A split reads a feature, a threshold and two children; a leaf reads only a
    value. Holding both in one struct left one of the two doubles unread in
    every single node - and two bytes of padding in each besides, three shorts
    before a double rounding up to 24. Apart, and with the thresholds stored at
    the width sklearn actually chose them at, the same trees take 198,000 bytes
    rather than 522,000, and nothing about them changes.

    A child reference is a split index when it is >= 0, and the leaf -1 - c when
    it is negative. tree_root carries the same encoding, so a tree that is a
    bare leaf is its own root with no special case anywhere.
    """
    bounds = list(offsets) + [len(flat)]
    tree_root = []
    feature, threshold, left, right, value = [], [], [], [], []

    for t in range(len(offsets)):
        lo, hi = bounds[t], bounds[t + 1]
        if hi <= lo:
            raise ValueError('tree %d is empty' % t)

        # Where each node of this tree will land, worked out before anything is
        # appended, so that a child can be encoded while its own row is written.
        ref = [0] * (hi - lo)
        next_split, next_leaf = len(feature), len(value)
        for i in range(hi - lo):
            if flat[lo + i][0] < 0:
                ref[i] = -1 - next_leaf
                next_leaf += 1
            else:
                ref[i] = next_split
                next_split += 1

        for i in range(hi - lo):
            feat, l, r, thr, val = flat[lo + i]
            if feat < 0:
                value.append(float(val))
            else:
                feature.append(int(feat))
                threshold.append(float(thr))
                left.append(ref[int(l)])
                right.append(ref[int(r)])

        tree_root.append(ref[0])

    if len(feature) > SHORT_MAX or len(value) > SHORT_MAX:
        raise ValueError('%d splits and %d leaves will not fit the short indices '
                         'the header stores' % (len(feature), len(value)))

    return {'tree_root': tree_root, 'split_feature': feature,
            'split_threshold': _narrow(threshold, 'split_threshold'),
            'split_left': left,
            'split_right': right, 'leaf_value': value}


def walk(layout, targets, baseline, x):
    """Score one feature vector, exactly as classify() in rhythm.cpp does.

    Kept here rather than in the trainer so that both the fit and any later
    change to the layout are checked against the same reading of it.
    """
    root = layout['tree_root']
    feat, thr = layout['split_feature'], layout['split_threshold']
    left, right = layout['split_left'], layout['split_right']
    value = layout['leaf_value']

    s = list(baseline)
    for t in range(len(root)):
        n = root[t]
        while n >= 0:
            n = left[n] if x[feat[n]] <= thr[n] else right[n]
        s[targets[t]] += value[-1 - n]
    return s


def _float32_literal(v):
    """The shortest literal that reads back as this float, with the f suffix.

    The suffix is not decoration. Without it the literal is a double, and a
    double that is not exactly a float narrows in a braced initialiser, which
    C++ makes ill-formed - the shortest spelling of a float rarely is one
    exactly, 0.1f being the standard example.
    """
    for digits in range(1, 10):   # 9 always round-trips a float32
        s = '%.*g' % (digits, v)
        if _as_float32(float(s)) == v:
            break
    # '1f' is not a literal; '1.f' is. An exponent already makes it a real.
    if '.' not in s and 'e' not in s and 'E' not in s:
        s += '.'
    return s + 'f'


def _rows(values, per, render):
    for i in range(0, len(values), per):
        yield '\t\t' + ', '.join(render(v) for v in values[i:i + per])


def _array(lines, ctype, name, values, per, render):
    lines.append('\tconst %s %s[%d] = {' % (ctype, name, len(values)))
    for row in _rows(values, per, render):
        lines.append(row + ',')
    lines[-1] = lines[-1].rstrip(',')
    lines.append('\t};')
    lines.append('')


def emit(layout, targets, baseline, class_names, track_count, feature_count):
    """Render the C++ header. Returns its text."""
    n_split = len(layout['split_feature'])
    n_leaf = len(layout['leaf_value'])
    n_tree = len(layout['tree_root'])

    # Both renderers round-trip through the shortest string that reads back as
    # the same bits, which is what keeps a regenerated header identical to the
    # one it replaced. Either would render an infinity as 'inf', which C++ will
    # not parse; sklearn does not produce one here, and if that ever changes
    # this should stop rather than emit a file that does not compile.
    for v in layout['split_threshold'] + layout['leaf_value'] + list(baseline):
        if v != v or v in (float('inf'), float('-inf')):
            raise ValueError('cannot render %r as a C++ literal' % v)

    b = []
    b.append('#ifndef BPMCORE_RHYTHM_MODEL_H')
    b.append('#define BPMCORE_RHYTHM_MODEL_H')
    b.append('')
    b.append('// GENERATED FILE - do not edit by hand.')
    b.append('// Produced by scripts/train_rhythm_model.py; see docs/tango-analysis.md.')
    b.append('//')
    b.append('// Gradient boosted decision trees over the features built by')
    b.append('// bpmcore::build_features. Class order is '
             + ', '.join(c.capitalize() for c in class_names) + ',')
    b.append('// matching bpmcore::rhythm_class.')
    b.append('//')
    b.append('// Fitted on %d hand-labelled tracks, %d features,' % (track_count, feature_count))
    b.append('// %d trees, %d splits and %d leaves.' % (n_tree, n_split, n_leaf))
    b.append('//')
    b.append('// Splits and leaves are held apart. A split reads a feature, a threshold and')
    b.append('// two children; a leaf reads only a value. One struct carrying both left one')
    b.append('// of the two doubles unread in every node, and two bytes of padding besides,')
    b.append('// which cost 522,000 bytes for what fits in 198,000.')
    b.append('//')
    b.append('// A child reference is a split index when it is >= 0, and the leaf -1 - c when')
    b.append('// it is negative. tree_root uses the same encoding, so a tree that is a bare')
    b.append('// leaf needs no special case.')
    b.append('')
    b.append('namespace bpmcore')
    b.append('{')
    b.append('namespace rhythm_model')
    b.append('{')
    b.append('\tconst int feature_count = %d;' % feature_count)
    b.append('\tconst int class_count = %d;' % len(class_names))
    b.append('\tconst int tree_count = %d;' % n_tree)
    b.append('\tconst int split_count = %d;' % n_split)
    b.append('\tconst int leaf_count = %d;' % n_leaf)
    b.append('')
    b.append('\tconst double baseline[%d] = { ' % len(baseline)
             + ', '.join('%.10e' % v for v in baseline) + ' };')
    b.append('')

    short = lambda v: '%d' % v
    # See the note above on repr; '%r' is the same thing for a float.
    real = lambda v: '%r' % v
    real32 = _float32_literal

    b.append('\t//! Where each tree starts, encoded as a child reference.')
    _array(b, 'short', 'tree_root', layout['tree_root'], 16, short)
    b.append('\t//! Which class each tree adds its leaf value to.')
    _array(b, 'short', 'tree_target', list(targets), 32, short)
    b.append('\t//! The feature a split compares, and what it compares it against.')
    _array(b, 'short', 'split_feature', layout['split_feature'], 16, short)
    b.append('\t//! float because that is the width scikit-learn chose these at - it bins')
    b.append('\t//! float32 features - so every one of them widens back to the number the')
    b.append('\t//! fit produced, and the walk below branches where the fit branched. The')
    b.append('\t//! generator refuses to narrow a threshold that would not survive it.')
    _array(b, 'float', 'split_threshold', layout['split_threshold'], 6, real32)
    b.append('\t//! Where a split goes, by the encoding above.')
    _array(b, 'short', 'split_left', layout['split_left'], 16, short)
    _array(b, 'short', 'split_right', layout['split_right'], 16, short)
    b.append('\t//! What a leaf adds to its tree\'s class.')
    _array(b, 'double', 'leaf_value', layout['leaf_value'], 4, real)

    b[-1] = '}   // namespace rhythm_model'
    b.append('}   // namespace bpmcore')
    b.append('')
    b.append('#endif // BPMCORE_RHYTHM_MODEL_H')
    return '\n'.join(b) + '\n'
