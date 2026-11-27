#!/usr/bin/env python3
"""Offline static validator for the X6886 Mali-G57 Valhall v9 driver.

Reads ONE C source file (the Mali driver) and asserts the v9 invariants fixed
after the driver audit.  No device, boot image or driver file is modified.

Exit status:
  0  every rule PASS
  1  at least one rule FAIL (regression)
  2  source missing/unreadable

Usage:
  ./tools/check-mali-v9.py                       # default driver in this tree
  ./tools/check-mali-v9.py /path/to/driver.c     # check any copy/candidate
"""
from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path

DEFAULT_SOURCE = (
    Path(__file__).resolve().parent.parent / "drivers" / "gpu" / "hakios.x6886_mali.c"
)

FUNCTION_NAMES = (
    "wait_for", "domain_on", "mfg_bus_protection_release", "pt_page", "pt_below",
    "map_page", "as0_update", "run_write_job", "first_jobs", "x6886_mali_up",
    "x6886_mali_ready", "x6886_mali_unmap", "x6886_mali_mapped", "x6886_mali_submit",
    "mali_worker", "x6886_mali_init", "x6886_mali_stop", "mali_apply_quirks",
    "mali_cores_power_on", "as0_flush", "x6886_mali_pstore_write", "mali_log",
    "gpu_is_g57", "x6886_mali_opp_check",
)

GATE_BASES = ("topckgen", "infracfg", "spm", "mfgcfg")


def norm(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def mask_source(src: str) -> str:
    """Blank comments and string/char literals, keeping offsets and newlines.

    Structural checks (braces, calls, register writes) run on this masked copy;
    literal checks (format strings, boot-arg names) run on the raw source.
    """
    out = list(src)
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if src[k] != "\n":
                    out[k] = " "
            i = j
        elif c == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i + 2)
            j = n if j < 0 else j
            for k in range(i, j):
                if src[k] != "\n":
                    out[k] = " "
            i = j
        elif c in ('"', "'"):
            quote = c
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == quote:
                    j += 1
                    break
                j += 1
            for k in range(i, min(j, n)):
                if src[k] != "\n":
                    out[k] = " "
            i = j
        else:
            i += 1
    return "".join(out)


def condition_spans(code: str, cond_regex: str) -> list[tuple[int, int]]:
    """Spans (open_brace, close_brace) whose 'if' condition matches.

    Nested blocks inherit the guard by containment, which is what matters for
    ``if (mali_hwctl) { for (...) { wr(spm, ...); } }``.
    """
    spans: list[tuple[int, int]] = []
    stack: list[int] = []
    flags: list[bool] = []
    for i, ch in enumerate(code):
        if ch == "{":
            prefix = code[max(0, i - 120):i]
            flags.append(bool(re.search(cond_regex + r"\s*$", prefix)))
            stack.append(i)
        elif ch == "}":
            if stack:
                opening = stack.pop()
                guarded = flags.pop()
                if guarded:
                    spans.append((opening, i))
    return spans


def in_spans(pos: int, spans: list[tuple[int, int]]) -> bool:
    return any(start < pos < end for start, end in spans)


