Analysis pipeline
=================

The reference implementation of `bpmcore`, and the training pipeline that
produces `bpmcore/rhythm_model.h`.

`odf.py`, `tempo.py`, `features.py` and `predict_bpm.py` are a line-for-line
counterpart of the C++ in `bpmcore/`. They exist so the algorithm can be changed
and measured against thousands of tracks in minutes rather than recompiled, and
so the C++ has something to be checked against — `bpmcore_test pipeline` and
these scripts agree to the printed precision on every track tried.

Requirements: Python 3.10+, `numpy scipy scikit-learn mutagen`, and `ffmpeg` on
the PATH.


Running it
----------

Set `TANGO_WORK` to a scratch directory (it defaults to `build/analysis` in the
repository) and `TANGO_FFMPEG` if ffmpeg is not on the PATH.

```bash
# 1. Read tags from every collection. Genre is the rhythm ground truth; an
#    integer BPM is a hand tap, a fractional one is machine written and ignored.
#    All five roots: the figures in docs/tango-analysis.md are over the lot,
#    and leaving two out fits on 10,189 tracks rather than 12,165.
python scan_tags.py C:\TangoTunes C:\SortedTangoSpanishNames \
                    C:\SortedTangoSpanishNamesFLAC C:\chacarera C:\cortinas

# 2. Decode everything once and cache the band onset envelope. This is the slow
#    step - about an hour for 12,000 tracks - and is restartable.
python extract_odf.py

# 3. Turn the cached envelopes into feature vectors, and into a BPM estimate
#    under each of the four rhythm hypotheses.
python build_features.py

# 4. Fit the classifier and write bpmcore/rhythm_model.h plus the reference
#    cases the CTest case checks it against.
python train_rhythm_model.py

# 5. Cross-validated accuracy for both halves of the problem.
python evaluate.py
```

Step 4 verifies its own export before writing anything: it walks the trees it is
about to emit and requires them to reproduce scikit-learn's `decision_function`
exactly.


What each file is
-----------------

| file | |
|------|-|
| `config.py` | scratch paths and tool locations, all overridable by environment |
| `tango_labels.py` | rhythm ground truth from the genre tag, and which BPM tags count as hand-tapped |
| `scan_tags.py` | walks the collections, dumps every tag to `tags.jsonl` |
| `odf.py` | reference onset envelope — mirrors `bpmcore/odf.cpp` |
| `tempo.py` | reference novelty, autocorrelation and comb scoring — mirrors `bpmcore/tempo.cpp` |
| `features.py` | reference metrical grid and feature vector — mirrors `bpmcore/rhythm.cpp` |
| `predict_bpm.py` | reference metrical-level choice — mirrors `tapped_bpm` |
| `dataset.py` | joins the tag dump to the envelope cache |
| `extract_odf.py` | decodes and caches envelopes, in parallel |
| `build_features.py` | feature matrix and per-hypothesis BPM |
| `train_rhythm_model.py` | fits the model and emits the C++ header |
| `rhythm_model_header.py` | the header's layout, kept apart from the fit so it can be changed without one |
| `evaluate.py` | grouped cross-validation for rhythm and BPM |

The feature order in `features.py` (`FEATURE_NAMES`) and in
`bpmcore/rhythm.cpp` (`build_features`) must stay in step — the exported weights
carry no names. `bpmcore_test model` is what catches a divergence.


A note on evaluation
--------------------

`evaluate.py` groups by recording, not by file. The collections overlap heavily:
the same side often exists as a shellac transfer, a declicked copy, a retuned
copy and a compilation track, each tagged and tapped separately. Splitting
without grouping puts the same performance in both halves and flatters the
result by several points.
