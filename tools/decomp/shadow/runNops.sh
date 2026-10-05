#!/bin/sh
# Rebuilds a Ghidra project ShadowGekko from main_nops.dol (Gekko paired-single words nopped by patchDol.py) and exports decompiled_nops/pseudocode.c. Run from shadowDecomp/ (this script lives in tools/decomp/shadow).
G=/opt/ghidra/support/analyzeHeadless
$G ghidra-project ShadowGekko -import main_nops.dol -processor PowerPC:BE:32:default -loader BinaryLoader -scriptPath ../tools/decomp/shadow -postScript MapDol.java > imp_nops.log 2>&1 &&
$G ghidra-project ShadowGekko -process main_nops.dol -scriptPath ../tools/decomp/shadow -postScript ExportDecompNops.java > analysis_nops.log 2>&1
echo finished > done_nops.flag
