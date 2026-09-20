Tango tempo and rhythm analysis
===============================

How `bpmcore` works, why it is built the way it is, and how well it does.

The short version: the tempo a dancer taps is not a property of the audio alone
— it depends on which rhythm is playing, because tango, vals, milonga and
reggae are tapped on different metrical levels. So the analysis settles the
rhythm first and reports the tempo on the level that rhythm implies.


What the reference data says
----------------------------

Everything here was derived from and measured against a collection of 12,160
tracks with genre tags, of which **3,692 carry a hand-tapped BPM**. The tapping
is the ground truth for tempo; the genre tag is the ground truth for rhythm,
and is never consulted at analysis time.

Two things fell out of the tapped values immediately.

**The tapping convention differs per rhythm.** Grouped by genre, the hand-tapped
values are strikingly tight, and they sit on four different metrical levels:

| rhythm  |   n  | median | p5–p95  | what is being tapped        |
|---------|-----:|-------:|---------|-----------------------------|
| tango   | 2733 |    125 | 116–136 | the beat (quarter note)     |
| vals    |  455 |     69 |  58–76  | the 3/4 bar                 |
| milonga |  425 |     53 |  41–59  | the 2/4 bar                 |
| reggae  |   14 |     78 |  67–100 | the quarter note, under the skank |

Anchoring on the tapped value itself and asking which multiple of that period
the autocorrelation likes best confirms it: for tango the tapped period *is* the
strongest short periodicity, for vals the audio's beat sits at one third of it,
for milonga at one half or one quarter, and for reggae — usually — at one half.

Reggae is a late addition and its taps need a word. Two more were supplied by
hand for sides that carry no BPM tag, *Rivers of Babylon* at 105 and *Kingston
Town* at 102, which makes 16; the table above counts only the 14 the tags hold,
but the prior below was placed using all of them, and the two are what placed
it. On 14 of the 16 the beat the grid returns is within 3 BPM of exactly twice
the tap, and on the other two — the two slowest sides — it *is* the tap. What
is being tapped is the quarter note throughout; what varies is whether the grid
locked onto the quarter note or onto the skank an octave above it.

**Human tapping repeats to about ±2 BPM.** 469 recordings appear more than once
in the collection — a shellac transfer, a declicked copy, a different
compilation — and were tapped independently each time. Comparing those pairs:

| tap-to-tap difference | share |
|-----------------------|------:|
| identical             |  41%  |
| within 1 BPM          |  68%  |
| within 2 BPM          |  84%  |
| within 3 BPM          |  93%  |

That is the ceiling. An estimator that agrees with a tap to within 2 BPM is
already as close as the same person tapping the same track twice.


The pipeline
------------

### 1. Onset envelope (`odf.cpp`)

Mono, normalised by the track's overall RMS, then a short-time Fourier transform
with a 46ms window every 11.6ms. Magnitudes are compressed as `log(1 + 100·m)`,
differenced between adjacent frames, half-wave rectified and summed into **six
frequency bands** (60, 200, 400, 800, 1600, 3200, 8000 Hz).

Six bands rather than one broadband figure matters here. The original 2009
analysis summed a flux weighted by bin index, which puts most of the weight at
the top of the spectrum — on a 1935 shellac transfer that is surface noise, so
the beat being tracked was largely hiss. Keeping the bands apart lets the tempo
stage normalise each one by its own variation before mixing, and gives the
rhythm classifier something to read.

The geometry is fixed in *seconds*, not samples, and the band edges in Hz. That
gets the window and the hop right in time whatever the input rate, but only a
rate that is 22.05kHz times a power of two — 11.025, 22.05, 44.1, 88.2 —
reproduces the model's 46.4ms window *and* its 21.53Hz per bin exactly. Anything
else is resampled to 22.05kHz first (`resample.cpp`), which is the rate `odf.py`
decodes the training set at and so the rate every figure here was measured at.

The transform size is the nearest even number with no prime factor above 5,
which for those four rates is the power of two they already landed on. It only
does anything on the path where the resampler stands aside — a ratio it cannot
approximate — and the track is analysed at its own rate after all: 46.9ms at
48kHz rather than the 42.7ms a power of two would give, or 46.9 at 32kHz rather
than 64. Sizes built from 2, 3 and 5 are the ones kiss_fft has butterflies for,
so nothing pays for the flexibility.

48kHz is the case that made this necessary. Its window rounds to 2048 points
covering 42.7ms where the geometry asks for 46.4, and the six band edges fall on
different bins, so the same track at 48kHz and at 44.1kHz were two different
analyses — on one recording resampled to both, the metre and the rhythm class
could differ and the classifier's probability moved by up to 0.14. They now
agree to the printed precision, and so do 32kHz, 96kHz and the rest; the
`resample` test case in the harness is what holds that.

