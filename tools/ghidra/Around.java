// args: <out dir> <depth up> <depth down> <hex address>...  Decompiles each function, its callers up to <depth up>
// levels and its callees down to <depth down> levels, into <out dir>/<address>.c, and prints the call edges.
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.File;
import java.io.PrintWriter;
import java.util.*;

public class Around extends GhidraScript {
    DecompInterface dec;
    File out;
    Set<Function> done = new HashSet<>();

    void dump(Function f) throws Exception {
        if (!done.add(f)) return;
        DecompileResults res = dec.decompileFunction(f, 120, monitor);
        try (PrintWriter w = new PrintWriter(new File(out, f.getEntryPoint() + ".c"))) {
            w.println(res.getDecompiledFunction() == null ? "// " + res.getErrorMessage() : res.getDecompiledFunction().getC());
        }
    }

    void up(Function f, int depth) throws Exception {
        dump(f);
        if (depth == 0) return;
        for (Function c : f.getCallingFunctions(monitor)) {
            println("call " + c.getEntryPoint() + " -> " + f.getEntryPoint());
            up(c, depth - 1);
        }
    }

    void down(Function f, int depth) throws Exception {
        dump(f);
        if (depth == 0) return;
        for (Function c : f.getCalledFunctions(monitor)) {
            println("call " + f.getEntryPoint() + " -> " + c.getEntryPoint());
            down(c, depth - 1);
        }
    }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        out = new File(a[0]);
        out.mkdirs();
        dec = new DecompInterface();
        dec.openProgram(currentProgram);
        for (int i = 3; i < a.length; i++) {
            Function f = getFunctionContaining(toAddr(Long.parseLong(a[i], 16)));
            if (f == null) { println("no function at " + a[i]); continue; }
            up(f, Integer.parseInt(a[1]));
            down(f, Integer.parseInt(a[2]));
        }
        dec.dispose();
    }
}
