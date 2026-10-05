/* Rebuilds a raw-imported GameCube main.dol (one block holding the whole file at address 0) as the DOL's own sections.
 * DOL header (big-endian): 18 section file offsets at 0x00 (7 text, then 11 data), load addresses at 0x48, sizes at 0x90, BSS address 0xD8,
 * BSS size 0xDC, entry point 0xE0. Registers r2 / r13 (small data bases) were read from the startup code (__init_registers, 0x800054F0..0x800054F8). */
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import java.io.ByteArrayInputStream;
import java.math.BigInteger;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;

public class MapDol extends GhidraScript
{
	static final long R2 = 0x800CC680L;
	static final long R13 = 0x800CBE60L;

	@Override
	public void run() throws Exception
	{
		Memory mem = currentProgram.getMemory();
		MemoryBlock raw = mem.getBlocks()[0];
		byte[] file = new byte[(int) raw.getSize()];
		raw.getBytes(raw.getStart(), file);
		ByteBuffer h = ByteBuffer.wrap(file).order(ByteOrder.BIG_ENDIAN);
		mem.removeBlock(raw, monitor);

		List<long[]> covered = new ArrayList<>();
		for (int i = 0; i < 18; i++)
		{
			long off = h.getInt(i * 4) & 0xFFFFFFFFL;
			long addr = h.getInt(0x48 + i * 4) & 0xFFFFFFFFL;
			long size = h.getInt(0x90 + i * 4) & 0xFFFFFFFFL;
			if (size == 0) continue;
			boolean text = i < 7;
			String name = text ? ".text" + i : ".data" + (i - 7);
			byte[] part = new byte[(int) size];
			System.arraycopy(file, (int) off, part, 0, (int) size);
			MemoryBlock b = mem.createInitializedBlock(name, toAddr(addr), new ByteArrayInputStream(part), size, monitor, false);
			b.setRead(true);
			b.setWrite(!text);
			b.setExecute(text);
			covered.add(new long[] { addr, addr + size });
			println(String.format("%s file 0x%X -> 0x%08X size 0x%X", name, off, addr, size));
		}

		/* The DOL's BSS range overlaps the small data sections, so only the gaps between sections get a zero block. */
		long bssStart = h.getInt(0xD8) & 0xFFFFFFFFL;
		long bssEnd = bssStart + (h.getInt(0xDC) & 0xFFFFFFFFL);
		covered.sort((a, b) -> Long.compare(a[0], b[0]));
		long cursor = bssStart;
		int n = 0;
		for (long[] c : covered)
		{
			if (c[1] <= cursor || c[0] >= bssEnd) continue;
			if (c[0] > cursor)
			{
				makeBss(mem, n++, cursor, c[0]);
			}
			cursor = Math.max(cursor, c[1]);
		}
		if (cursor < bssEnd) makeBss(mem, n++, cursor, bssEnd);

		Register r2 = currentProgram.getRegister("r2");
		Register r13 = currentProgram.getRegister("r13");
		Address lo = toAddr(0x80003100L);
		Address hi = toAddr(0x800C4680L + 0xAC0L - 1);
		currentProgram.getProgramContext().setValue(r2, lo, hi, BigInteger.valueOf(R2));
		currentProgram.getProgramContext().setValue(r13, lo, hi, BigInteger.valueOf(R13));

		long entry = h.getInt(0xE0) & 0xFFFFFFFFL;
		Address entryAddr = toAddr(entry);
		currentProgram.getSymbolTable().addExternalEntryPoint(entryAddr);
		createLabel(entryAddr, "entry", true);
		disassemble(entryAddr);
		createFunction(entryAddr, "entry");
		println(String.format("entry 0x%08X, r2 0x%08X, r13 0x%08X", entry, R2, R13));
	}

	private void makeBss(Memory mem, int n, long start, long end) throws Exception
	{
		MemoryBlock b = mem.createUninitializedBlock(".bss" + n, toAddr(start), end - start, false);
		b.setRead(true);
		b.setWrite(true);
		println(String.format(".bss%d 0x%08X..0x%08X", n, start, end));
	}
}