### 2. Tempo (`tempo.cpp`)

The bands are normalised and summed, smoothed, local-mean-subtracted and
rectified into a novelty curve; then autocorrelated over **12-second windows
every 3 seconds**, and the windows reduced lag by lag with a **median**.

The median is the part that matters for this repertoire. These are human
performances, often from shellac: the tempo drifts, a singer stretches a phrase,
a bandoneon variation drops behind for a few bars. Taking the median across
windows lets those pass without dragging the answer down — which is what the
hand tapping did too. It also handles a beatless introduction: those windows
simply carry no periodicity and the median ignores them.

A joint search over (beat period, meter) then picks the grid, scoring each
candidate by the autocorrelation at the beat, the bar, two bars and the
subdivision the meter implies, with a penalty for leaving a strong periodicity
off the grid. The period is sharpened against the first eight multiples of
itself at once — a single autocorrelation peak is a couple of frames wide, its
harmonics are not.

### 3. Rhythm (`rhythm.cpp`)

358 features, all metrical rather than timbral:

* the autocorrelation sampled at 24 musically meaningful multiples of the beat
  (the thirds expose a 3/4 bar, the halves and quarters the habanera
  subdivision, the long ones the phrase structure);
* explicit 3-against-4 and 6-against-4 contrasts;
* **bar-synchronous patterns**: one bar of onset energy per band, folded and
  rotated so the strongest low-band accent lands in bin 0. Folded at the
  detected bar length, at one beat, and — so a mis-read meter cannot corrupt the
  whole picture — at a fixed 2, 3 and 4 beats regardless;
* per-band flux share and variability, which is mostly what separates a shellac
  side from a modern cortina;
* novelty curve kurtosis and skew: a sharply articulated marcato and a smooth
  legato line look very different at the same tempo.

Gradient boosted trees over those features (150 iterations, 15 leaves, five
classes). Logistic regression on the same features reaches only 85% — the
interactions are real — so the trees are exported verbatim into
`bpmcore/rhythm_model.h` and walked directly. Both the thresholds and the leaf
values are stored as `float`, for different reasons.

For the thresholds it is not a rounding at all. sklearn bins `float32`
features, so all 10,500 thresholds are already `float` values widened to
`double`, and `classify` compares a `double` feature against one, which widens
it straight back to the number the fit produced. The generator checks that
instead of assuming it, and refuses to narrow a threshold that a refit has
moved off the `float` grid — half an ulp there is enough to send a feature down
the other side of a split, which reads as a slightly different answer on a
handful of tracks rather than as a failure.

The leaf values do round — they are sums of gradients, not boundaries between
two observed feature values, so nothing puts them on the `float` grid. What it
costs is bounded: a class score is the baseline plus one leaf per tree targeting
that class, accumulated in `double`, so the error is 150 half-ulps rather than
anything that compounds. `leaf_slack()` works that bound out of the model itself
— 5.0e-06 on this fit, against scores of order one — and every refit checks the
walk against it. Over the 61 reference cases the scores actually move by
7.6e-08, no track changes class, and the worst gap between the C++ confidence
and sklearn's own `predict_proba` stays where it was at 5.0e-07, against the
harness's 1e-5. For the argmax to notice, two classes would have to be closer
together than that, and at that distance the fit is not choosing between them
either.

Splits and leaves are stored apart, in separate arrays. A split reads a feature,
a threshold and two children; a leaf reads only a value; and one struct carrying
both left one of the two doubles unread in all 21,750 nodes, with two bytes of
padding in each besides. Apart, the same trees take 240,000 bytes instead of
522,000, and the x64 DLL 694KB instead of 959KB. Storing the thresholds and the
leaves at `float` took another 87,000 bytes off every binary afterwards, leaving
153,000 for the whole model.

Worth knowing before reaching for that saving again: **the packaged component
barely moves**, 416,161 bytes to 410,184. What came out was almost entirely
zeros - the threshold every leaf did not use and the value every split did not
use were both 0.0 - and deflate had already collapsed them to nothing. The
saving is real in what gets mapped into a process, which is what matters on a
phone, and close to nothing in what gets downloaded.

The thresholds went the same way: 42,000 bytes out of each binary, but only
about 6,000 out of the archive, because 10,500 values drawn from 6,592 distinct
ones repeat enough that deflate had already found most of it.

