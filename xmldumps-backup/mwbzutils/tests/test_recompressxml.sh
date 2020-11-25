#!/bin/bash

# test recompressxml

test_setup() {
    rm -rf tests/output
    mkdir -p tests/output/temp
    cd tests/output_expected/recompressxml
    cp pages-articles-p2566p2583.multistream.xml.bz2 pages-articles-p2566p2583.multistream-filein.xml.bz2
    cp pages-articles-p2566p2583.multistream-index.xml.bz2 pages-articles-p2566p2583.multistream-index-nofooter.xml.bz2
    cp pages-articles-p2566p2583.multistream-index.xml.bz2 pages-articles-p2566p2583.multistream-index-filein.xml.bz2
    cd ../../../
}

test_cleanup() {
    cd tests/output_expected/recompressxml
    rm pages-articles-p2566p2583.multistream-filein.xml.bz2
    rm pages-articles-p2566p2583.multistream-index-nofooter.xml.bz2
    rm pages-articles-p2566p2583.multistream-index-filein.xml.bz2
    cd ../../../
}

if [ ! -e recompressxml ]; then
    echo "Run this script from the dumps repo directory containing the recompressxml binary."
    exit 1
fi

do_tests() {
    inputfile="$1"
    bzcat "$inputfile" | ./recompressxml -p 5 -b tests/output/pages-articles-p2566p2583.multistream-index.xml.bz2 -o tests/output/pages-articles-p2566p2583.multistream.xml.bz2
    ./recompressxml -i "$inputfile" -p 5 -b tests/output/pages-articles-p2566p2583.multistream-index-filein.xml.bz2 -o tests/output/pages-articles-p2566p2583.multistream-filein.xml.bz2
    bzcat "$inputfile" | ./recompressxml -p 5 -b tests/output/pages-articles-p2566p2583.multistream-index-noheader.xml.bz2 -o tests/output/pages-articles-p2566p2583.multistream-noheader.xml.bz2 -H
    bzcat "$inputfile" | ./recompressxml -p 5 -b tests/output/pages-articles-p2566p2583.multistream-index-nofooter.xml.bz2 -o tests/output/pages-articles-p2566p2583.multistream-nofooter.xml.bz2 -F
}

check_tests() {
    errors=0
    for outfile in pages-articles-p2566p2583.multistream-index.xml.bz2 pages-articles-p2566p2583.multistream.xml.bz2 pages-articles-p2566p2583.multistream-index-filein.xml.bz2 pages-articles-p2566p2583.multistream-filein.xml.bz2 pages-articles-p2566p2583.multistream-index-noheader.xml.bz2 pages-articles-p2566p2583.multistream-noheader.xml.bz2 pages-articles-p2566p2583.multistream-index-nofooter.xml.bz2  pages-articles-p2566p2583.multistream-nofooter.xml.bz2; do
	bzcat "tests/output/${outfile}" > "tests/output/temp/got.txt"
	bzcat "tests/output_expected/recompressxml/${outfile}" > "tests/output/temp/expected.txt"
	cmp -s "tests/output/temp/got.txt" "tests/output/temp/expected.txt"
	if [ $? != 0 ]; then
	    echo "TEST FAILED, diff between tests/output/${outfile} and tests/output_expected/recompressxml/${outfile}:"
	    /usr/bin/diff "tests/output/temp/got.txt" "tests/output/temp/expected.txt"
	    errors=$(( ${errors} + 1 ))
	fi
    done
    if [ $errors != "0" ]; then
	echo "TEST FAILURES in $errors tests"
    else
	echo "SUCCESS"
    fi
}

test_setup
do_tests tests/input/pages-articles-p2566p2583.xml.bz2
check_tests
test_cleanup
