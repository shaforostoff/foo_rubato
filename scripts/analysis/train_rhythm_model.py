# -*- coding: utf-8 -*-
"""Fit the shipping rhythm classifier and emit it as a C++ header.

The tree walk is re-implemented here in plain Python first and checked against
sklearn's own decision_function, so that what the header encodes is known to be
the same model before any of it reaches C++.

The header's layout lives in rhythm_model_header.py rather than here, so that
it can be regenerated and checked without refitting anything.
"""
import os, sys, collections
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import config
import tango_labels as T, features as F
import rhythm_model_header as H
from sklearn.ensemble import HistGradientBoostingClassifier

CLS = T.CLASSES
OUT = config.MODEL_HEADER


def walk(trees_flat, offsets, targets, baseline, x, n_cls):
    s = list(baseline)
    for t in range(len(offsets)):
        n = offsets[t]
        i = 0
        while trees_flat[n + i][0] >= 0:
            feat, left, right, thr, val = trees_flat[n + i]
            i = left if x[feat] <= thr else right
        s[targets[t]] += trees_flat[n + i][4]
    return s


def main():
    d = np.load(config.FEATURES, allow_pickle=True)
    X = np.nan_to_num(d['X'].astype(np.float64), nan=0.0, posinf=0.0, neginf=0.0)
    paths = list(d['paths'])
    recs = {r['path']: r for r in T.load()}
    y = np.array([CLS.index(T.label(recs[p])[0]) for p in paths])
    print('fitting on', X.shape, dict(collections.Counter(CLS[i] for i in y)))
    assert X.shape[1] == len(F.FEATURE_NAMES)

    clf = HistGradientBoostingClassifier(max_iter=150, learning_rate=0.12, max_leaf_nodes=15,
                                         l2_regularization=1.0, class_weight='balanced',
                                         early_stopping=False, random_state=0)
    clf.fit(X, y)
    print('train accuracy', (clf.predict(X) == y).mean())

    baseline = np.ravel(np.asarray(clf._baseline_prediction, dtype=np.float64))
    n_cls = len(CLS)
    if baseline.size == 1:
        baseline = np.repeat(baseline, n_cls)
    assert baseline.size == n_cls, baseline.shape

    flat, offsets, targets = [], [], []
    for it in clf._predictors:
        for k, pred in enumerate(it):
            nodes = pred.nodes
            offsets.append(len(flat))
            targets.append(k)
            for nd in nodes:
                if nd['is_leaf']:
                    flat.append((-1, 0, 0, 0.0, float(nd['value'])))
                else:
                    flat.append((int(nd['feature_idx']), int(nd['left']), int(nd['right']),
                                 float(nd['num_threshold']), 0.0))
    print(f'trees={len(offsets)} nodes={len(flat)}')

    layout = H.split_layout(flat, offsets)
    print(f"splits={len(layout['split_feature'])} leaves={len(layout['leaf_value'])}")

    # Parity check against sklearn itself, through both readings of the trees.
    # The flat one comes straight out of the fit; the split layout is what the
    # header stores and what rhythm.cpp walks, so checking only the first would
    # leave the half that ships unverified.
    idx = np.random.RandomState(0).choice(len(X), 400, replace=False)
    ref = clf.decision_function(X[idx])
    if ref.ndim == 1:
        ref = np.column_stack([-ref, ref])
    worst = worst_split = 0.0
    for j, i in enumerate(idx):
        mine = walk(flat, offsets, targets, baseline, X[i], n_cls)
        worst = max(worst, float(np.max(np.abs(np.array(mine) - ref[j]))))
        split = H.walk(layout, targets, baseline, X[i])
        # The same doubles compared in the same order, so these agree exactly
        # rather than closely. Anything else is a bug in the layout.
        worst_split = max(worst_split, max(abs(a - b) for a, b in zip(split, mine)))
    print(f'max |mine - sklearn decision_function| over 400 tracks = {worst:.3e}')
    assert worst < 1e-6, 'exported trees do not reproduce the model'
    assert worst_split == 0.0, f'the split layout walks to different scores ({worst_split})'

    with open(OUT, 'w', encoding='utf-8') as fh:
        fh.write(H.emit(layout, targets, baseline, CLS, X.shape[0], X.shape[1]))
    print('wrote', OUT, os.path.getsize(OUT), 'bytes')

    # Reference features + expected output, for the C++ parity test.
    sel = list(range(0, len(paths), max(1, len(paths) // 60)))[:60]
    # An even stride over a set this lopsided can miss a small class entirely -
    # reggae is 68 of 12,165 - and then nothing checks that the C++ agrees about
    # what that class is called. Top up to two cases each.
    for c in range(len(CLS)):
        have = [i for i in sel if y[i] == c]
        for i in np.flatnonzero(y == c)[:max(0, 2 - len(have))]:
            sel.append(int(i))
    sel = sorted(set(sel))
    cases = config.REFERENCE_CASES
    with open(cases, 'w', encoding='utf-8') as fh:
        for i in sel:
            probs = clf.predict_proba(X[i:i + 1])[0]
            fh.write(paths[i] + '\t' + CLS[y[i]] + '\t' + CLS[int(np.argmax(probs))] + '\t'
                     + f'{probs.max():.6f}' + '\t'
                     + ' '.join(repr(float(v)) for v in X[i]) + '\n')
    print('wrote reference_cases.tsv')


if __name__ == '__main__':
    main()
