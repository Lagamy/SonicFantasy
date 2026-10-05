/* Prints references to the addresses given as script arguments (hex), following up to 3 levels of data references back to code. */
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import java.util.*;

public class Xrefs extends GhidraScript
{
	@Override
	public void run() throws Exception
	{
		for (String arg : getScriptArgs())
		{
			Address a = toAddr(Long.parseLong(arg, 16));
			println("== " + a);
			walk(a, 0, new HashSet<>());
			byte[] pat = new byte[] { (byte) (a.getOffset() >> 24), (byte) (a.getOffset() >> 16), (byte) (a.getOffset() >> 8), (byte) a.getOffset() };
			Address hit = currentProgram.getMemory().findBytes(currentProgram.getMinAddress(), pat, null, true, monitor);
			while (hit != null)
			{
				Function f = getFunctionContaining(hit);
				println("  word " + hit + (f != null ? " in " + f.getName() : " (data)"));
				walk(hit, 1, new HashSet<>());
				Address next = hit.add(1);
				hit = currentProgram.getMemory().findBytes(next, pat, null, true, monitor);
			}
		}
	}

	private void walk(Address a, int depth, Set<Address> seen)
	{
		if (depth > 3 || !seen.add(a)) return;
		for (Reference r : getReferencesTo(a))
		{
			Address from = r.getFromAddress();
			Function f = getFunctionContaining(from);
			println("  ".repeat(depth + 1) + from + " -> " + a + (f != null ? "  in " + f.getName() : "  (data)"));
			if (f == null) walk(from, depth + 1, seen);
		}
	}
}
