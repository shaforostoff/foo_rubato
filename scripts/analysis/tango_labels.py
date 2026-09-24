# -*- coding: utf-8 -*-
"""Shared ground-truth labelling for the tango collections."""
import json, os, re

CLASSES = ['tango', 'vals', 'milonga', 'reggae', 'other']


def parse_bpm(rec):
    """Return (value, is_decimal). Decimal values are machine-written, not hand-tapped."""
    b = rec.get('bpm')
    if b is None:
        return None, None
    s = str(b).strip()
    if not s:
        return None, None
    dec = ('.' in s) or (',' in s)
    try:
        v = float(s.replace(',', '.'))
    except ValueError:
        return None, None
    if not (10.0 <= v <= 400.0):
        return None, None
    return v, dec


def hand_tapped(rec):
    """BPM the user tapped by hand, or None. Integers only, decimals are rejected."""
    v, dec = parse_bpm(rec)
    if v is None or dec:
        return None
    return v


# genre-tag phrases, longest/most specific first
_RULES = [
    ('milonga', ['milonga candombe', 'milonga tangueada', 'milonga criolla',
                 'milonga campera', 'milonga portena', 'tango milonga', 'milongon',
                 'milonga']),
    ('vals',    ['vals criollo', 'vals cancion', 'vals peruano', 'vals pasillo',
                 'vals serenata', 'valsecito', 'vals', 'waltz', 'walzer']),
    # 'argentinetango' is one word as some taggers write it. Without it the
    # word boundary below misses it entirely, and 24 modern-orchestra tangos
    # in C:/TangoTunes/Modern went into training as 'other' - which, with every
    # other tango a shellac side, taught the model that a clean recording is
    # not a tango.
    ('tango',   ['tango cancion', 'tango sinfonico', 'tango canyengue', 'tango negro',
                 'tango campero', 'tango electronico', 'tango nuevo', 'argentinetango',
                 'argentine tango', 'tango']),
    ('reggae',  ['roots reggae', 'rocksteady', 'rock steady', 'reggae']),
]

# The one place a directory names the rhythm. Everywhere else the path is
# deliberately ignored - a collection folder is named after the collection, so
# matching on it would call every pasodoble under C:/TangoTunes a tango - but
# this folder is named after the music in it and was confirmed to hold nothing
# else. It has to be consulted: only 25 of the 67 sides in it carry a genre tag.
_FOLDERS = [
    ('reggae', ['cortinas/reggae']),
]

# Folders left out of training and evaluation altogether, because no class
# describes them: calling El Cachivache Quinteto 'tango' teaches the model
# that tango punk is danced, and calling it 'other' teaches it that a modern
# tango orchestra is not a tango - the very mistake the 'argentinetango' rule
# above was added to undo.
_EXCLUDED = ['modern/2018 - el cachivache quinteto']


def excluded(rec):
    p = rec['path'].replace(os.sep, '/').replace('\\', '/').lower()
    return any('/' + f in p for f in _EXCLUDED)

_ACC = str.maketrans('áàâäãéèêëíìîïóòôöõúùûüñç', 'aaaaaeeeeiiiiooooouuuunc')


def _norm(s):
    return (s or '').lower().translate(_ACC)


def _match(text):
    t = _norm(text)
    for cls, phrases in _RULES:
        for p in phrases:
            if re.search(r'(?<![a-z])' + re.escape(p) + r'(?![a-z])', t):
                return cls
    return None


def label(rec):
    """Ground-truth rhythm class from the genre tag, falling back to the file name.

    The full path is deliberately NOT used, bar the folders in `_FOLDERS`: a
    collection directory is usually named after the music in it, so every file
    under a "TangoTunes" folder would otherwise match 'tango' - including the
    pasodobles and foxtrots.
    """
    p = rec['path'].replace(os.sep, '/').replace('\\', '/').lower()
    for cls, folders in _FOLDERS:
        for f in folders:
            if '/' + f + '/' in p:
                return cls, 'folder'
    g = rec.get('genre')
    if g:
        m = _match(g)
        if m:
            return m, 'genre'
    # File names in these collections end with " - <Genre>.<ext>"
    base = os.path.splitext(os.path.basename(rec['path']))[0]
    tail = base.rsplit(' - ', 1)[-1] if ' - ' in base else ''
    m = _match(tail)
    if m:
        return m, 'filename-tail'
    m = _match(base)
    if m:
        return m, 'filename'
    return 'other', ('genre-other' if g else 'no-genre')


def load(work_dir=None):
    """Read the tag dump produced by scan_tags.py."""
    import config
    path = os.path.join(work_dir, 'tags.jsonl') if work_dir else config.TAGS
    return [json.loads(l) for l in open(path, encoding='utf-8')]