The leaves are the exception, and the only part of the model where the download
moves with the binary. 11,250 `double`s, all distinct, are the one thing here
that does not compress: 90,000 bytes deflating to 86,078. As `float` they are
45,000 bytes deflating to 41,530 — 45,000 out of each binary and about 44,500
out of each copy in the archive.

Nothing about the model changes. The layout is generated by
`scripts/analysis/rhythm_model_header.py`, which is separate from the fit
precisely so it can be changed without one, and every refit checks the walk over
the split arrays against sklearn's own `decision_function`.

### 4. The tapped level (`tempo.cpp`, `tapped_bpm`)

Given the rhythm, the beat period is projected onto the levels that rhythm
allows — powers of two for tango and milonga, thirds and sixths for vals — and
each candidate is scored by its autocorrelation support plus a log-normal tempo
prior fitted to the hand tapping.

The prior only ever chooses between levels, which are a factor of at least 1.33
apart. **The value itself always comes from the autocorrelation peak**, so a
prior cannot pull a tempo towards its mean. That separation is deliberate: the
priors are tight (tango is 125.5 with a log sigma of 0.075) and would otherwise
flatten every tango to the same number.

**A near-flat prior does not settle an octave.** Reggae is the case that shows
it. The tap is the quarter note and the grid usually returns the skank an
octave above it, so the two readings differ by a factor of two — and the
autocorrelation supports both almost equally: across 67 reggae sides the two
differ by a median of −0.017, the slower reading being fractionally the better
supported. Under the "other" prior the scores came out within a few hundredths
of each other and the answer was effectively a coin toss; *Ethiopia* scored
3.844 at 141.6 against 3.822 at 70.8, and was tapped at 70. A class with a
prior of its own is what settles that, and is why reggae is a class at all
rather than a label.

**A prior's centre is a boundary, not an average.** Reggae's is 88, which is
above the middle of its own tapped values, 81. A log-normal prior separates two
metrical levels at their geometric midpoint, `mu * sqrt(2)`, so 88 places that
boundary at 124.5 — between the fastest quarter note tapped, 120, and the
slowest skank the grid returns for a tapped side, 128. Centred on the tapped mean instead the
boundary lands at 115, and the two sides whose quarter note the grid found
directly are reported an octave down; fitted to the 13 taps that cluster at
64–90, as it first was, the boundary lands at 107 and takes a third side with
it. Sweeping the centre from 76 to 95, only 86 to 90 puts all 16 on the level they
were tapped at; 88 sits in the middle of that window and wins its levels by
nearly twice the margin 86 does and forty times the margin 90 does.

The width, 0.115, is narrower than the tapped spread of 0.17. That is also
deliberate: the levels being a factor of two apart, a narrow prior separates
them by a wider margin, and every tapped side still sits within three sigma of
the centre.

**The levels on offer have to be levels of the metre.** Two beats is not a
metrical position in a 3/4 bar, and while it was offered a slow vals could be
reported at the two-beat rate instead of the bar: a Peruvian vals at 56 to the
bar sits below anything the Argentine prior expects, so the prior stops
defending the right answer and a spurious two-beat periodicity wins. Removing
that one level took vals from 90.5% to 94.1% within 2 BPM.

"Other" is the exception in two ways. It spans bossa to disco to chacarera and
has no useful tempo prior, so the autocorrelation is weighted nearly four times
as heavily and the prior only breaks ties. And because the class states no metre
of its own, the level set follows the metre the grid search found — duple or
triple — rather than allowing both. Allowing both is what let a son cubano be
read at two thirds of its beat: *Chan Chan* came out at 112 and *Guantanamera*
at 83, neither of which is a rate anything in those recordings moves at.

A constant **+0.5 BPM** is added at the end. Taps run marginally ahead of the
measured pulse across the whole collection; this is a calibration to that habit,
not a correction to the measurement.


Results
-------

Rhythm classification, 5-fold cross-validated with recordings **grouped**, so
the same performance never appears in both halves (the collections overlap
heavily, and many sides exist as a transfer, a declicked copy and a retuned
copy):

```
n=12160   accuracy=93.63%   balanced=78.97%

actual        tango     vals  milonga   reggae    other   recall
tango          8369       23       18        1      112    98.2%
vals             20      932        8        0       46    92.6%
milonga          69       13      617        0       54    81.9%
reggae            1        0        0       29       38    42.6%
other           194      100       72        6     1438    79.4%
precision     96.7%    87.3%    86.3%    80.6%    85.2%
```

Fitting four classes on the same tracks and the same folds, with reggae folded
back into "other", is what the model was before and gives 94.22% accuracy,
88.42% balanced:

