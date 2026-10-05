#!/bin/sh
# Rebuilds the Ghidra project SadxGekko from main_nops.dol + rel/_Main.* and exports decompiled/pseudocode.c. Run from sadxDecomp/ (this script lives in tools/decomp/sadx).
G=/opt/ghidra/support/analyzeHeadless
$G ghidra-project SadxGekko -import main_nops.dol -processor PowerPC:BE:32:default -loader BinaryLoader -scriptPath ../tools/decomp/sadx -postScript MapDol.java > imp.log 2>&1 &&
$G ghidra-project SadxGekko -process main_nops.dol -noanalysis -scriptPath ../tools/decomp/sadx -postScript AddRel.java _Main > addrel.log 2>&1 &&
$G ghidra-project SadxGekko -process main_nops.dol -scriptPath ../tools/decomp/sadx -postScript ExportDecomp.java > analysis4.log 2>&1
echo finished > done.flag
