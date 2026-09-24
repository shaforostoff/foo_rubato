# -*- coding: utf-8 -*-
import os, sys, hashlib, json
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tango_labels as T
import config

CACHE = config.CACHE

def cpath(p):
    h = hashlib.sha1(p.encode('utf-8')).hexdigest()
    return os.path.join(CACHE, h[:2], h + '.npy')

def load_odf(p):
    c = cpath(p)
    if not os.path.exists(c):
        return None
    try:
        return np.load(c).astype(np.float32)
    except Exception:
        return None

def records(need_bpm=False, need_cache=True):
    out = []
    for r in T.load(config.WORK):
        if T.excluded(r):
            continue
        v = T.hand_tapped(r)
        if need_bpm and v is None:
            continue
        if need_cache and not os.path.exists(cpath(r['path'])):
            continue
        r['hand'] = v
        r['cls'], r['cls_src'] = T.label(r)
        out.append(r)
    return out