```
actual        tango     vals  milonga    other   recall
tango          8396       17       16       94    98.5%
vals             22      922        9       53    91.7%
milonga          69       11      624       49    82.9%
other           201       91       71     1515    80.7%
precision     96.6%    88.6%    86.7%    88.5%
```

So the fifth class costs 0.6 points of accuracy. The balanced figure falls
much further, from 88.42% to 78.97%, but that is arithmetic rather than
regression: it is the mean of the per-class recalls, and reggae's 42.6% is now
one of five terms where before it was not a term at all.

What it costs the existing classes is easy to overstate from the diagonals.
Tango ends 27 sides worse, milonga 7, vals 10 better — but those are net
figures over a much larger churn: 45 tango sides changed class and 18 changed
back, 21 milongas moved out and 14 in, 14 vals out and 24 in. **Reggae takes
seven tracks in the whole collection**, one of them a tango — Juan D'Arienzo's
1971 *La cumparsita (fast)*, now read at 69.9 where it was 139.2. The other
~130 moves are between the four classes that were already there, and are
refitting noise rather than anything reggae did. That is measured, not assumed:
refitting the **four**-class model under a different fold split - a nuisance
change that cannot mean anything - moves 287 of the 12,160 tracks, and adding
the fifth class moves 301. Per class the two are the same size: 65 tango
against 73, 55 vals against 44, 57 milongas against 61, 110 "other" against
123. Milonga recall reads 82.60% and 83.13% under those two four-class splits
and 81.9% with reggae, so what the class costs milonga is about half again the
swing between two arbitrary splits of the model without it.

Reggae is not what moves them, either. Over all 753 milongas the five-class
model gives reggae a mean probability of 0.0001 and never ranks it even second;
the probability those 21 milongas lose goes to "other", which is where a
borderline milonga has always gone. Measured where it matters — against the hand taps — the whole churn is
close to a wash. 18 tapped tracks land closer to their tap and 15 further, and
five of the 18 are reggae sides, so the four original classes come out slightly
behind and well inside the noise of a refit.

The tracks that move furthest are the ones whose two candidate levels are both
defensible: three copies of Biagi's *Pajaro herido* go from 70.6 to 105.7
against a tap of 70, and two of D'Arienzo's *Milonga vieja milonga* from 49.2
to 98.0 against a tap of 50, while D'Arienzo's *Irene* goes from 109.7 to 73.3
against a tap of 73 and Fresedo's *Vuelves* from 63.8 to 127.1 against 129. A
refit moves a handful of those either way; none of it is a property of the new
class.

Restricted to the tango-era collections alone — where every track is a shellac
transfer, so recording quality cannot be doing the work — accuracy is 94.49%,
against 94.74% for four classes, with tango/vals/milonga recall at
98.3 / 92.8 / 82.0%. No reggae is labelled in those collections and one track
was called reggae across all of them.

BPM against the 3,692 hand-tapped tracks, using the **predicted** rhythm:

| rhythm  |   n  | exact | ≤1 BPM | ≤2 BPM | ≤3 BPM | right level |
|---------|-----:|------:|-------:|-------:|-------:|------------:|
| tango   | 2733 | 35.3% |  74.6% |  88.4% |  93.7% |       98.3% |
| vals    |  455 | 44.4% |  87.5% |  95.2% |  95.6% |       96.0% |
| milonga |  425 | 41.2% |  79.8% |  86.8% |  87.5% |       87.5% |
| reggae  |   14 | 21.4% |  64.3% |  64.3% |  64.3% |       64.3% |
| other   |   65 | 35.4% |  69.2% |  76.9% |  80.0% |       83.1% |
| **all** | 3692 | 37.1% |  76.7% |  88.7% |  92.9% |       96.4% |

Reggae was 28.6% within 2 BPM under four classes and is 64.3% under five. The
whole-collection figure is unchanged at 88.7%: tango and vals are flat or
slightly better, milonga gives up 0.7 points and "other" 1.6.

Set against the tap-to-tap repeatability above (68% within 1, 84% within 2, 93%
within 3), the estimator agrees with a tap about as closely as the same person
tapping twice.

The rhythm classifier is what buys most of this. Skipping it and treating every
track as a tango gives 66.7% within 2 BPM instead of 88.7%.

### Where it still misses

* **Metrical level, ~4% of tracks.** Almost all of these are cases where the tap
  itself sat on an unusual level — around 20 milongas tapped on the beat rather
  than the bar, a dozen tangos tapped at half rate. Nothing in the audio
  distinguishes them; the same recording tapped on another day would land
  differently.
* **Milonga recall, 83%.** Milonga is the smallest class and shades into
  candombe and *milonga tangueada*, which are genuinely intermediate.
