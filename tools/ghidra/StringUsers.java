// args: <out dir> <string>...  For each string: where it is, which TOC slots point at it, which functions use
// either, and their decompiled C in <out dir>/<function address>.c. PS3 code loads string pointers from the TOC.
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import java.io.File;
import java.io.PrintWriter;
import java.util.*;

public class StringUsers extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args[0]);
        out.mkdirs();
        Memory mem = currentProgram.getMemory();
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        Set<Function> done = new HashSet<>();
        for (int i = 1; i < args.length; i++) {
            byte[] needle = (args[i] + "\0").getBytes("ASCII");
            for (Address s = mem.findBytes(mem.getMinAddress(), needle, null, true, monitor); s != null;
                 s = mem.findBytes(s.add(1), needle, null, true, monitor)) {
                List<Address> users = new ArrayList<>();
                users.add(s);
                long v = s.getOffset();
                byte[] ptr = {(byte) (v >> 24), (byte) (v >> 16), (byte) (v >> 8), (byte) v};
                for (Address p = mem.findBytes(mem.getMinAddress(), ptr, null, true, monitor); p != null;
                     p = mem.findBytes(p.add(1), ptr, null, true, monitor)) {
                    if (p.getOffset() % 4 == 0) users.add(p);
                }
                for (Address u : users) {
                    for (Reference r : getReferencesTo(u)) {
                        Function f = getFunctionContaining(r.getFromAddress());
                        println(args[i] + " @" + s + " via " + u + " <- " + r.getFromAddress() + " in " + (f == null ? "?" : f.getEntryPoint()));
                        if (f != null && done.add(f)) {
                            DecompileResults res = dec.decompileFunction(f, 120, monitor);
                            try (PrintWriter w = new PrintWriter(new File(out, f.getEntryPoint() + ".c"))) {
                                w.println("// uses: " + args[i]);
                                w.println(res.getDecompiledFunction() == null ? res.getErrorMessage() : res.getDecompiledFunction().getC());
                            }
                        }
                    }
                }
            }
        }
        dec.dispose();
    }
}
