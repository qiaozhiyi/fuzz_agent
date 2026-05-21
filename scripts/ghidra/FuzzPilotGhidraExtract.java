// Ghidra headless post-script for FuzzPilot static-analysis context extraction.
// Usage:
//   analyzeHeadless /tmp/ghidra_projects fuzzpilot_tmp \
//     -import target_binary \
//     -scriptPath scripts/ghidra \
//     -postScript FuzzPilotGhidraExtract.java /tmp/static_context.json \
//     -deleteProject

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Symbol;

import java.io.FileWriter;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

public class FuzzPilotGhidraExtract extends GhidraScript {
    private static final int MAX_FUNCTIONS = 20;
    private static final int MAX_TOKENS = 150;
    private static final int MAX_CONSTANTS = 100;
    private static final int MAX_BRANCH_CONSTRAINTS = 150;
    private static final int MAX_DECOMPILED = 5;

    private static class FunctionMeta implements Comparable<FunctionMeta> {
        String name;
        String addr;
        long size;
        int dangerScore;
        Set<String> riskyCalls = new LinkedHashSet<>();
        String decompiled = "";

        @Override
        public int compareTo(FunctionMeta other) {
            int byScore = Integer.compare(other.dangerScore, dangerScore);
            if (byScore != 0) {
                return byScore;
            }
            return name.compareTo(other.name);
        }
    }

    private static class BranchConstraint {
        String addr;
        String condition;
        String comparison;
        String op1;
        String op2;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("missing output JSON path");
            return;
        }

        Listing listing = currentProgram.getListing();
        Set<String> magicTokens = new LinkedHashSet<>();
        Set<String> constants = new LinkedHashSet<>();
        List<BranchConstraint> branchConstraints = new ArrayList<>();
        List<FunctionMeta> functions = collectFunctions(listing, constants, branchConstraints);
        collectDefinedStrings(listing, magicTokens);
        collectMagicTokensFromConstants(constants, magicTokens);
        decompileTopFunctions(functions);

