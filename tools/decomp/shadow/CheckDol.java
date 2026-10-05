/* Prints what Ghidra shows at addresses derived independently from the DOL file offsets of known strings, and the instructions at the entry. */
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;

public class CheckDol extends GhidraScript
{
	@Override
	public void run() throws Exception
	{
		for (MemoryBlock b : currentProgram.getMemory().getBlocks())
			println(String.format("%-8s %s..%s r%s w%s x%s init=%s", b.getName(), b.getStart(), b.getEnd(), b.isRead() ? "+" : "-", b.isWrite() ? "+" : "-", b.isExecute() ? "+" : "-", b.isInitialized()));
		long[] addrs = { 0x804B7A80L, 0x804BC9A4L, 0x8051C400L };
		for (long a : addrs)
		{
			Address at = toAddr(a);
			StringBuilder s = new StringBuilder();
			for (int i = 0; i < 40; i++)
			{
				byte c = currentProgram.getMemory().getByte(at.add(i));
				if (c == 0) break;
				s.append(c >= 32 && c < 127 ? (char) c : '.');
			}
			println(String.format("0x%08X: \"%s\"", a, s));
		}
		Instruction in = getInstructionAt(toAddr(0x80003154L));
		for (int i = 0; i < 6 && in != null; i++, in = in.getNext()) println(in.getAddress() + "  " + in);
		println("r13 at entry: " + currentProgram.getProgramContext().getValue(currentProgram.getRegister("r13"), toAddr(0x80003154L), false));
	}
}
