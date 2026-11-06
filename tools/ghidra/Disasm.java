// args: <out file> <hex start> <hex end>  Disassembles (creating code where needed) and lists instructions, e.g.
// for jump-table targets the decompiler skipped.
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Instruction;
import java.io.PrintWriter;

public class Disasm extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Address s = toAddr(Long.parseLong(a[1], 16)), e = toAddr(Long.parseLong(a[2], 16));
        new DisassembleCommand(new AddressSet(s, e), null, true).applyTo(currentProgram, monitor);
        try (PrintWriter w = new PrintWriter(a[0])) {
            for (Instruction i = getInstructionAt(s); i != null && i.getAddress().compareTo(e) < 0; i = i.getNext()) {
                String ref = "";
                for (var r : i.getReferencesFrom()) ref += " ->" + r.getToAddress();
                w.println(i.getAddress() + "  " + i + ref);
            }
        }
    }
}
