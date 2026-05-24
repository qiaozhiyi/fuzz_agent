#!/usr/bin/env python3
"""Extract a fuzzing dictionary from .rodata only via objcopy."""
import json, subprocess, sys, os, tempfile
from collections import Counter

if len(sys.argv) != 2:
    sys.exit('usage: extract_strings_dict.py <binary>')
binary = sys.argv[1]
out_dir = os.path.dirname(binary)

# 1. objcopy .rodata to a flat file
with tempfile.NamedTemporaryFile(suffix='.rodata', delete=False) as tmp:
    rodata_path = tmp.name
subprocess.run(['objcopy', '-O', 'binary', '--only-section=.rodata',
                binary, rodata_path], check=True)
# 2. strings on the .rodata blob
proc = subprocess.run(['strings', '-n', '4', rodata_path],
                       capture_output=True, text=True, errors='replace')
os.unlink(rodata_path)
raw = proc.stdout.splitlines()

DROP_PREFIXES = ('__sanitizer','__afl','__asan','__ubsan','__msan','__tsan',
    '__memprof','__hwasan','__gcov','__llvm','__cxx_','asan.module',
    'GCOV','AFL_','AFL+','/build/','/usr/lib/','__cxa_','GLIBC',
    '/opt/','/tmp/','libgcc','libstdc','##SIG_AFL')
DROP_SUBSTRS = ('sanitizer_cov','gcov_','_pad_unaligned',
    '_GLOBAL__sub_','_ZN','_ZL','AFL_DISABLE','AFL_DEBUG')

def keep(s):
    n = len(s)
    if n < 4 or n > 60: return False
    if any(s.startswith(p) for p in DROP_PREFIXES): return False
    if any(sub in s for sub in DROP_SUBSTRS): return False
    if any(ord(c) < 0x20 or ord(c) > 0x7e for c in s): return False
    alnum = sum(1 for c in s if c.isalnum())
    if alnum < n * 0.5: return False
    if not any(c.islower() for c in s): return False
    return True

uniq = Counter(s for s in raw if keep(s))
top = uniq.most_common(256)

dpath = os.path.join(out_dir, 'extracted.dict')
with open(dpath, 'w') as fp:
    fp.write(f'# .rodata-extracted dictionary for {os.path.basename(binary)}\n')
    fp.write(f'# {len(uniq)} unique tokens; top {len(top)} written\n')
    for tok, c in top:
        esc = tok.replace('\\','\\\\').replace('"','\\"')
        fp.write(f'"{esc}"\n')

jpath = os.path.join(out_dir, 'extracted.json')
with open(jpath, 'w') as fp:
    json.dump({'binary': os.path.basename(binary),
               'binary_size': os.path.getsize(binary),
               'total_unique_tokens': len(uniq),
               'top256_written': len(top),
               'top20_samples': [{'token':t,'count':c} for t,c in top[:20]]}, fp, indent=2)

print(f'{os.path.basename(binary)}: unique={len(uniq)} top10={[(t,c) for t,c in top[:10]]}')
