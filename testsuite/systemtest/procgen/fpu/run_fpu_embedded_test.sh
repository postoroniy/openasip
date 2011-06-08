#!/bin/bash
TCECC=../../../../tce/src/bintools/Compiler/tcecc
DISASM=../../../../tce/src/bintools/Disassembler/tcedisasm
PROGE=../../../../tce/src/procgen/ProGe/generateprocessor
PIG=../../../../tce/src/bintools/PIG/generatebits

PDIR=proge-output
ADF=data/test_fpu_embedded.adf
IDF=$(echo $ADF | sed 's/.adf/.idf/')
TPEF=test_code.tpef

RUNCYCLES=3000
RUNTIME=${RUNCYCLES}0

# Compile program
$TCECC -O1 -a $ADF -o $TPEF -k result data/test_code.c || exit 1

# Check that there are no emulation functions
$DISASM -o temp.txt $ADF $TPEF || exit 1
grep emulate temp.txt
rm temp.txt

# Generate processor
$PROGE -i $IDF -o $PDIR $ADF
# Generate program image
$PIG -d -w 4 -p $TPEF -x $PDIR $ADF >& /dev/null

# Simulate the processor
cd $PDIR || exit 1

# change the simulation time
eval "sed -i 's/5234/${RUNCYCLES}/g' tb/testbench_constants_pkg.vhdl"
./ghdl_compile.sh >& /dev/null ||  exit 1
# TODO how long does the simulation actually need to run?
eval "./testbench --assert-level=none --stop-time=${RUNTIME}ns >& /dev/null" || exit 1

# Print simulation output
cat printchar_output.txt
