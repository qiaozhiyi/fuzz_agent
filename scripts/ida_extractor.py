#!/usr/bin/env python3
import sys
import json
import os
import string

# IN IDA 9.0+, idapro must be imported first to initialize the environment
try:
    import idapro
    import idautils
    import ida_bytes
    import ida_funcs
    import ida_ua
    import ida_idp
    import ida_auto
    import ida_allins
    import ida_idaapi
    import ida_lines
    import ida_name
    import ida_typeinf
    import ida_hexrays
except ImportError as e:
    import traceback
    print(json.dumps({"error": f"Import failed: {str(e)}", "traceback": traceback.format_exc()}))
    sys.exit(1)

def get_danger_score(func_ea):
    """Calculate a danger score based on risky API calls within the function."""
    risky_apis = {
        "strcpy": 5, "strcat": 5, "gets": 10, "sprintf": 4,
        "malloc": 3, "free": 3, "memcpy": 3, "memmove": 3,
        "printf": 2, "system": 10, "popen": 10
    }
    score = 0
    calls = []

    f = ida_funcs.get_func(func_ea)
    if not f: return 0, []

    for head_ea in idautils.Heads(func_ea, f.end_ea):
        insn = ida_ua.insn_t()
        if ida_ua.decode_insn(insn, head_ea):
            if insn.get_canon_feature() & ida_idp.CF_CALL:
                for xref in idautils.CodeRefsFrom(head_ea, False):
                    name = ida_name.get_name(xref)
                    if name:
                        for api, weight in risky_apis.items():
                            if api in name:
                                score += weight
                                calls.append(name)
    return score, list(set(calls))

def get_struct_definitions():
    """Extract C-style struct definitions using the new IDA 9.0 ida_typeinf API."""
    structs = {}
    til = ida_typeinf.get_idati()
    if not til:
        return {}

    # In IDA 9.0, ntf_flags=0 is used for all named types
    name = ida_typeinf.first_named_type(til, 0)
    while name:
        tif = ida_typeinf.tinfo_t()
        if tif.get_named_type(til, name):
            if tif.is_struct():
                members = []
                for udm in tif.iter_udt():
                    m_type = udm.type
                    members.append({
                        "name": udm.name,
                        "offset": udm.offset // 8,
                        "size": m_type.get_size()
                    })
                structs[name] = members
        name = ida_typeinf.next_named_type(til, name, 0)
    return structs

def decompile_high_risk_functions(functions):
    """Decompile top-N risky functions to provide source-level context to Agent."""
    if not ida_hexrays.init_hexrays_plugin():
        return {}

    decompiled_code = {}
    for func_meta in functions[:5]: # Only top 5 most dangerous
        ea = int(func_meta["addr"], 16)
        try:
            cfunc = ida_hexrays.decompile(ea)
            if cfunc:
                decompiled_code[func_meta["name"]] = str(cfunc)
        except:
            continue
    return decompiled_code

def extract_branch_constraints():
    """Scan for conditional branches and extract their comparison logic (x86 and ARM64)."""
    constraints = []
    for func_ea in idautils.Functions():
        f = ida_funcs.get_func(func_ea)
        if not f: continue

        for head_ea in idautils.Heads(func_ea, f.end_ea):
            mnem = ida_ua.ua_mnem(head_ea).lower()
            # Recognize conditional branches for x86 (jcc) and ARM64 (b.xx, cbz, tbz)
            is_jcc = mnem.startswith('j') and len(mnem) > 1
            is_arm_b = mnem.startswith('b.') or mnem in ['cbz', 'cbnz', 'tbz', 'tbnz']

            if is_jcc or is_arm_b:
                # Find preceding comparison (CMP, SUBS, TEST)
                prev_ea = ida_bytes.prev_head(head_ea, 0)
                # Look back up to 3 instructions for ARM64 as SUBS/CMP might be further up
                for _ in range(3):
                    if prev_ea == ida_idaapi.BADADDR: break
                    prev_mnem = ida_ua.ua_mnem(prev_ea).lower()
                    if any(x in prev_mnem for x in ['cmp', 'test', 'subs', 'adds']):
                        constraints.append({
                            "addr": hex(head_ea),
                            "condition": mnem,
                            "comparison": prev_mnem,
                            "op1": ida_lines.tag_remove(ida_ua.print_operand(prev_ea, 0)),
                            "op2": ida_lines.tag_remove(ida_ua.print_operand(prev_ea, 1))
                        })
                        break
                    prev_ea = ida_bytes.prev_head(prev_ea, 0)
    return constraints[:150]

def extract_comprehensive_context(binary_path, output_json_path):
    # Initialize IDA engine
    if idapro.open_database(binary_path, True) != 0:
        result = {"error": f"Failed to open database for {binary_path}"}
        with open(output_json_path, "w") as f:
            json.dump(result, f)
        sys.exit(1)

    ida_auto.auto_wait()

    knowledge_base = {
        "functions": [],
        "magic_tokens": set(),
        "constants": set(),
        "structs": get_struct_definitions(),
        "decompiled_logic": {},
        "branch_constraints": extract_branch_constraints()
    }

    # 1. Function Analysis
    for func_ea in idautils.Functions():
        f = ida_funcs.get_func(func_ea)
        name = ida_name.get_name(func_ea)
        if name.startswith("sub_") or name.startswith("_"):
            if not any(keyword in name for keyword in ["main", "parse", "handle", "fuzz"]):
                continue

        score, risky_calls = get_danger_score(func_ea)
        knowledge_base["functions"].append({
            "name": name,
            "addr": hex(func_ea),
            "danger_score": score,
            "risky_calls": risky_calls,
            "size": f.end_ea - f.start_ea
        })

    # 2. String & Constant Extraction
    for s in idautils.Strings():
        text = str(s)
        if 3 <= len(text) <= 32:
            knowledge_base["magic_tokens"].add(text)

    for func_ea in idautils.Functions():
        f = ida_funcs.get_func(func_ea)
        for head_ea in idautils.Heads(func_ea, f.end_ea):
            insn = ida_ua.insn_t()
            if ida_ua.decode_insn(insn, head_ea):
                for op in insn.ops:
                    if op.type == ida_ua.o_imm:
                        val = op.value
                        knowledge_base["constants"].add(val)
                        if 0x20202020 <= val <= 0x7E7E7E7E:
                            for order in ['little', 'big']:
                                try:
                                    b = val.to_bytes(4, byteorder=order)
                                    if all(32 <= c <= 126 for c in b):
                                        knowledge_base["magic_tokens"].add(b.decode('ascii'))
                                except: pass

    # Sort and pick top risky functions for decompilation
    sorted_funcs = sorted(knowledge_base["functions"], key=lambda x: x["danger_score"], reverse=True)
    knowledge_base["decompiled_logic"] = decompile_high_risk_functions(sorted_funcs)

    # Final Serialization
    result = {
        "functions": sorted_funcs[:20],
        "magic_tokens": sorted(list(knowledge_base["magic_tokens"]))[:150],
        "cmp_constants": sorted(list(knowledge_base["constants"]))[:100],
        "structs": knowledge_base["structs"],
        "decompiled_logic": knowledge_base["decompiled_logic"],
        "branch_constraints": knowledge_base["branch_constraints"]
    }

    idapro.close_database()

    with open(output_json_path, "w") as f:
        json.dump(result, f, indent=2)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(1)
    extract_comprehensive_context(sys.argv[1], sys.argv[2])