class Ctx:
    def __init__(self, src: str):
        self.src = src
        self.code = mask_source(src)
        self.functions: dict[str, tuple[str, int, int, int]] = {}
        for name in FUNCTION_NAMES:
            m = re.search(r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", self.code, re.S)
            if not m:
                continue
            opening = m.end() - 1
            depth = 0
            for i in range(opening, len(self.code)):
                if self.code[i] == "{":
                    depth += 1
                elif self.code[i] == "}":
                    depth -= 1
                    if depth == 0:
                        self.functions[name] = (
                            self.code[opening + 1:i], m.start(), opening, i
                        )
                        break

    def body(self, name: str) -> str:
        found = self.functions.get(name)
        return found[0] if found else ""

    def nbody(self, name: str) -> str:
        return norm(self.body(name))

    def call_sites(self, name: str) -> list[int]:
        definition = self.functions.get(name)
        defpos = definition[1] if definition else -1
        return [
            m.start()
            for m in re.finditer(r"\b" + re.escape(name) + r"\s*\(", self.code)
            if m.start() != defpos
        ]


def result(rule: str, title: str, ok: bool, detail: str) -> dict:
    return {"rule": rule, "title": title, "ok": bool(ok), "detail": detail}


# ---- the v9 invariants ------------------------------------------------------

def r01(ctx: Ctx) -> dict:
    m = re.search(r"\bjob\[4\]\s*=\s*([^;]+);", ctx.src)
    expr = norm(m.group(1)) if m else ""
    ok = (
        bool(m)
        and "JOB_TYPE_WRITEVALUE << 1" in expr
        and "JOB_INDEX << 16" in expr
        and not re.search(r"\|\s*1u?\b", expr)
        and not re.search(r"Is64b|Is 64b|is_64bit", ctx.src, re.I)
    )
    return result("V9-01", "no Bifrost Is64b bit in v9 header", ok,
                  f"job[4] = {expr}" if m else "job[4] assignment missing")


def r02(ctx: Ctx) -> dict:
    define = bool(re.search(r"#define\s+PTE_VALID\s+0x403ull\b", ctx.src))
    table = bool(re.search(r"\*entry\s*=\s*phys\s*\|\s*PTE_VALID\s*;", ctx.code))
    legacy = re.findall(r"#define\s+(PTE_(?:TABLE|AF|SH_INNER))\b", ctx.code)
    bits = re.search(r"#define\s+PTE_BITS\s+([^\n]+)", ctx.code)
    ok = define and table and not legacy and bool(bits) and "PTE_VALID" in bits.group(1)
    return result("V9-02", "table descriptors use PTE_VALID 0x403 (AF)", ok,
                  f"define={define} table_write={table} legacy={legacy or 'none'}")


def r03(ctx: Ctx) -> dict:
    nx = bool(re.search(r"#define\s+PTE_EXECUTE_NEVER\s*\(\s*1ull\s*<<\s*54\s*\)", ctx.src))
    bits = re.search(r"#define\s+PTE_BITS\s+([^\n]+)", ctx.code)
    leaf = bool(re.search(r"=\s*\(phys\s*&\s*PTE_ADDRESS\)\s*\|\s*PTE_BITS\s*;", ctx.code))
    ok = nx and bool(bits) and "PTE_EXECUTE_NEVER" in bits.group(1) and leaf
    return result("V9-03", "leaf PTE carries NX (bit 54)", ok,
                  f"NX_define={nx} in_PTE_BITS={bool(bits) and 'PTE_EXECUTE_NEVER' in bits.group(1)} leaf={leaf}")


def r04(ctx: Ctx) -> dict:
    body = ctx.nbody("x6886_mali_submit")
    flush = body.find("wr(mali, js + JS_FLUSH_ID_NEXT, 0);")
    start = body.find("wr(mali, js + JS_COMMAND_NEXT, JS_COMMAND_START);")
    define = bool(re.search(r"#define\s+JS_FLUSH_ID_NEXT\s+0x70u\b", ctx.src))
    ok = flush >= 0 and start >= 0 and flush < start and define
    return result("V9-04", "JS_FLUSH_ID_NEXT written before START", ok,
                  f"flush_index={flush} start_index={start} define_0x70={define}")


def r05(ctx: Ctx) -> dict:
    body = ctx.nbody("x6886_mali_submit")
    busy = re.search(
        r"if\s*\(\s*rd\(mali,\s*js\s*\+\s*JS_COMMAND_NEXT\)\s*!=\s*0"
        r"\s*\|\|\s*rd\(mali,\s*js\s*\+\s*JS_COMMAND\)\s*!=\s*0\s*\)",
        body,
    )
    start = body.find("wr(mali, js + JS_COMMAND_NEXT, JS_COMMAND_START);")
    ok = bool(busy) and start >= 0 and busy.end() < start and "EBUSY" in body[busy.end():start]
    detail = f"precheck={'yes' if busy else 'no'} EBUSY_return={'yes' if busy and 'EBUSY' in body[busy.end():start] else 'no'} before_START={'yes' if start >= 0 and busy and busy.end() < start else 'no'}"
    return result("V9-05", "busy precheck returns EBUSY before START", ok, detail)


def r06(ctx: Ctx) -> dict:
    body = ctx.nbody("x6886_mali_submit")
    reset = body.find("GPU_CMD_SOFT_RESET")
    flush = body.find("as0_flush(0, GPU_ADDRESS_END);")
    update = body.find("as0_update(pt_top_phys, TRANSCFG_AARCH64_4K, MEMATTR_G57);")
    faults = "GPU_FAULT_STATUS" in body and "GPU_FAULT_ADDRESS" in body
    ok = reset >= 0 and flush > reset and update > flush and faults
    return result("V9-06", "fault recovery flushes AS0 before UPDATE", ok,
                  f"reset={reset} flush={flush} update={update} fault_log={faults}")


def r07(ctx: Ctx) -> dict:
    flush_body = ctx.nbody("as0_flush")
    lock = flush_body.find("wr(mali, AS0 + AS_COMMAND, AS_COMMAND_LOCK);")
    flush_mem = flush_body.find("wr(mali, AS0 + AS_COMMAND, AS_COMMAND_FLUSH_MEM);")
    define = bool(re.search(r"#define\s+AS_COMMAND_FLUSH_MEM\s+5u\b", ctx.src))

    def ordered(name: str) -> bool:
        b = ctx.nbody(name)
        f, u = b.find("as0_flush("), b.find("as0_update(")
        return f >= 0 and u > f

    ok = lock >= 0 and flush_mem > lock and define and ordered("x6886_mali_unmap") and ordered("x6886_mali_mapped")
    return result("V9-07", "AS_COMMAND_LOCK then FLUSH_MEM(5) before UPDATE", ok,
                  f"lock={lock} flush_mem={flush_mem} define_5={define} unmap={ordered('x6886_mali_unmap')} mapped={ordered('x6886_mali_mapped')}")


def r08(ctx: Ctx) -> dict:
    define = bool(re.search(r"#define\s+MEMATTR_G57\s+0x00004c8d888d8f88ull\b", ctx.src))
    calls = [norm(a) for a in re.findall(r"as0_update\s*\(([^;]*?)\)\s*;", ctx.code)]
    aarch64 = [a for a in calls if "TRANSCFG_AARCH64_4K" in a]
    ok = (
        define
        and bool(aarch64)
        and all("MEMATTR_G57" in a for a in aarch64)
        and not re.search(r"mali_fastmap\s*\?", ctx.code)
    )
    return result("V9-08", "MEMATTR_G57 written unconditionally (no fastmap 0)", ok,
                  f"define={define} aarch64_calls={len(aarch64)} all_MEMATTR={all('MEMATTR_G57' in a for a in aarch64)} fastmap_ternary={'no' if not re.search(r'mali_fastmap\s*\?', ctx.code) else 'YES'}")


def r09(ctx: Ctx) -> dict:
    writes = re.findall(
        r"wr\s*\(\s*mali\s*,\s*[^;]*?JS_AFFINITY_NEXT_(?:LO|HI)[^;]*?\)\s*;", ctx.code
    )
    normed = [norm(w) for w in writes]
    ok = (
        len(normed) >= 4
        and all("rd64(mali, SHADER_READY)" in w and "SHADER_READY" in w for w in normed)
        and not any("shader_mask" in w for w in normed)
    )
    return result("V9-09", "shader affinity sourced from SHADER_READY", ok,
                  f"affinity_writes={len(normed)} all_SHADER_READY={all('SHADER_READY' in w for w in normed)} no_shader_mask={not any('shader_mask' in w for w in normed)}")


def r10(ctx: Ctx) -> dict:
    guarded = condition_spans(ctx.code, r"if\s*\(\s*mali_hwctl\s*\)")
    problems: list[str] = []
    write_count = 0
    for name, (body, _defpos, opening, _close) in ctx.functions.items():
        for m in re.finditer(r"\bwr(?:64)?\s*\(\s*(" + "|".join(GATE_BASES) + r")\s*,", body):
            write_count += 1
            absolute = opening + 1 + m.start()
            if in_spans(absolute, guarded):
                continue
            sites = ctx.call_sites(name)
            if not sites or not all(in_spans(s, guarded) for s in sites):
                problems.append(f"{name}->{m.group(1)}")
    default_off = bool(re.search(r"mali_hwctl\s*=\s*0\s*;", ctx.code))
    parsed = '"mali_hwctl"' in ctx.src and "PE_parse_boot_argn" in ctx.code
    ok = write_count > 0 and not problems and default_off and parsed
    return result("V9-10", "all unproven gate writes under mali_hwctl", ok,
                  f"gate_writes={write_count} unguarded={problems or 'none'} default0={default_off} bootarg={parsed}")


def r11(ctx: Ctx) -> dict:
    def span_body(cond: str) -> str:
        spans = condition_spans(ctx.code, cond)
        return norm(ctx.code[spans[0][0] + 1:spans[0][1]]) if spans else ""

    product = bool(re.search(r"#define\s+G57_PRODUCT\s+0x9093u\b", ctx.src))
    is_g57 = bool(re.search(r"\b\w+\s*>>\s*16\s*\)?\s*==\s*G57_PRODUCT", ctx.nbody("gpu_is_g57")))
    g57 = span_body(r"if\s*\(\s*gpu_is_g57\s*\(\s*gpu_id\s*\)\s*\)")
    exact = span_body(r"if\s*\(\s*gpu_id\s*==\s*REF_G57_GPU_ID\s*\)")
    ok = (
        product and is_g57
        and "SHADER_CONFIG" in g57 and "JM_CONFIG" in g57
        and "L2_MMU_CONFIG" in exact
    )
    return result("V9-11", "G57 quirks product-scoped, MMU quirk exact-ID", ok,
                  f"product={product} is_g57={is_g57} shader_jm_in_G57={'SHADER_CONFIG' in g57 and 'JM_CONFIG' in g57} mmu_in_exact={'L2_MMU_CONFIG' in exact}")


def r12(ctx: Ctx) -> dict:
    defines = all(
        re.search(r"#define\s+" + name + r"\s+" + value + r"\b", ctx.src)
        for name, value in (
            ("X6886_PSTORE_PHYS", r"0x480af000ULL"),
            ("X6886_PSTORE_SIG", r"0x43474244u"),
            ("X6886_PSTORE_SIZE", r"0x1000u"),
            ("X6886_PSTORE_HEAD", r"12u"),
        )
    )
    capacity = "*(volatile uint32_t *)(base + 8) = X6886_PSTORE_SIZE - X6886_PSTORE_HEAD;" in ctx.code
    header_guard = bool(re.search(
        r"if\s*\(\s*sig\s*!=\s*X6886_PSTORE_SIG\s*\|\|\s*size\s*<\s*16"
        r"\s*\|\|\s*size\s*>\s*X6886_PSTORE_SIZE\s*-\s*X6886_PSTORE_HEAD\s*\)",
        ctx.code,
    ))
    mirror = (
        'snprintf(line, sizeof(line), "[X6886-mali] ")' in ctx.src
        and 'printf("%s\\n", line);' in ctx.src
        and "x6886_mali_pstore_write(line, (unsigned)strlen(line));" in ctx.src
    )
    ok = defines and capacity and header_guard and mirror
    return result("V9-12", "pstore ring contract and [X6886-mali] mirror", ok,
                  f"defines={defines} capacity_word8={capacity} valid_header_kept={header_guard} dual_log={mirror}")


def r13(ctx: Ctx) -> dict:
    sysctls = set(re.findall(r"SYSCTL_UINT\(\s*_kern_hakios\s*,\s*OID_AUTO\s*,\s*(\w+),", ctx.code))
    expected = {"mali", "mali_id", "mali_jobs", "mali_failed", "mali_opps"}
    up = ctx.nbody("x6886_mali_up")
    stages = "case 2: reached = 6; break;" in up and "case 1: reached = 5; break;" in up
    ready = "reached >= 6 && mali != NULL" in ctx.nbody("x6886_mali_ready")
    stopped = "reached = 0;" in ctx.nbody("x6886_mali_stop")
    ok = expected.issubset(sysctls) and stages and ready and stopped
    return result("V9-13", "sysctls and reached=5/6 semantics intact", ok,
                  f"sysctls={sorted(sysctls & expected)} stages={stages} ready_requires_6={ready} stop_resets={stopped}")


def r14(ctx: Ctx) -> dict:
    rejected = "if (gpu_id == 0 || gpu_id == ~0u)" in ctx.nbody("x6886_mali_up")
    read_id = "gpu_id = rd(mali, GPU_ID);" in ctx.nbody("x6886_mali_up")
    no_csf = not re.search(r"\bcsf\b", ctx.code, re.I)
    no_fake = not re.search(r"gpu_id\s*=\s*0x[0-9a-fA-F]", ctx.code)
    jm = all(s in ctx.code for s in ("JS0", "JS1", "JS_COMMAND_START"))
    ok = rejected and read_id and no_csf and no_fake and jm
    return result("V9-14", "JM-only path, no CSF and no assumed GPU_ID", ok,
                  f"reject_0_and_inv={rejected} read_real_ID={read_id} no_CSF={no_csf} no_fake_ID={no_fake} JM={jm}")


def r15(ctx: Ctx) -> dict:
    bounded = bool(re.search(r"for\s*\(\s*uint32_t us = 0; us < ms \* 1000; us \+= 10\s*\)", ctx.code))
    worker = bool(re.search(r"while\s*\(\s*!\s*mali_worker_stop\s*\)", ctx.nbody("mali_worker")))
    sleeps = ctx.code.count("lck_mtx_sleep_deadline")
    no_infinite = not re.search(r"for\s*\(\s*;\s*;\s*\)|while\s*\(\s*1\s*\)", ctx.code)
    ok = bounded and worker and sleeps >= 3 and no_infinite
    return result("V9-15", "bounded polls and stoppable event-driven worker", ok,
                  f"bounded_wait_for={bounded} worker_stop={worker} sleep_deadline_calls={sleeps} no_infinite_loop={no_infinite}")


RULES = (r01, r02, r03, r04, r05, r06, r07, r08, r09, r10, r11, r12, r13, r14, r15)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", nargs="?", type=Path, default=DEFAULT_SOURCE,
                    help="driver source to validate (default: %(default)s)")
    args = ap.parse_args()
    path = args.source.expanduser().resolve()
    try:
        src = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        print(f"ERROR: cannot read source {path}: {exc}", file=sys.stderr)
        return 2

    ctx = Ctx(src)
    results = [rule(ctx) for rule in RULES]
    passed = sum(1 for r in results if r["ok"])

    print("X6886 Mali v9 offline static validator")
    print(f"source : {path}")
    print(f"sha256 : {hashlib.sha256(src.encode('utf-8')).hexdigest()}")
    print()
    print(f"{'RULE':<8} {'STATUS':<6} TITLE")
    print(f"{'-' * 8} {'-' * 6} {'-' * 62}")
    for r in results:
        print(f"{r['rule']:<8} {'PASS' if r['ok'] else 'FAIL':<6} {r['title']}")
    print()
    for r in results:
        detail = r["detail"] if not r["ok"] else (r["detail"][:72] + "..." if len(r["detail"]) > 72 else r["detail"])
        print(f"{r['rule']:<8} {detail}")
    print()
    if passed == len(results):
        print(f"RESULT: {passed}/{len(results)} PASS - all v9 invariants intact (offline only; boot proof still required)")
        return 0
    print(f"RESULT: {passed}/{len(results)} PASS, {len(results) - passed} FAIL - REGRESSION DETECTED")
    return 1


if __name__ == "__main__":
    sys.exit(main())