* **"Other" tempo.** With 52 tapped examples spanning Glenn Miller to Daft Punk
  there is no convention to learn, and what is left is an octave choice with
  nothing to settle it: *Bitter Sweet Symphony* and *La Tanga* have beats within
  1 BPM of each other and were tapped at opposite levels, 85 and 171. Both
  readings are defensible and the engine can only be right about one of them.
  Interestingly the *predicted* rhythm does better here than the true one (80.8%
  vs 65.4% within 2 BPM): the candombes tagged "other" get classified as
  milonga, and the milonga prior then puts them on the level they were actually
  tapped on.
* **Reggae recall, 43%.** 68 labelled sides against tango's 8,523 is not much
  to fit a class on, and the 57% that are missed simply behave as they did
  before — they fall to "other" and take the old octave decision, which is the
  safe direction to fail in. Precision is the figure that matters here and it
  is 80.6%: seven tracks in 12,160 are called reggae and are not. At least one
  of those is arguable rather than wrong — Grace Jones, *I've Seen That Face
  Before*, is Compass Point reggae in everything but the tag — so both figures
  are understated by however much unlabelled reggae sits in "other". More
  labelled examples is the only real remedy.

  The one that costs something is a tango called reggae, because the reggae
  prior then halves it. A tango's beat is 110-140 and a reggae's skank is
  125-180, so the two overlap at the fast end and that is where it can happen.
  One side in 8,523 did, held out: D'Arienzo's 1971 *La cumparsita (fast)*, a
  transfer running over speed, which the fitted model itself calls tango at
  139.2.
* **Reggae slower than about 62 to the quarter note.** The prior separates the
  levels at 124.5, which sits just under the slowest skank in the collection,
  128. A side whose skank fell below that — a quarter note under 62 — would be
  reported at the skank rather than at the tap. Nothing in the collection is
  that slow, and the boundary cannot be lowered without giving up the sides
  whose quarter note the grid finds directly, at 102 and 120.
* **Beat search floor, ~96 BPM.** Eight multiples of the beat have to fit inside
  the five-second autocorrelation. Every rhythm here sits well above that — a
  tango beat is 110–140, a vals beat around 205 — but a genuinely slow piece is
  found through a subdivision and divided back down.


Performance
-----------

Measured on a 169-second track, one core of a Ryzen 7 PRO 250, and after the
optimisations below:

| input rate |  1 thread | 2 threads | all cores | resampled |
|------------|----------:|----------:|----------:|:---------:|
| 11025      |    0.079s |    0.048s |    0.020s |     no    |
| 22050      |    0.146s |    0.088s |    0.039s |     no    |
| 32000      |    0.193s |    0.116s |    0.059s |    yes    |
| 44100      |    0.208s |    0.125s |    0.064s |     no    |
| 48000      |    0.202s |    0.121s |    0.057s |    yes    |
| 88200      |    0.373s |    0.228s |    0.119s |     no    |

Resampling is close to free even on one thread — the smaller transform very
nearly pays for the filter — and 48kHz now comes out *faster* than 44.1kHz
despite the extra stage, because it is analysed at 22.05kHz where the transform
is a quarter of the size.

Where the time goes at 22.05kHz, on one thread and on all cores:

| stage      | 1 thread | all cores |
|------------|---------:|----------:|
| envelope   |  0.1221s |   0.0282s |
| autocorr   |  0.0110s |   0.0034s |
| features   |  0.0010s |   0.0015s |
| everything else | 0.0004s | 0.0006s |

And inside the envelope, measured by stubbing each piece out in turn: the
transform and the loops around it are 65%, `log` is 32%, `sqrt` is 3%.

The spectral stage is 90–97% of the run time; nothing else is worth optimising
until it is. What was done:

* **Only the used bins leave the transform.** The bands stop at 8kHz, so on a
  44.1kHz file 370 bins of the 1025 produced are turned into magnitudes. The
  logarithm is the single most expensive operation in the loop.
* **The window is the nearest even 5-smooth size.** Every rate the resampler
  lets through is a power of two anyway, so this costs those nothing; it keeps
  the window near 46.4ms on the fallback path, where a power of two can be 40%
  out. Rounding *up* to a power of two, which is where this started, gave a
  48kHz file an 85ms window — twice the work and a different analysis again.
* **Two tight loops, not one fused one.** Computing the whole span of logarithms
  and differencing afterwards measured a third faster than interleaving them:
  the transcendental loop pipelines cleanly only when nothing else is storing
  alongside it.
* **`log(1 + z)` rather than `log1p(z)`.** At these magnitudes the difference
  never reaches the sums, and `log` is measurably faster.
