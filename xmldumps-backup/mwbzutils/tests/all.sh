#!/bin/bash

testfiles="test_appendbz2.sh test_dumpbz2filefromoffset.sh test_dumplastbz2block.sh test_findpageidinbz2xml.sh test_getlastidinbz2xml.sh test_recompressxml.sh test_revsperpage.sh test_split_bz2.sh test_writeuptopageid.sh"
for testfile in $testfiles; do
    echo "running $testfile"
    bash tests/$testfile
done
