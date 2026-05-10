#!/usr/bin/env python3
import sys
import json
import os

try:
    import idapro
    import ida_bytes
    import ida_funcs
    import idautils
    import ida_ua
    import ida_idp
    import ida_allins
except ImportError:
    print(json.dumps({"error": "IDA Pro Python SDK (idalib) not found in environment."}))
    sys.exit(1)

def extract_context(binary_path, output_json_path):
    # Initialize IDA engine and load binary silently
    if idapro.open_database(binary_path, True) != 0:
        result = {"error": f"Failed to open database for {binary_path}"}
        with open(output_json_path, "w") as f:
            json.dump(result, f)
        sys.exit(1)

    extracted_strings = []
    extracted_cmps = set()

    # 1. Extract Strings
    for s in idautils.Strings():
        # Only keep reasonable ASCII strings
        text = str(s)
        if len(text) >= 4 and len(text) < 32:
            extracted_strings.append(text)

    import string
    printable_chars = set(string.printable.encode('ascii'))

    # 2. Extract Immediate Constants (ARM64 / x86_64)
    for func_ea in idautils.Functions():
        for head_ea in idautils.Heads(func_ea, ida_funcs.get_func(func_ea).end_ea):
            insn = ida_ua.insn_t()
            if ida_ua.decode_insn(insn, head_ea):
                for op in insn.ops:
                    if op.type == ida_ua.o_imm:
                        val = op.value
                        extracted_cmps.add(val)
                        
                        # Check if this integer could be a packed string (e.g., optimized strncmp)
                        if val > 0xFFFFFF: # at least 4 bytes
                            try:
                                b = val.to_bytes((val.bit_length() + 7) // 8, byteorder='little')
                                if all(c in printable_chars for c in b):
                                    extracted_strings.append(b.decode('ascii'))
                            except:
                                pass
                            try:
                                b = val.to_bytes((val.bit_length() + 7) // 8, byteorder='big')
                                if all(c in printable_chars for c in b):
                                    extracted_strings.append(b.decode('ascii'))
                            except:
                                pass

    idapro.close_database()

    result = {
        "extracted_strings": list(set(extracted_strings))[:100],  # Limit to avoid token bloat
        "extracted_cmp_consts": list(extracted_cmps)[:50]
    }

    with open(output_json_path, "w") as f:
        json.dump(result, f, indent=2)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 ida_extractor.py <target_binary> <output_json>")
        sys.exit(1)
        
    binary = sys.argv[1]
    out_json = sys.argv[2]
    
    if not os.path.exists(binary):
        print(json.dumps({"error": f"Target binary not found: {binary}"}))
        sys.exit(1)
        
    extract_context(binary, out_json)