* **Optional threading over frame blocks.** Blocks are handed out from a shared
  counter, and each block re-derives the frame before its first, so the envelope
  is **bit-identical whatever the thread count** — verified in the test harness.
  On two cores this is worth about 1.55×.
* **The autocorrelation is threaded too**, over windows rather than over lags.
  The windows do not interact — the median that reduces them is taken
  afterwards, lag by lag — so a slot is reserved per window and filled in
  parallel. It was 8% of the run on one thread and 20% once the envelope was
  spread out; that is now 3.4ms.
* **So is the resampler**, over output blocks. Every output sample is an
  independent dot product over a fixed window of the input, so how the range is
  divided cannot change a bit of it. The samples at each end of the track are
  gathered into a zero-padded window and put through the *same* dot product
  rather than summed with the missing taps skipped — skipping them would sum in
  a different order and the two paths have to agree exactly.
* **Four accumulators in the filter's inner loop.** MSVC will not reassociate
  float addition, so a single running total stays scalar; four independent ones
  let it use the whole vector unit and are still deterministic.

Resampling used to be on the *not done* list, on the grounds that a decimating
FIR good enough to keep aliasing out of the 3200–8000Hz band costs about as much
as it saves. That was wrong about the filter. The envelope never reads above
8kHz, so an alias landing between 8 and 11kHz lands in bins nothing looks at and
the stopband only has to start where the first alias of the *passband* would
fold back — 22050 − 8000 = 14050Hz. Six kilohertz of transition band instead of
one is the difference between a filter that costs more than the transform it
feeds and one that costs a fraction of it: 40 taps per output sample for 80dB of
alias rejection, measured at 82–88dB, with the passband flat to 0.01% out to
7900Hz.

Still deliberately *not* done:

* **Analysing 44.1kHz through the resampler.** It halves the transform, and the
  filter is cheap enough now, but measured end to end it is worth only 7% on one
  thread and 16% on all cores — the filter eats most of what the smaller
  transform saves. For that it would move every answer on the rate that carries
  the measured accuracy. 88.2kHz would gain more, around 37%, but it is rare and
  the same objection applies.
* **Float precision.** A float transform and `logf` would be worth something like
  40% of the envelope between them. But the classifier is gradient boosted trees
  splitting on hard thresholds, so a feature moving in its last bits is enough to
  hand back a different probability, and on a near-tie a different rhythm. Not a
  trade worth making on a stage that already runs at 4300× realtime.
* **Analysing less than the whole track.** The median across autocorrelation
  windows is what makes the estimate robust, and fewer windows is a weaker
  median. It would cut the decode, which is the part that actually costs — but
  see below for what to do about that instead.

### The decode, not the analysis

In the component the analysis is not what takes the time. A three-minute side is
0.04–0.06s of analysis and anywhere from a fraction of a second to tens of
seconds of decoding, depending on the codec and where the file lives — Monkey's
Audio at its higher compression settings runs at ten or twenty times realtime,
and a network share can be worse. Two things follow:

* The decode is opened with `input_flag_simpledecode`. The whole side is read
  once, front to back, and never seeked, so a decoder need not build a seektable
  it will never be asked for; and a format carrying looping metadata is not
  decoded round and round until the length cap stops it.
* With *output debug information* on, each track logs its audio length, the time
  to read it and the time to analyse it as separate numbers. They have nothing to
  do with each other and only one of them is this component's to fix.

Tracks are still analysed one at a time, so a library scan is bounded by one
decoder on one core. Running several tracks at once is the obvious next step and
the only one that would help a slow codec.


### What the spectral stage costs at float

The transform is 65% of the envelope loop and the envelope is about 90% of the
analysis, so an embedded build wants a SIMD FFT — and every one worth having is
single precision. That is a question about the analyser's arithmetic rather
than about which library computes it, so it was settled first and separately.

KISS is scalar C at either width and measures 0.1053s against 0.1067s over a
three-minute track, so the width buys no speed by itself. What it buys is the
ability to ask the question before any of the porting work exists: build
`BPMCORE_FFT_SCALAR=double` and `=float`, run `bpmcore_test batch` over the
collections under each, and diff with `scripts/analysis/compare_batch.py`.

Over **12,157 tracks analysed by both**, nothing moved:

| | |
|---|---|
| metrical level flipped | 0 |
| meter changed | 0 |
| rhythm class changed | 0 |
| BPM \|delta\|, median / p99 / max | 0.000000 / 0.000013 / 0.056 |
| confidence \|delta\|, median / p99 / max | 0.000000 / 0.000000 / 0.198 |

