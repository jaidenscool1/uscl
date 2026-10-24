// args: <out file> <hex start> <hex end>...  Writes "<hex entry> <name>" for every function in the ranges
// (input for a uscl-rpcs3 logpoint list).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;

public class FuncList extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        AddressSet set = new AddressSet();
        for (int i = 1; i + 1 < a.length; i += 2) {
            set.add(toAddr(Long.parseLong(a[i], 16)), toAddr(Long.parseLong(a[i + 1], 16) - 1));
        }
        try (PrintWriter w = new PrintWriter(a[0])) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(set, true)) {
                w.println(String.format("%08x %s", f.getEntryPoint().getOffset(), f.getName()));
            }
        }
    }
}
