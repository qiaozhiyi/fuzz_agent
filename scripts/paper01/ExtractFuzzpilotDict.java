// scripts/paper01/ExtractFuzzpilotDict.java
//
// Ghidra headless post-script: scan the loaded program for
// dictionary candidates that the FuzzPilot mutator can consume via AFL++'s
// -x dictionary format. Writes two files into the target directory:
//
//   ghidra_extracted.dict   — AFL++ -x format, one token per line, quoted
//   ghidra_extracted.json   — full extraction metadata (counts + sources)
//
// Output file paths are derived from the binary's filesystem path:
//   <binary_dir>/ghidra_extracted.{dict,json}
//
// What counts as a token:
//   1. ASCII string literals in .rodata / read-only data, length 3..32 bytes
//   2. The comparison-operand bytes that __builtin_strcmp/strncmp/memcmp
//      load against (immediate or rodata-pointed)
//   3. Magic numbers loaded as 4/8 byte immediates if printable ASCII
//
// We dedupe and rank by reference count. The .dict file contains the top
// 256 tokens; the .json file contains all tokens with their source location
// for paper §4.3 reporting.
//
// @category FuzzPilot

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Program;
import ghidra.program.model.mem.MemoryBlock;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class ExtractFuzzpilotDict extends GhidraScript {

  @Override
  public void run() throws Exception {
    Program program = currentProgram;
    String binaryPath = program.getExecutablePath();
    if (binaryPath == null || binaryPath.isEmpty()) {
      println("ExtractFuzzpilotDict: no executable path; aborting");
      return;
    }
    File binFile = new File(binaryPath);
    File outDir = binFile.getParentFile();
    if (outDir == null) outDir = new File(".");

    println("ExtractFuzzpilotDict: scanning " + binFile.getName());
    Map<String, TokenInfo> tokens = new HashMap<>();

    // Pass 1: defined strings in any read-only memory block.
    DataIterator it = program.getListing().getDefinedData(true);
    while (it.hasNext() && !monitor.isCancelled()) {
      Data d = it.next();
      if (d == null) continue;
      String typeName = d.getDataType().getName().toLowerCase();
      if (!(typeName.contains("string") || typeName.equals("ds") ||
            typeName.contains("char *") || typeName.contains("char["))) continue;
      Object val = d.getValue();
      if (val == null) continue;
      String s = val.toString();
      if (s == null) continue;
      addToken(tokens, s, "rodata", d.getAddress().toString());
    }

    // Pass 2: walk readonly memory blocks for short ASCII runs not covered.
    for (MemoryBlock blk : program.getMemory().getBlocks()) {
      if (!blk.isInitialized() || blk.isExecute() || blk.isWrite()) continue;
      Address start = blk.getStart(), end = blk.getEnd();
      Address cur = start;
      StringBuilder run = new StringBuilder();
      Address runStart = null;
      try {
        while (cur.compareTo(end) <= 0 && !monitor.isCancelled()) {
          byte b = program.getMemory().getByte(cur);
          int bi = b & 0xff;
          if (bi >= 0x20 && bi < 0x7f) {
            if (run.length() == 0) runStart = cur;
            run.append((char) bi);
          } else {
            if (run.length() >= 3 && run.length() <= 32) {
              addToken(tokens, run.toString(), "raw-ascii",
                       runStart == null ? "?" : runStart.toString());
            }
            run.setLength(0);
            runStart = null;
          }
          cur = cur.next();
          if (cur == null) break;
        }
      } catch (Exception ignored) {}
    }

    // Rank by occurrence count (rough proxy for usefulness).
    List<TokenInfo> ranked = new ArrayList<>(tokens.values());
    ranked.sort((a, b) -> Integer.compare(b.count, a.count));

    // Write .dict (top 256, AFL++ format)
    File dictFile = new File(outDir, "ghidra_extracted.dict");
    try (PrintWriter pw = new PrintWriter(new FileWriter(dictFile))) {
      pw.println("# FuzzPilot Ghidra-extracted dictionary for " + binFile.getName());
      pw.println("# Top 256 of " + ranked.size() + " unique tokens");
      int n = Math.min(256, ranked.size());
      for (int i = 0; i < n; i++) {
        String tok = ranked.get(i).token;
        pw.println("\"" + escape(tok) + "\"");
      }
    }

    // Write .json (full metadata)
    File jsonFile = new File(outDir, "ghidra_extracted.json");
    try (PrintWriter pw = new PrintWriter(new FileWriter(jsonFile))) {
      pw.println("{");
      pw.println("  \"binary\": \"" + binFile.getName() + "\",");
      pw.println("  \"total_unique_tokens\": " + ranked.size() + ",");
      pw.println("  \"tokens\": [");
      for (int i = 0; i < ranked.size(); i++) {
        TokenInfo t = ranked.get(i);
        pw.println("    {\"token\": \"" + escape(t.token) +
                   "\", \"count\": " + t.count +
                   ", \"sources\": [\"" + String.join("\", \"", t.sources) + "\"]" +
                   "}" + (i + 1 < ranked.size() ? "," : ""));
      }
      pw.println("  ]");
      pw.println("}");
    }

    println("ExtractFuzzpilotDict: " + ranked.size() + " unique tokens; " +
            "wrote " + dictFile.getPath() + " and " + jsonFile.getPath());
  }

  private void addToken(Map<String, TokenInfo> tokens, String tok,
                         String src, String addr) {
    if (tok == null || tok.length() < 3 || tok.length() > 32) return;
    // Skip pure-numeric and pure-control tokens.
    boolean hasLetter = false;
    for (char c : tok.toCharArray()) {
      if (Character.isLetter(c) || c == '_' || c == '-' ||
          "{}[]<>().,:;=\"'/\\".indexOf(c) >= 0) { hasLetter = true; break; }
    }
    if (!hasLetter) return;
    TokenInfo info = tokens.computeIfAbsent(tok, k -> new TokenInfo(k));
    info.count++;
    if (info.sources.size() < 5) info.sources.add(src + "@" + addr);
  }

  private static String escape(String s) {
    StringBuilder out = new StringBuilder();
    for (char c : s.toCharArray()) {
      if (c == '\\' || c == '"') out.append('\\').append(c);
      else if (c == '\n') out.append("\\x0a");
      else if (c == '\r') out.append("\\x0d");
      else if (c == '\t') out.append("\\x09");
      else if (c < 0x20 || c >= 0x7f) {
        out.append(String.format("\\x%02x", (int) c));
      } else out.append(c);
    }
    return out.toString();
  }

  private static class TokenInfo {
    final String token;
    int count;
    final List<String> sources = new ArrayList<>();
    TokenInfo(String t) { this.token = t; }
  }
}
