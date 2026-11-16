"""classify.py perf.data: GPUBack samples by nearest known ancestor (first match walking leaf->root)."""
import collections, subprocess, sys
out = subprocess.run(["./perf", "script", "-i", sys.argv[1], "--comm", "GPUBack", "-F", "ip,sym", "--no-demangle"],
                     capture_output=True, text=True, cwd="/home/main/.local/share/uscl/eglantine-bench").stdout
CATS = [  # (category, substrings) checked per frame from the leaf up; first frame matching any wins
 ("draw:configure", ["MakeConfigureSpecFunc", "ConfigureImpl"]),
 ("draw:pipeline lookup", ["CurrentGraphicsPipeline"]),
 ("draw:flushwork", ["9FlushWork"]),
 ("draw:other", ["RasterizerVulkan4Draw", "RasterizerVulkan11PrepareDraw"]),
 ("compute", ["DispatchCompute", "KeplerCompute"]),
 ("clear/blit/dma", ["Clear", "MaxwellDMA", "Fermi2D", "AccelerateSurfaceCopy"]),
 ("fence/query", ["SignalFence", "QueryCache", "SignalSyncPoint", "FenceManager", "ReleaseFences"]),
 ("cb words", ["CallConstBufferWords", "ProcessCBMultiData", "ProcessCBData", "WriteWordsCached", "WriteBlockCached"]),
 ("state regs", ["CallStateMethodRun", "ProcessDirtyRegisters"]),
 ("macro", ["Macro", "MacroEngine"]),
 ("other methods", ["CallMethod", "CallMultiMethod", "ProcessMethodCall"]),
 ("replay", ["GpuSplit7Execute", "ExecuteRepeated"]),
 ("backloop/idle", ["GpuSplit8BackLoop", "RunIdleHook"]),
]
c, total = collections.Counter(), 0
unk = collections.Counter()
for block in out.split("\n\n"):
    frames = [l.strip().split(None, 1)[-1] for l in block.strip().splitlines()] if block.strip() else []
    if not frames: continue
    total += 1
    for f in frames:
        hit = next((name for name, keys in CATS if any(k in f for k in keys)), None)
        if hit: break
    else:
        hit = "unknown"; unk[frames[0][:80]] += 1
    c[hit] += 1
print(f"{total} GPUBack samples")
for k, n in c.most_common(): print(f"  {100*n/total:5.1f}%  {k}")
print("top unknown leaves:"); [print(f"  {100*n/total:5.1f}%  {k}") for k, n in unk.most_common(8)]
