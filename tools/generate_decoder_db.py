#!/usr/bin/env python3
"""
Generate myobjdump decoder database from the official riscv/riscv-opcodes repo.

from __future__ import annotations
import argparse, csv, json, re, subprocess, sys, tempfile
from pathlib import Path
from typing import Dict, List, Optional, Tuple

OFFICIAL_REPO = "https://github.com/riscv/riscv-opcodes.git"

# ── Split-immediate definitions ─────────────────────────────────────────────
# Each entry maps a logical immediate name to a list of
# (instructionMsb, instructionLsb, operandLsb) triples.

SPLIT_IMMEDIATES: Dict[str, List[Tuple[int,int,int]]] = {
    "bimm12": [(31,31,12),(30,25,5),(11,8,1),(7,7,11)],
    "jimm20": [(19,12,12),(20,20,11),(30,21,1),(31,31,20)],
    "imm12s": [(31,25,5),(11,7,0)],
    # Compressed 6-bit signed immediate (c.addi, c.addiw, c.li, c.lui, c.slli, …)
    # inst[12]   -> imm[5]  (sign bit)
    # inst[6:2]  -> imm[4:0]
    "c_nzimm6": [(12,12,5),(6,2,0)],
    "c_imm6":   [(12,12,5),(6,2,0)],
    "c_nzuimm6":[(12,12,5),(6,2,0)],
    # Compressed 9-bit branch offset (c.beqz, c.bnez)
    # inst[12]   -> imm[8]  (sign)
    # inst[11:10]-> imm[4:3]
    # inst[6:5]  -> imm[7:6]
    # inst[4:3]  -> imm[2:1]
    # inst[2]    -> imm[5]
    "c_bimm9": [(12,12,8),(6,5,6),(2,2,5),(11,10,3),(4,3,1)],
    # Compressed 12-bit jump offset (c.j, c.jal)
    # inst[12]   -> imm[11] (sign)
    # inst[11]   -> imm[4]
    # inst[10:9] -> imm[9:8]
    # inst[8]    -> imm[10]
    # inst[7]    -> imm[6]
    # inst[6]    -> imm[7]
    # inst[5:3]  -> imm[3:1]
    # inst[2]    -> imm[5]
    "c_imm12_j": [(12,12,11),(8,8,10),(10,9,8),(6,6,7),(7,7,6),(2,2,5),(11,11,4),(5,3,1)],
    # Compressed 8-bit SP-relative store offset (c.swsp, c.sdsp)
    "c_uimm8sp_s": [(12,9,2),(8,7,6)],
    "c_uimm9sp_s": [(12,10,3),(9,7,6)],
    # c.addi16sp (c_nzimm10)
    # inst[12]   -> imm[9]
    # inst[6]    -> imm[4]
    # inst[5]    -> imm[6]
    # inst[4:3]  -> imm[8:7]
    # inst[2]    -> imm[5]
    "c_nzimm10": [(12,12,9),(4,3,7),(5,5,6),(2,2,5),(6,6,4)],
    # c.lui (c_nzimm18)
    "c_nzimm18": [(12,12,17),(6,2,12)],
}

SPLIT_TOKEN_TO_LOGICAL: Dict[str,str] = {
    "bimm12hi":"bimm12","bimm12lo":"bimm12",
    "jimm20":"jimm20",
    "imm12hi":"imm12s","imm12lo":"imm12s",
    # compressed 6-bit immediates
    "c_nzimm6hi":"c_nzimm6","c_nzimm6lo":"c_nzimm6",
    "c_imm6hi":"c_imm6","c_imm6lo":"c_imm6",
    "c_nzuimm6hi":"c_nzuimm6","c_nzuimm6lo":"c_nzuimm6",
    # compressed branch (c.beqz, c.bnez)
    "c_bimm9hi":"c_bimm9","c_bimm9lo":"c_bimm9",
    # compressed jump (c.j, c.jal)
    "c_imm12":"c_imm12_j",
    # c.addi16sp
    "c_nzimm10hi":"c_nzimm10","c_nzimm10lo":"c_nzimm10",
    # c.lui
    "c_nzimm18hi":"c_nzimm18","c_nzimm18lo":"c_nzimm18",
}

# ── Built-in operand location fallback ──────────────────────────────────────
BUILTIN: Dict[str,List[Tuple[int,int]]] = {
    "rd":[(11,7)],"rs1":[(19,15)],"rs2":[(24,20)],"rs3":[(31,27)],
    "rm":[(14,12)],"csr":[(31,20)],"imm12":[(31,20)],"imm20":[(31,12)],
    "shamt":[(25,20)],"shamtd":[(25,20)],"shamtw":[(24,20)],
    "shamtq":[(26,20)],"shamtw4":[(23,20)],
    "funct7":[(31,25)],"funct3":[(14,12)],
    "aq":[(26,26)],"rl":[(25,25)],"fm":[(31,28)],
    "pred":[(27,24)],"succ":[(23,20)],
    "rs1_p":[(9,7)],"rs2_p":[(4,2)],"rd_p":[(4,2)],
    "rd_rs1_p":[(9,7)],"rd_rs1":[(11,7)],"rd_rs1_n0":[(11,7)],
    "rd_n0":[(11,7)],"rd_n2":[(11,7)],"rs1_n0":[(11,7)],
    "c_rs2":[(6,2)],"c_rs2_n0":[(6,2)],"c_rs1_n0":[(11,7)],
    "c_sreg1":[(9,7)],"c_sreg2":[(4,2)],
    "c_imm12":[(12,2)],"c_bimm9hi":[(12,10)],"c_bimm9lo":[(6,2)],
    "c_nzimm6hi":[(12,12)],"c_nzimm6lo":[(6,2)],
    "c_imm6hi":[(12,12)],"c_imm6lo":[(6,2)],
    "c_nzuimm10":[(12,5)],"c_nzimm10hi":[(12,12)],"c_nzimm10lo":[(6,2)],
    "c_nzimm18hi":[(12,12)],"c_nzimm18lo":[(6,2)],
    "c_uimm7hi":[(12,10)],"c_uimm7lo":[(6,5)],
    "c_uimm8hi":[(12,10)],"c_uimm8lo":[(6,5)],
    "c_uimm9hi":[(12,10)],"c_uimm9lo":[(6,5)],
    "c_nzuimm5":[(6,2)],"c_nzuimm6hi":[(12,12)],"c_nzuimm6lo":[(6,2)],
    "c_uimm8sphi":[(12,12)],"c_uimm8splo":[(6,2)],"c_uimm8sp_s":[(12,7)],
    "c_uimm9sphi":[(12,12)],"c_uimm9splo":[(6,2)],"c_uimm9sp_s":[(12,7)],
    "c_uimm10sphi":[(12,12)],"c_uimm10splo":[(6,2)],"c_uimm10sp_s":[(12,7)],
    "c_uimm1":[(5,5)],"c_uimm2":[(6,5)],"c_rlist":[(7,4)],"c_spimm":[(3,2)],
    "c_index":[(9,2)],"simm5":[(19,15)],"zimm5":[(19,15)],
    "zimm10":[(29,20)],"zimm11":[(30,20)],
    "zimm6hi":[(26,26)],"zimm6lo":[(19,15)],
    "bs":[(31,30)],"rnum":[(23,20)],
    "vm":[(25,25)],"wd":[(26,26)],"nf":[(31,29)],"amoop":[(31,27)],
    "vd":[(11,7)],"vs1":[(19,15)],"vs2":[(24,20)],"vs3":[(11,7)],
}

# ── Helpers ──────────────────────────────────────────────────────────────────

def run(cmd,cwd=None):
    print("+"," ".join(cmd))
    subprocess.run(cmd,cwd=cwd,check=True)

def get_repo(repo_dir):
    if repo_dir:
        p=repo_dir.resolve()
        if not (p/"extensions").is_dir():
            raise SystemExit(f"Not a riscv-opcodes repo: {p}")
        return p,None
    tmp=tempfile.TemporaryDirectory(prefix="riscv-opcodes-")
    tgt=Path(tmp.name)/"riscv-opcodes"
    run(["git","clone","--depth","1",OFFICIAL_REPO,str(tgt)])
    return tgt,tmp

def parse_int(t):
    t=t.strip()
    if t.startswith(("0x","0X")): return int(t,16)
    if t.startswith(("0b","0B")): return int(t,2)
    return int(t,10)

def parse_fixed(token):
    m=re.fullmatch(r"(\d+)(?:\.\.(\d+))?=(.+)",token)
    if not m: raise ValueError(token)
    a,b=int(m.group(1)),int(m.group(2) or m.group(1))
    return max(a,b),min(a,b),parse_int(m.group(3))

def fixed_mask_match(tokens):
    mask=match=0
    for t in tokens:
        if "=" not in t: continue
        msb,lsb,val=parse_fixed(t)
        w=msb-lsb+1
        fm=((1<<w)-1)<<lsb
        mask|=fm; match|=(val&((1<<w)-1))<<lsb
    return mask,match

def read_lines(path):
    out=[]
    with path.open("r",encoding="utf-8") as f:
        for raw in f:
            line=raw.strip()
            if not line or line.startswith("#"): continue
            out.append(line)
    return out

# FIX 2: width from extension name
def width_from_ext(stem):
    s=stem.lower()
    # rv_c and rv64_c are 16-bit compressed extensions
    if s in ("rv_c","rv64_c"): return 16
    if s.startswith("rv_zc") or s.startswith("rv64_zc"): return 16
    return 32

# FIX 1: correct CSV parsing
def load_arg_lut(repo):
    candidates=[repo/"arg_lut.csv",repo/"src"/"riscv_opcodes"/"arg_lut.csv"]
    path=next((p for p in candidates if p.exists()),None)
    if path is None:
        print("[WARNING] arg_lut.csv not found, using built-in table only")
        return {}
    locs={}
    with path.open("r",encoding="utf-8",newline="") as f:
        for row in csv.reader(f):
            row=[c.strip() for c in row]
            if len(row)<3: continue
            name=row[0].strip('"').strip("'").strip()
            if not name: continue
            try:
                msb,lsb=int(row[1]),int(row[2])
            except ValueError: continue
            if 0<=lsb<=msb<=63:
                locs[name]=[(msb,lsb)]
    print(f"Loaded {len(locs)} operand locations from arg_lut.csv")
    return locs

def pieces_for(name,locs):
    ranges=locs.get(name) or BUILTIN.get(name)
    if not ranges: return []
    out=[]; ob=0
    for hi,lo in ranges:
        out.append({"instructionMsb":hi,"instructionLsb":lo,"operandLsb":ob})
        ob+=hi-lo+1
    return out

def build_split(logical):
    return [{"instructionMsb":msb,"instructionLsb":lsb,"operandLsb":op}
            for msb,lsb,op in SPLIT_IMMEDIATES[logical]]

def make_def(tokens,extension,locs):
    if not tokens: return None
    mn=tokens[0]
    if mn.startswith("$"): return None
    width=width_from_ext(extension)
    mask,match=fixed_mask_match(tokens[1:])
    var=[]
    for t in tokens[1:]:
        if "=" not in t and t not in var: var.append(t)
    operands=[]; seen_logical=set()
    for name in var:
        logical=SPLIT_TOKEN_TO_LOGICAL.get(name)
        if logical is not None:
            if logical in seen_logical: continue
            seen_logical.add(logical)
            operands.append({"name":logical,"pieces":build_split(logical)})
        else:
            p=pieces_for(name,locs)
            if p: operands.append({"name":name,"pieces":p})
    return {"mnemonic":mn,"match":match,"mask":mask,
            "encoding":" ".join(tokens),"extension":extension,
            "variableFields":var,"operands":operands,"length":width//8}

def collect(repo,include_unratified):
    ext_dir=repo/"extensions"
    files=sorted(p for p in ext_dir.iterdir() if p.is_file() and p.name.startswith("rv"))
    if include_unratified:
        un=ext_dir/"unratified"
        if un.exists():
            files+=sorted(p for p in un.iterdir() if p.is_file() and p.name.startswith("rv"))
    locs=load_arg_lut(repo)
    defs=[]; seen=set()
    for path in files:
        ext=path.stem
        for line in read_lines(path):
            line=line.split("#",1)[0].strip()
            if not line: continue
            d=make_def(line.split(),ext,locs)
            if not d: continue
            key=(d["mnemonic"],d["match"],d["mask"],d["length"])
            if key in seen: continue
            seen.add(key); defs.append(d)
    defs.sort(key=lambda d:(d["length"], -bin(d["mask"]).count("1"), d["match"], d["mnemonic"]))
    return defs

# ── Validation ───────────────────────────────────────────────────────────────

def find(defs,mn): return next((d for d in defs if d["mnemonic"]==mn),None)
def oppieces(d,name): return next((o["pieces"] for o in d["operands"] if o["name"]==name),None)

def validate(defs):
    ok=True

    beq=find(defs,"beq")
    if beq is None: print("[FAIL] beq missing"); ok=False
    else:
        if beq["length"]!=4: print(f"[FAIL] beq length={beq['length']}"); ok=False
        if beq["match"]!=0x63: print(f"[FAIL] beq match=0x{beq['match']:x}"); ok=False
        if beq["mask"]!=0x707f: print(f"[FAIL] beq mask=0x{beq['mask']:x}"); ok=False
        bp=oppieces(beq,"bimm12")
        exp=[(31,31,12),(30,25,5),(11,8,1),(7,7,11)]
        act=[(p["instructionMsb"],p["instructionLsb"],p["operandLsb"]) for p in (bp or [])]
        if act!=exp: print(f"[FAIL] beq bimm12 pieces={act}"); ok=False

    srai=find(defs,"srai")
    if srai is None: print("[FAIL] srai missing"); ok=False
    else:
        if srai["length"]!=4: print(f"[FAIL] srai length={srai['length']}"); ok=False
        if srai["match"]!=0x40005013: print(f"[FAIL] srai match=0x{srai['match']:x}"); ok=False
        if srai["mask"]!=0xfc00707f: print(f"[FAIL] srai mask=0x{srai['mask']:x}"); ok=False
        rd=oppieces(srai,"rd")
        rs1=oppieces(srai,"rs1")
        sh=oppieces(srai,"shamtd")
        if rd!=[{"instructionMsb":11,"instructionLsb":7,"operandLsb":0}]:
            print(f"[FAIL] srai rd={rd}"); ok=False
        if rs1!=[{"instructionMsb":19,"instructionLsb":15,"operandLsb":0}]:
            print(f"[FAIL] srai rs1={rs1}"); ok=False
        if sh!=[{"instructionMsb":25,"instructionLsb":20,"operandLsb":0}]:
            print(f"[FAIL] srai shamtd={sh}"); ok=False

    ceb=find(defs,"c.ebreak")
    if ceb is None: print("[FAIL] c.ebreak missing"); ok=False
    else:
        if ceb["length"]!=2: print(f"[FAIL] c.ebreak length={ceb['length']}"); ok=False
        if ceb["match"]!=0x9002: print(f"[FAIL] c.ebreak match=0x{ceb['match']:x}"); ok=False
        if ceb["mask"]!=0xffff: print(f"[FAIL] c.ebreak mask=0x{ceb['mask']:x}"); ok=False

    if ok: print("[VALIDATION] All checks passed.")
    else:  print("[VALIDATION] FAILED — output NOT written.")
    return ok

# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--repo-dir",type=Path)
    ap.add_argument("--output",type=Path,default=Path("data/riscv_decoder.json"))
    ap.add_argument("--include-unratified",action="store_true")
    ap.add_argument("--skip-validation",action="store_true")
    args=ap.parse_args()

    repo,tmp=get_repo(args.repo_dir)
    try:
        defs=collect(repo,args.include_unratified)
        if len(defs)<100:
            raise SystemExit(f"Only {len(defs)} definitions — too few, refusing to write.")
        print(f"\nGenerated {len(defs)} instruction definitions.")
        valid=validate(defs)
        if not valid and not args.skip_validation:
            raise SystemExit("Validation failed. Use --skip-validation to override.")
        args.output.parent.mkdir(parents=True,exist_ok=True)
        doc={"source":"riscv/riscv-opcodes","sourceRepository":OFFICIAL_REPO,
             "generatedBy":"tools/generate_decoder_db.py",
             "instructionCount":len(defs),"instructions":defs}
        with args.output.open("w",encoding="utf-8") as f:
            json.dump(doc,f,indent=2); f.write("\n")
        print(f"Output: {args.output.resolve()}")
    finally:
        if tmp: tmp.cleanup()

if __name__=="__main__":
    main()

