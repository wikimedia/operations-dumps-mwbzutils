#!/bin/bash

# test getlastidinbz2xml with the various options

test_setup() {
    rm -rf tests/output
    mkdir tests/output
}

if [ ! -e getlastidinbz2xml ]; then
    echo "Run this script from the dumps repo directory containing the getlastidinbz2xml binary."
    exit 1
fi

do_tests() {
    inputfile_one="$1"
    inputfile_two="$1"
    ./getlastidinbz2xml -f "${inputfile_one}" -t page > tests/output/page-big.txt
    ./getlastidinbz2xml -f "${inputfile_one}" -t rev > tests/output/rev-big.txt
    ./getlastidinbz2xml -f "${inputfile_two}" -t page > tests/output/page-small.txt
    ./getlastidinbz2xml -f "${inputfile_two}" -t rev > tests/output/rev-small.txt
}

check_tests() {
    errors=0
    for outfile in page-big.txt rev-big.txt page-small.txt rev-small.txt; do
	cmp -s "tests/output/${outfile}" "tests/output_expected/getlastidinbz2xml/${outfile}"
	if [ $? != 0 ]; then
	    echo "TEST FAILED, diff between tests/output/${outfile} and tests/output_expected/getlastidinbz2xml/${outfile}:"
	    /usr/bin/diff "tests/output/${outfile}" "tests/output_expected/getlastidinbz2xml/${outfile}"
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
do_tests tests/input/sample-pages-articles.xml.bz2 tests/input/pages-articles-p2566p2583.xml.bz2
check_tests


