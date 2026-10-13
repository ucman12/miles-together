// Research-only: analyze selected functions, never export game-derived code into the mod project.
// @category MilesTogether.Research

import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class CoopResearch extends GhidraScript {
    private final TreeMap<Long, Long> bounds = new TreeMap<>();
    private long base;

    private Function prepare(long rva, String name) throws Exception {
        Address start = toAddr(base + rva);
        Function existing = getFunctionAt(start);
        Long end = bounds.get(rva);
        if (existing != null) {
            if (end != null) existing.setBody(new AddressSet(start, toAddr(base + end - 1)));
            return existing;
        }
        if (end != null && end - rva > 16384) return null;
        AddressSet body = end == null ? new AddressSet(start, start.add(255)) :
            new AddressSet(start, toAddr(base + end - 1));
        new DisassembleCommand(start, body, true).applyTo(currentProgram, monitor);
        Function function = createFunction(start, name);
        if (function != null && end != null) function.setBody(body);
        return function;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("Seeds TSV and private output folder required");
        base = currentProgram.getImageBase().getOffset();
        MemoryBlock pdata = currentProgram.getMemory().getBlock(".pdata");
        if (pdata != null) {
            for (long offset = 0; offset + 12 <= pdata.getSize(); offset += 12) {
                Address address = pdata.getStart().add(offset);
                long begin = Integer.toUnsignedLong(currentProgram.getMemory().getInt(address));
                long end = Integer.toUnsignedLong(currentProgram.getMemory().getInt(address.add(4)));
                if (begin != 0 && end > begin) bounds.put(begin, end);
            }
        }
        if (args.length >= 3) {
            for (String line : Files.readAllLines(Path.of(args[2]), StandardCharsets.UTF_8)) {
                String[] fields = line.trim().split("\\s+");
                if (fields.length >= 2) bounds.put(Long.decode(fields[0]), Long.decode(fields[1]));
            }
        }
        LinkedHashMap<Long, String> seeds = new LinkedHashMap<>();
        for (String line : Files.readAllLines(Path.of(args[0]), StandardCharsets.UTF_8)) {
            String[] fields = line.trim().split("\\s+");
            if (fields.length >= 2) seeds.put(Long.decode(fields[0]), fields[1]);
        }
        int initialCount = seeds.size();
        for (Map.Entry<Long, String> seed : new ArrayList<>(seeds.entrySet())) {
            Function function = prepare(seed.getKey(), seed.getValue());
            if (function == null) continue;
            InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
            while (instructions.hasNext()) {
                Instruction instruction = instructions.next();
                if (!instruction.getFlowType().isCall()) continue;
                for (Address target : instruction.getFlows()) {
                    long rva = target.getOffset() - base;
                    if (bounds.containsKey(rva) && seeds.size() < 120) {
                        seeds.putIfAbsent(rva, "callee_" + Long.toHexString(rva));
                    }
                }
            }
        }
        for (Map.Entry<Long, String> seed : seeds.entrySet()) prepare(seed.getKey(), seed.getValue());
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        Path output = Path.of(args[1]);
        Files.createDirectories(output);
        int done = 0;
        for (Map.Entry<Long, String> seed : seeds.entrySet()) {
            monitor.checkCancelled();
            Function function = getFunctionAt(toAddr(base + seed.getKey()));
            if (function == null) continue;
            DecompileResults result = decompiler.decompileFunction(function, 30, monitor);
            String text = result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage();
            Files.writeString(output.resolve(seed.getValue() + ".c"), text, StandardCharsets.UTF_8);
            done++;
            if (done % 10 == 0) println("Bounded research: " + done + "/" + seeds.size());
        }
        decompiler.dispose();
        println("Bounded research done: " + done + " functions from " + initialCount + " seeds");
    }
}
