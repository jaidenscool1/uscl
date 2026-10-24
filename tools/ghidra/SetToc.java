// Pre-script, before auto-analysis: PS3 code addresses data through r2 (TOC); give Ghidra its value everywhere.
// arg: the TOC base, e.g. 0xd67e98 (read from the entry point's descriptor: second word).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.lang.Register;
import ghidra.program.model.mem.MemoryBlock;
import java.math.BigInteger;

public class SetToc extends GhidraScript {
    @Override
    public void run() throws Exception {
        long toc = Long.decode(getScriptArgs()[0]);
        Register r2 = currentProgram.getRegister("r2");
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            if (b.isExecute()) {
                currentProgram.getProgramContext().setValue(r2, b.getStart(), b.getEnd(), BigInteger.valueOf(toc));
                println("r2=" + Long.toHexString(toc) + " on " + b.getName());
            }
        }
    }
}
