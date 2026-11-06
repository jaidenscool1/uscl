// args: <out dir> <hex start> <hex end>  Decompiles every function starting in [start, end) to <out dir>/<address>.c.
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import java.io.File;
import java.io.PrintWriter;

public class Range extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File out = new File(a[0]);
        out.mkdirs();
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        AddressSet range = new AddressSet(toAddr(Long.parseLong(a[1], 16)), toAddr(Long.parseLong(a[2], 16) - 1));
        int n = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(range, true)) {
            DecompileResults res = dec.decompileFunction(f, 120, monitor);
            try (PrintWriter w = new PrintWriter(new File(out, f.getEntryPoint() + ".c"))) {
                w.println(res.getDecompiledFunction() == null ? "// " + res.getErrorMessage() : res.getDecompiledFunction().getC());
            }
            n++;
        }
        println(n + " functions");
        dec.dispose();
    }
}
