#!/bin/bash

# test dumpbz2filefromoffset

test_setup() {
    rm -rf tests/output
    mkdir -p tests/output/temp
}

if [ ! -e dumpbz2filefromoffset ]; then
    echo "Run this script from the dumps repo directory containing the dumpbz2filefromoffset binary."
    exit 1
fi

do_tests() {
    inputfile="$1"
    ./dumpbz2filefromoffset "$inputfile" 1486591  | bzip2 > tests/output/from-offset-1486591-page.bz2
    ./dumpbz2filefromoffset "$inputfile" 1486591 raw  | bzip2 > tests/output/from-offset-1486591-raw.bz2
    ./dumpbz2filefromoffset "$inputfile" 1663000 raw 2>&1 | bzip2 > tests/output/from-offset-1663000-raw.bz2
}

check_tests() {
    errors=0
    for outfile in from-offset-1486591-page.bz2 from-offset-1486591-raw.bz2 from-offset-1663000-raw.bz2; do
	bzcat "tests/output/${outfile}" > "tests/output/temp/got.txt"
	bzcat "tests/output_expected/dumpbz2filefromoffset/${outfile}" > "tests/output/temp/expected.txt"
	cmp -s "tests/output/temp/got.txt" "tests/output/temp/expected.txt"
	if [ $? != 0 ]; then
	    echo "TEST FAILED, diff between tests/output/${outfile} and tests/output_expected/dumpbz2filefromoffset/${outfile}:"
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
do_tests tests/input/sample-pages-articles.xml.bz2
check_tests