Against the 3,692 hand taps both widths read 37.84% exact, 90.03% within 2 BPM
and 97.75% right level — the same figures to two decimals, with not one tapped
track landing closer to its tap or further from it. Set against the refit noise
floor above, which would predict 287 tracks moving on a collection this size,
float moved none.

The counts being zero hides the near misses, and those are worth naming because
they show the mechanism that would eventually bite. Every one of the largest
disagreements is a Canaro acoustic side from the 1920s — the flattest, noisiest
transfers here, where the classifier and the autocorrelation peak both have the
least contrast to work from. The extreme case is *Gabriela* (1927, vals), whose
probability fell from 0.9900 to 0.7915 while the answer stayed vals at 57.916
against a beat of 171.21. A change that large from a perturbation of 1e-5 is a
feature crossing a split in the trees, which moves a leaf and so the probability
discretely rather than smoothly. That is exactly the failure mode float was
suspected of, and on this collection it fires once in twelve thousand sides and
does not reach the decision.

Two caveats on what this measured. The sweep decodes to the model rate, so the
transform is 1024 points; a 44.1kHz path takes 2048 and accumulates over one
more radix-2 stage. And it measured float **KISS**, which is the same algorithm
at a narrower width — a different library reorders and uses different
butterflies, so it needs its own check with KISS as the oracle. That is the
reason to keep KISS after a faster transform arrives, rather than as a fallback
nobody would ship.

Some of the risk was retired before the sweep ran: the reference pipeline the
model was fitted with already stores its spectrum as float32 (`stft_mag` in
`scripts/analysis/odf.py`), so the trees were trained on float32 magnitudes.
Not the same thing as a float transform — numpy computes in double and rounds
the result, where a float FFT accumulates over all its stages — but it means
the width the features arrive at was never the double one.


### What the magnitude loop costs at float

The width above is the transform's. The loop that turns its output into the
feature - a magnitude and a compressed logarithm per bin, per frame - is a
separate question, because it runs at whatever width its own arithmetic is
written in whatever the transform did. It was left at double when the transform
question was settled, and it is the largest single cost in the analysis, so it
was asked again on its own terms.

`logf` and `sqrtf` are about a third cheaper than their double counterparts.
Narrowing the two of them, and the magnitude they consume, leaves the
difference that follows at double so that the subtraction between frames stays
exact.

Measured the same way, as the second of two changes over the full collection
(`bt_acf.exe` against `bt_log.exe`, everything else identical). Over **12,159
tracks analysed by both**, nothing moved:

| | |
|---|---|
| metrical level flipped | 0 |
| meter changed | 0 |
| rhythm class changed | 0 |
| BPM \|delta\|, median / p99 / max | 0.000000 / 0.000006 / 0.000453 |
| confidence \|delta\|, median / p99 / max | 0.000000 / 0.000000 / 0.003553 |

Against the hand taps both read 37.84% exact, 90.03% within 2 BPM and 97.75%
right level, with not one tapped track landing closer to its tap or further
from it.

That is a quieter result than the transform's own narrowing, which is what it
should be: the largest disagreement there was 0.056 BPM and a confidence that
moved 0.198 on one Canaro side, where here the largest is 0.0005 BPM and 0.004.
A rounding applied once to a magnitude perturbs less than one accumulated
through every stage of a transform.

There is also a reason to expect it rather than merely to find it. The
reference pipeline the model was fitted with computes this same loop at float32
already - `stft_mag` in `scripts/analysis/odf.py` rounds each magnitude to
float32, and the `np.log1p` that follows stays there. Double was the wider of
the two, not the truer one; this narrowing moves bpmcore towards the arithmetic
the trees were trained on rather than away from it.

### Swapping the transform for PFFFT

With the width settled, the library was the remaining question, and it is a
different one: PFFFT reorders its output, packs the two real-valued bins
together, computes with different butterflies in a different order, and refuses
sizes KISS accepts. None of that can be argued from the source.

At the sizes bpmcore uses it is **about six times faster** than KISS, measured
over the same frames on the same machine:

| N | KISS | PFFFT ordered | PFFFT raw |
|---|-----:|--------------:|----------:|
| 1024 | 71.9ms | 11.2ms (6.4x) | 10.0ms (7.2x) |
| 2048 | 107.5ms | 19.1ms (5.6x) | 17.3ms (6.2x) |
| 4096 | 131.9ms | 21.9ms (6.0x) | 20.2ms (6.5x) |

The ordered variant is the one used. The raw layout is about a tenth faster but
permuted, and band edges are bin numbers, so a permuted spectrum would have to
be reordered anyway.