        Collections.sort(functions);
        String json = buildJson(functions, magicTokens, constants, branchConstraints);
        try (FileWriter writer = new FileWriter(args[0])) {
            writer.write(json);
        }
    }

    private List<FunctionMeta> collectFunctions(Listing listing,
                                                Set<String> constants,
                                                List<BranchConstraint> branchConstraints) {
        List<FunctionMeta> functions = new ArrayList<>();
        FunctionIterator iterator = currentProgram.getFunctionManager().getFunctions(true);
        while (iterator.hasNext() && !monitor.isCancelled()) {
            Function function = iterator.next();
            FunctionMeta meta = new FunctionMeta();
            meta.name = function.getName();
            meta.addr = function.getEntryPoint().toString();
            meta.size = function.getBody().getNumAddresses();

            InstructionIterator instructions = listing.getInstructions(function.getBody(), true);
            while (instructions.hasNext() && !monitor.isCancelled()) {
                Instruction instruction = instructions.next();
                collectInstructionConstants(instruction, constants);
                collectRiskyCalls(instruction, meta);
                maybeCollectBranchConstraint(listing, instruction, branchConstraints);
            }
            functions.add(meta);
        }
        return functions;
    }

    private void collectRiskyCalls(Instruction instruction, FunctionMeta meta) {
        if (!instruction.getFlowType().isCall()) {
            return;
        }
        for (Address target : instruction.getFlows()) {
            String name = "";
            Function targetFunction = getFunctionAt(target);
            if (targetFunction != null) {
                name = targetFunction.getName();
            } else {
                Symbol symbol = getSymbolAt(target);
                if (symbol != null) {
                    name = symbol.getName();
                }
            }
            if (name.isEmpty()) {
                continue;
            }
            int score = riskScore(name);
            if (score > 0) {
                meta.dangerScore += score;
                meta.riskyCalls.add(name);
            }
        }
    }

    private int riskScore(String symbolName) {
        String name = symbolName.toLowerCase(Locale.ROOT);
        if (name.contains("gets") || name.contains("system") || name.contains("popen")) {
            return 10;
        }
        if (name.contains("strcpy") || name.contains("strcat")) {
            return 5;
        }
        if (name.contains("sprintf")) {
            return 4;
        }
        if (name.contains("malloc") || name.contains("free") ||
            name.contains("memcpy") || name.contains("memmove")) {
            return 3;
        }
        if (name.contains("printf")) {
            return 2;
        }
        return 0;
    }

    private void collectInstructionConstants(Instruction instruction, Set<String> constants) {
        for (int i = 0; i < instruction.getNumOperands(); ++i) {
            Object[] operands = instruction.getOpObjects(i);
            for (Object operand : operands) {
                if (operand instanceof Scalar) {
                    Scalar scalar = (Scalar) operand;
                    constants.add(Long.toUnsignedString(scalar.getUnsignedValue()));
                }
            }
        }
    }

    private void maybeCollectBranchConstraint(Listing listing,
                                              Instruction instruction,
                                              List<BranchConstraint> out) {
        if (out.size() >= MAX_BRANCH_CONSTRAINTS) {
            return;
        }
        String mnemonic = instruction.getMnemonicString().toLowerCase(Locale.ROOT);
        if (!isConditionalBranch(mnemonic)) {
            return;
        }

        Instruction previous = instruction;
        for (int i = 0; i < 4; ++i) {
            previous = listing.getInstructionBefore(previous.getAddress());
            if (previous == null) {
                return;
            }
            String cmpMnemonic = previous.getMnemonicString().toLowerCase(Locale.ROOT);
            if (cmpMnemonic.contains("cmp") || cmpMnemonic.contains("test") ||
                cmpMnemonic.contains("subs") || cmpMnemonic.contains("adds")) {
                BranchConstraint constraint = new BranchConstraint();
                constraint.addr = instruction.getAddress().toString();
                constraint.condition = mnemonic;
                constraint.comparison = cmpMnemonic;
                constraint.op1 = operand(previous, 0);
                constraint.op2 = operand(previous, 1);
                out.add(constraint);
                return;
            }
        }
    }

    private boolean isConditionalBranch(String mnemonic) {
        if (mnemonic.startsWith("j") && !mnemonic.equals("jmp")) {
            return true;
        }
        return mnemonic.startsWith("b.") || mnemonic.equals("cbz") || mnemonic.equals("cbnz") ||
               mnemonic.equals("tbz") || mnemonic.equals("tbnz");
    }

    private String operand(Instruction instruction, int index) {
        if (index >= instruction.getNumOperands()) {
            return "";
        }
        return instruction.getDefaultOperandRepresentation(index);
    }

    private void collectDefinedStrings(Listing listing, Set<String> magicTokens) {
        DataIterator dataIterator = listing.getDefinedData(true);
        while (dataIterator.hasNext() && magicTokens.size() < MAX_TOKENS && !monitor.isCancelled()) {
            Data data = dataIterator.next();
            Object value = data.getValue();
            if (value instanceof String) {
                keepToken((String) value, magicTokens);
            }
        }
    }

    private void collectMagicTokensFromConstants(Set<String> constants, Set<String> magicTokens) {
        for (String value : constants) {
            if (magicTokens.size() >= MAX_TOKENS) {
                break;
            }
            try {
                long parsed = Long.parseUnsignedLong(value);
                if (parsed < 0x20202020L || parsed > 0x7e7e7e7eL) {
                    continue;
                }
                byte[] little = new byte[] {
                    (byte) (parsed & 0xff),
                    (byte) ((parsed >> 8) & 0xff),
                    (byte) ((parsed >> 16) & 0xff),
                    (byte) ((parsed >> 24) & 0xff),
                };
                byte[] big = new byte[] {
                    (byte) ((parsed >> 24) & 0xff),
                    (byte) ((parsed >> 16) & 0xff),
                    (byte) ((parsed >> 8) & 0xff),
                    (byte) (parsed & 0xff),
                };
                keepToken(bytesToAscii(little), magicTokens);
                keepToken(bytesToAscii(big), magicTokens);
            } catch (NumberFormatException ignored) {
            }
        }
    }

    private String bytesToAscii(byte[] bytes) {
        StringBuilder builder = new StringBuilder();
        for (byte b : bytes) {
            int value = b & 0xff;
            if (value < 0x20 || value > 0x7e) {
                return "";
            }
            builder.append((char) value);
        }
        return builder.toString();
    }

    private void keepToken(String token, Set<String> magicTokens) {
        if (token == null) {
            return;
        }
        String trimmed = token.trim();
        if (trimmed.length() < 3 || trimmed.length() > 32) {
            return;
        }
        if (looksLikeRuntimeNoise(trimmed)) {
            return;
        }
        for (int i = 0; i < trimmed.length(); ++i) {
            char c = trimmed.charAt(i);
            if (c < 0x20 || c > 0x7e) {
                return;
            }
        }
        magicTokens.add(trimmed);
    }

    private boolean looksLikeRuntimeNoise(String token) {
        String lower = token.toLowerCase(Locale.ROOT);
        if (token.startsWith("_")) {
            return true;
        }
        if (token.indexOf('/') >= 0 || token.indexOf('\\') >= 0) {
            return true;
        }
        String[] blocked = new String[] {
            "afl", "cmplog", "forkserver", "shmat", "shmem", "map_size",
            "debug:", "fs_error", "libsystem", "dyld", "asan", "ubsan",
            "sanitizer", "waitpid"
        };
        for (String item : blocked) {
            if (lower.contains(item)) {
                return true;
            }
        }
        return false;
    }

    private void decompileTopFunctions(List<FunctionMeta> functions) {
        Collections.sort(functions);
        DecompInterface decompiler = new DecompInterface();
        try {
            decompiler.openProgram(currentProgram);
            int count = 0;
            for (FunctionMeta meta : functions) {
                if (count >= MAX_DECOMPILED || monitor.isCancelled()) {
                    break;
                }
                Function function = getFunctionAt(toAddr(meta.addr));
                if (function == null) {
                    continue;
                }
                DecompileResults results = decompiler.decompileFunction(function, 20, monitor);
                if (results != null && results.decompileCompleted() &&
                    results.getDecompiledFunction() != null) {
                    meta.decompiled = results.getDecompiledFunction().getC();
                    ++count;
                }
            }
        } finally {
            decompiler.dispose();
        }
    }

    private String buildJson(List<FunctionMeta> functions,
                             Set<String> magicTokens,
                             Set<String> constants,
                             List<BranchConstraint> branchConstraints) {
        StringBuilder out = new StringBuilder();
        out.append("{");
        out.append("\"backend\":\"ghidra\",");
        out.append("\"program\":\"").append(json(currentProgram.getName())).append("\",");
        out.append("\"language\":\"").append(json(currentProgram.getLanguageID().getIdAsString())).append("\",");
        out.append("\"functions\":[");
        for (int i = 0; i < Math.min(MAX_FUNCTIONS, functions.size()); ++i) {
            if (i != 0) {
                out.append(",");
            }
            FunctionMeta meta = functions.get(i);
            out.append("{\"name\":\"").append(json(meta.name)).append("\",");
            out.append("\"addr\":\"").append(json(meta.addr)).append("\",");
            out.append("\"danger_score\":").append(meta.dangerScore).append(",");
            out.append("\"risky_calls\":").append(stringArray(meta.riskyCalls, 32)).append(",");
            out.append("\"size\":").append(meta.size).append("}");
        }
        out.append("],");
        out.append("\"magic_tokens\":").append(stringArray(magicTokens, MAX_TOKENS)).append(",");
        out.append("\"cmp_constants\":").append(stringArray(constants, MAX_CONSTANTS)).append(",");
        out.append("\"structs\":{},");
        out.append("\"decompiled_logic\":{");
        boolean first = true;
        for (FunctionMeta meta : functions) {
            if (meta.decompiled == null || meta.decompiled.isEmpty()) {
                continue;
            }
            if (!first) {
                out.append(",");
            }
            first = false;
            out.append("\"").append(json(meta.name)).append("\":\"")
               .append(json(limit(meta.decompiled, 12000))).append("\"");
        }
        out.append("},");
        out.append("\"branch_constraints\":[");
        for (int i = 0; i < branchConstraints.size(); ++i) {
            if (i != 0) {
                out.append(",");
            }
            BranchConstraint constraint = branchConstraints.get(i);
            out.append("{\"addr\":\"").append(json(constraint.addr)).append("\",");
            out.append("\"condition\":\"").append(json(constraint.condition)).append("\",");
            out.append("\"comparison\":\"").append(json(constraint.comparison)).append("\",");
            out.append("\"op1\":\"").append(json(constraint.op1)).append("\",");
            out.append("\"op2\":\"").append(json(constraint.op2)).append("\"}");
        }
        out.append("]");
        out.append("}\n");
        return out.toString();
    }

    private String stringArray(Set<String> values, int limit) {
        List<String> sorted = new ArrayList<>(values);
        Collections.sort(sorted);
        StringBuilder out = new StringBuilder("[");
        for (int i = 0; i < Math.min(limit, sorted.size()); ++i) {
            if (i != 0) {
                out.append(",");
            }
            out.append("\"").append(json(sorted.get(i))).append("\"");
        }
        out.append("]");
        return out.toString();
    }

    private String json(String value) {
        if (value == null) {
            return "";
        }
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < value.length(); ++i) {
            char c = value.charAt(i);
            switch (c) {
                case '\\':
                    out.append("\\\\");
                    break;
                case '"':
                    out.append("\\\"");
                    break;
                case '\n':
                    out.append("\\n");
                    break;
                case '\r':
                    out.append("\\r");
                    break;
                case '\t':
                    out.append("\\t");
                    break;
                default:
                    if (c < 0x20) {
                        out.append(String.format("\\u%04x", (int) c));
                    } else {
                        out.append(c);
                    }
            }
        }
        return out.toString();
    }

    private String limit(String value, int max) {
        if (value == null || value.length() <= max) {
            return value;
        }
        return value.substring(0, max);
    }
}
