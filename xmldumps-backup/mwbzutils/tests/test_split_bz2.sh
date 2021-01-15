#!/bin/bash

# test split_bz2.py

test_setup() {
    rm -rf tests/output
    mkdir -p tests/output/temp
    cp tests/input/sample-pages-articles.xml.bz2 tests/input/pages-articles.xml-p1p2689.bz2
}

test_cleanup() {
    rm tests/input/pages-articles.xml-p1p2689.bz2
}

if [ ! -e scripts/split_bz2.py ]; then
    echo "Run this script from the dumps repo directory containing the scripts subdirectory."
    exit 1
fi

do_tests() {
    inputfile="$1"
    python3 scripts/split_bz2.py -f tests/input/pages-articles.xml-p1p2689.bz2 -s 250K -o tests/output -b 2 -u '.' --dryrun 2> tests/output/dryrun.txt
    bzip2 tests/output/dryrun.txt

    python3 scripts/split_bz2.py -f tests/input/pages-articles.xml-p1p2689.bz2 -s 250K -o tests/output -b 2 -u '.'
}

check_tests() {
    errors=0
    for outfile in dryrun.txt.bz2 pages-articles.xml-p2035p2689.bz2 pages-articles.xml-p1590p2034.bz2 pages-articles.xml-p1021p1365.bz2 pages-articles.xml-p1366p1589.bz2 pages-articles.xml-p359p1020.bz2 pages-articles.xml-p1p358.bz2; do
	bzcat "tests/output/${outfile}" > "tests/output/temp/got.txt"
	bzcat "tests/output_expected/split_bz2/${outfile}" > "tests/output/temp/expected.txt"
	cmp -s "tests/output/temp/got.txt" "tests/output/temp/expected.txt"
	if [ $? != 0 ]; then
	    echo "TEST FAILED, diff between tests/output/${outfile} and tests/output_expected/split_bz2/${outfile}:"
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
test_cleanup