End to end that is less dramatic, because the transform is 65% of the envelope
loop rather than all of it, and the envelope carries fixed costs the transform
does not touch - the loudness pass over the whole track, and the setup. On a
three-minute side at 44.1kHz, single threaded:

| | KISS, double | PFFFT, float |
|---|---:|---:|
| envelope | 0.1994s | 0.1143s |
| whole analysis | 0.2136s | 0.1284s |

**1.74x on the envelope, 1.66x overall.** On the desktop that is still invisible
next to the decode; on ARM, where the envelope is a much larger share of a
battery-powered budget, it is the point of the exercise.

Correctness was checked twice over. `fft_backend_test` builds both transforms
into one binary at the same width and requires them to agree on every size
bpmcore can ask for; the worst disagreement is 1.3e-7 relative, about one float
epsilon, which is what two correct implementations differing only in summation
order should give. Then the whole collection was analysed again with PFFFT and
diffed against the KISS run: of the 12,157 tracks both runs analysed, **not one
moved at all** - no class, no meter, no metrical level - against the 287 in
12,160 a meaningless refit moves. Half the collection agreed to the last digit
printed; the 99th percentile of the BPM disagreement was 1.3e-5 and the largest
anywhere was 0.056. Tap accuracy came out identical against 3,692 hand taps,
37.84% exact and 97.75% on the right level, with no track landing either closer
to its tap or further from it.

One track's confidence moved 0.20 without its answer changing, which is the same
discrete jump the float sweep turned up: a feature crossing a split sends the
walk down the other side of a tree, and the tree votes differently while the
sum still lands on the same class. It is worth remembering that this is what a
near miss looks like here - not a drift in the BPM, but a confidence that falls
while the answer stays put.

Two things worth knowing about PFFFT, both found by running it rather than
reading it:

* **It does not enforce its own size restriction.** `pffft_new_setup` checks
  `N % 32` with `assert()` alone, so a release build hands back a working-looking
  setup for a size it cannot transform and then produces a wrong spectrum in
  silence. `fft_size_for` and `fft_size_supported` in `bpmcore/real_fft.h` are
  what stand in the way, and the `fft_sizes` case in `bpmcore_test` is what
  keeps them honest.
* **Its size grid is coarser than KISS's**, 5-smooth multiples of 32 against
  even 5-smooth numbers, so rounding the window to a usable size can move it
  further. Every rate that is the model rate times a power of two still lands
  exactly, under either. On the odd-rate fallback, where no usable resampling
  ratio existed, KISS costs at most 3.1% of the window duration and PFFFT at
  most 5.0% - against the 37.8% a plain power of two would cost at 32kHz.

PFFFT also falls back to scalar code, silently, when it finds no SIMD for the
target. `fft_backend_test` prints `pffft_simd_size()` for that reason: a build
that quietly lost its SIMD is six times slower than it was meant to be and says
nothing about it.

**PFFFT at float is now the default**, and KISS at double is the reference
build. When this was first written the default stayed where it had always been,
on the reasoning that a measurement showing nothing moved is not by itself a
reason to move. What changed is not the evidence but what the transform is a
share of. The whitening median, the flux sums and the frame RMS have since been
taken out of the way, so the transform is a larger fraction of what remains and
the same swap now buys more of a shorter run: on a 196-second side at the model
rate, one thread, 0.261s on KISS at double against 0.163s on PFFFT at float -
**1.60x**, where before those three loops were fixed it was 1.42x. A dual-core
2014 MacBook Air is the machine this has to feel quick on, and there that is
the whole of the difference. It costs nothing in size either: the 64-bit DLL is
about 9KB *smaller* on PFFFT.

Two things the switch does mean, neither of them new but both now shipping:

* **PFFFT's answer depends on the SIMD it found.** Its scalar fallback sums in
  a different order from its SSE path, and NEON is a third. So the two slices
  of the macOS universal bundle can differ in the last bits of a magnitude, as
  can a machine whose build lost its SIMD. That is the same class of difference
  `fft_backend_test` measures at 1.3e-7 relative, and it is far below anything
  that reaches a printed figure - but it is why that test prints
  `pffft_simd_size()`, and why an odd result on a new architecture is worth
  checking there first.
* **KISS is not a fallback, it is the oracle.** It stays vendored and built at
  both widths for exactly that reason. `-Kiss` on Windows and `--kiss` on macOS
  build the double reference, each into its own build directory and its own
  archive name.


Reproducing the model
---------------------

See `scripts/analysis/README.md`. The pipeline regenerates
`bpmcore/rhythm_model.h` and `bpmcore_test/reference_cases.tsv`; the
`rhythm_model` CTest case then checks the exported trees still reproduce the
classifier they came from.
