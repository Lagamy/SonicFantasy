/* Writes the decompiler's C for every function of the program to decompiled/pseudocode.c (function markers: "// FUN <address> <name> <size>")
 * and a list of the functions to decompiled/functions.txt. Run after auto-analysis. */
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import java.io.BufferedWriter;
import java.io.FileWriter;

public class ExportDecomp extends GhidraScript
{
	@Override
	public void run() throws Exception
	{
		String dir = "/home/lagamyrose/Github/SonicFantasy/sadxDecomp/decompiled/";
		DecompInterface decomp = new DecompInterface();
		decomp.openProgram(currentProgram);
		int total = currentProgram.getFunctionManager().getFunctionCount();
		int done = 0, failed = 0;
		try (BufferedWriter code = new BufferedWriter(new FileWriter(dir + "pseudocode.c"));
			BufferedWriter list = new BufferedWriter(new FileWriter(dir + "functions.txt")))
		{
			FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
			while (it.hasNext() && !monitor.isCancelled())
			{
				Function f = it.next();
				long size = f.getBody().getNumAddresses();
				list.write(f.getEntryPoint() + " " + f.getName() + " " + size + "\n");
				DecompileResults r = decomp.decompileFunction(f, 30, monitor);
				code.write("// FUN " + f.getEntryPoint() + " " + f.getName() + " " + size + "\n");
				if (r.decompileCompleted() && r.getDecompiledFunction() != null)
					code.write(r.getDecompiledFunction().getC() + "\n");
				else
				{
					failed++;
					code.write("// decompile failed: " + r.getErrorMessage() + "\n\n");
				}
				if (++done % 1000 == 0) println("decompiled " + done + " / " + total + " (" + failed + " failed)");
			}
		}
		println("done: " + done + " functions, " + failed + " failed");
	}
}
