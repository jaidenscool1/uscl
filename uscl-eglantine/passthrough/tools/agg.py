import sys, os, re, collections
d = sys.argv[1]
WAIT = re.compile(r'futex|nanosleep|clock_nanosleep|poll|epoll_wait|sched_yield|pthread_cond|__lll_lock_wait|syscall$|select|read$|recv|ppoll|sem_wait|usleep|sleep_for|_mm_pause|wait_for|condition_variable')
KEYS = ['Nx2Passthrough', 'Puller::', 'Maxwell3D::', 'KeplerCompute::', 'MacroEngine|Macro|MME', 'DmaPusher::', 'Nx2Passthrough::.*Words|Words::', 'TranslateHeader|RefreshPools', 'VtWorker|AstcWorker|Decompress|CompressBC|Swizzle', 'GpuSplit', 'Dynarmic|dynarmic|JIT', 'Kernel|KThread|Svc', 'vk|Vulkan']
threads = collections.defaultdict(lambda: {'n': 0, 'busy': 0, 'top': collections.Counter(), 'incl': collections.Counter(), 'kind': collections.Counter()})
files = sorted(f for f in os.listdir(d) if f.startswith('s'))
for f in files:
    tid = None; frames = []
    def flush():
        if tid is None: return
        t = threads[tid]; t['n'] += 1
        names = [x for x in frames]
        if names and WAIT.search(names[0]):
            return
        if names and any(WAIT.search(x) for x in names[:3]):
            return
        t['busy'] += 1
        top = next((x for x in names if x and not x.startswith('??')), '??')
        t['top'][top[:90]] += 1
        joined = ' '.join(names)
        for k in KEYS:
            if re.search(k, joined): t['incl'][k] += 1
        for x in names:
            if 'DmaPusher::Step' in x: t['kind']['gpu-entry'] += 1; break
            if 'VtWorker' in x: t['kind']['vt'] += 1; break
            if 'AstcWorker' in x: t['kind']['astc'] += 1; break
            if 'Dynarmic' in x or 'dynarmic' in x: t['kind']['cpu'] += 1; break
    for line in open(os.path.join(d, f), errors='replace'):
        m = re.match(r'TID (\d+):', line)
        if m:
            flush(); tid = int(m.group(1)); frames = []; continue
        m = re.match(r'#\d+\s+0x[0-9a-f]+\s*(.*)', line.strip())
        if m: frames.append(m.group(1).strip())
    flush()
print(len(files), 'samples')
for tid, t in sorted(threads.items(), key=lambda kv: -kv[1]['busy']):
    if t['busy'] < 8: continue
    kind = t['kind'].most_common(1)[0][0] if t['kind'] else '?'
    print(f'TID {tid} [{kind}] busy {t["busy"]}/{t["n"]} ({100*t["busy"]/max(1,t["n"]):.0f}%)')
    for k, c in t['incl'].most_common(8): print(f'    incl {100*c/t["busy"]:5.1f}%  {k}')
    for fn, c in t['top'].most_common(10): print(f'    top  {100*c/t["busy"]:5.1f}%  {fn}')
