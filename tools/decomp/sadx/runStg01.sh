#!/bin/sh
# Rebuilds a Ghidra project SadxStg01 from main_nops.dol + rel/_Stg01.* (buildRelImage.py _Stg01.rel _Stg01) and exports decompiled_stg01/pseudocode.c. Run from sadxDecomp/ (this script lives in tools/decomp/sadx).
G=/opt/ghidra/support/analyzeHeadless
$G ghidra-project SadxStg01 -import main_nops.dol -processor PowerPC:BE:32:default -loader BinaryLoader -scriptPath ../tools/decomp/sadx -postScript MapDol.java > imp_stg01.log 2>&1 &&
$G ghidra-project SadxStg01 -process main_nops.dol -noanalysis -scriptPath ../tools/decomp/sadx -postScript AddRel.java _Stg01 > addrel_stg01.log 2>&1 &&
$G ghidra-project SadxStg01 -process main_nops.dol -scriptPath ../tools/decomp/sadx -postScript ExportDecompStg.java > analysis_stg01.log 2>&1
echo finished > done_stg01.flag
