#!/bin/bash

# test findpageidinbz2xml

test_setup() {
    rm -rf tests/output
    mkdir tests/output
}

if [ ! -e findpageidinbz2xml ]; then
    echo "Run this script from the dumps repo directory containing the findpageidinbz2xml binary."
    exit 1
fi

do_tests() {
    inputfile_one="$1"
    inputfile_two="$2"
    ./findpageidinbz2xml -f "${inputfile_one}" -p 2850 > tests/output/page-2580.txt 
    ./findpageidinbz2xml -f "${inputfile_two}" -p 2681 > tests/output/page-2681.txt 
}

check_tests() {
    errors=0
    for outfile in page-2580.txt page-2681.txt; do
	cmp -s "tests/output/${outfile}" "tests/output_expected/findpageidinbz2xml/${outfile}"
	if [ $? != 0 ]; then
	    echo "TEST FAILED, diff between tests/output/${outfile} and tests/output_expected/findpageidinbz2xml/${outfile}:"
	    /usr/bin/diff "tests/output/${outfile}" "tests/output_expected/findpageidinbz2xml/${outfile}"
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
do_tests tests/input/pages-articles-p2566p2583.xml.bz2 tests/input/sample-pages-articles.xml.bz2
check_tests


