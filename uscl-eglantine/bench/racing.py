"""racing.py <runs.py output>: racing mean (chunks[5:]) and min per label."""
import ast, sys
lab = None
for l in open(sys.argv[1]):
    if l.startswith("  chunks:") and lab:
        try: c = ast.literal_eval(l.split("chunks:")[1].strip())
        except Exception: print(lab, "FAILED"); continue
        r = c[5:]; print(f"{lab:10} racing {sum(r)/len(r):5.1f}  min {min(r):5.1f}  n {len(r)}")
    elif not l.startswith(" "): lab = l.split()[0]
    if "STUCK" in l: print(l.strip())
