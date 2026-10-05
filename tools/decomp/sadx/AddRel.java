/* Adds a relocated rel image (see buildRelImage.py) to the open program: one block per section from rel/<name>.layout, r2/r13 set like the DOL's,
 * and a function at every "entry" line. Script argument: the module name. */
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.mem.MemoryBlock;
import java.io.ByteArrayInputStream;
import java.math.BigInteger;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public class AddRel extends GhidraScript
{
	@Override
	public void run() throws Exception
	{
		String name = getScriptArgs()[0];
		String dir = "/home/lagamyrose/Github/SonicFantasy/sadxDecomp/rel/";
		byte[] img = Files.readAllBytes(Path.of(dir + name + ".img"));
		List<Address> entries = new ArrayList<>();
		long lo = Long.MAX_VALUE, hi = 0;
		for (String line : Files.readAllLines(Path.of(dir + name + ".layout")))
		{
			String[] t = line.split(" ");
			if (t[0].equals("entry"))
			{
				entries.add(toAddr(Long.decode(t[1])));
				continue;
			}
			long addr = Long.decode(t[1]), off = Long.decode(t[2]), size = Long.decode(t[3]);
			boolean exec = t[4].equals("1");
			String bn = name + "." + t[0];
			MemoryBlock b;
			if (off == 0)
				b = currentProgram.getMemory().createUninitializedBlock(bn, toAddr(addr), size, false);
			else
			{
				byte[] part = new byte[(int) size];
				System.arraycopy(img, (int) off, part, 0, (int) size);
				b = currentProgram.getMemory().createInitializedBlock(bn, toAddr(addr), new ByteArrayInputStream(part), size, monitor, false);
			}
			b.setRead(true);
			b.setWrite(!exec);
			b.setExecute(exec);
			lo = Math.min(lo, addr);
			hi = Math.max(hi, addr + size - 1);
			println(String.format("%s 0x%08X size 0x%X", bn, addr, size));
		}
		Register r2 = currentProgram.getRegister("r2");
		Register r13 = currentProgram.getRegister("r13");
		currentProgram.getProgramContext().setValue(r2, toAddr(lo), toAddr(hi), BigInteger.valueOf(0x800CC680L));
		currentProgram.getProgramContext().setValue(r13, toAddr(lo), toAddr(hi), BigInteger.valueOf(0x800CBE60L));
		int n = 0;
		for (Address a : entries)
		{
			disassemble(a);
			if (createFunction(a, null) != null) n++;
		}
		println("functions created: " + n + " of " + entries.size());
	}
}
