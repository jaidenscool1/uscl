// args: <out dir> <hex address>...  Decompiles every function that references the addresses to <out dir>/<entry>.c.
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import java.io.File;
import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;

public class RefsTo extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File out = new File(a[0]);
        out.mkdirs();
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        Set<Function> done = new HashSet<>();
        for (int i = 1; i < a.length; i++) {
            for (Reference r : getReferencesTo(toAddr(Long.parseLong(a[i], 16)))) {
                Function f = getFunctionContaining(r.getFromAddress());
                println("ref " + a[i] + " <- " + r.getFromAddress() + " " + r.getReferenceType() + " in " + (f == null ? "?" : f.getEntryPoint()));
                if (f != null && done.add(f)) {
                    DecompileResults res = dec.decompileFunction(f, 120, monitor);
                    try (PrintWriter w = new PrintWriter(new File(out, f.getEntryPoint() + ".c"))) {
                        w.println(res.getDecompiledFunction() == null ? "// " + res.getErrorMessage() : res.getDecompiledFunction().getC());
                    }
                }
            }
        }
        dec.dispose();
    }
}
