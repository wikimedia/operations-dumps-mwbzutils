#!/bin/bash

# test dumplastbz2block

test_setup() {
    rm -rf tests/output
    mkdir -p tests/output/temp
}

if [ ! -e dumplastbz2block ]; then
    echo "Run this script from the dumps repo directory containing the dumplastbz2block binary."
    exit 1
fi

do_tests() {
    inputfile="$1"
    ./dumplastbz2block "$inputfile" | bzip2 > tests/output/pages-articles-last-block.bz2
}

check_tests() {
    errors=0
    for outfile in pages-articles-last-block.bz2; do
	bzcat "tests/output/${outfile}" > "tests/output/temp/got.txt"
	bzcat "tests/output_expected/dumplastbz2block/${outfile}" > "tests/output/temp/expected.txt"
	cmp -s "tests/output/temp/got.txt" "tests/output/temp/expected.txt"
	if [ $? != 0 ]; then
	    echo "TEST FAILED, diff between tests/output/${outfile} and tests/output_expected/dumplastbz2block/${outfile}:"
	    /usr/bin/diff "tests/output/temp/got.txt" "tests/output/temp/expected.txt" | head -10
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


