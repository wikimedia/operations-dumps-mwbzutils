#!/bin/bash

# for extra hand checks on the whole procedure one can do the following after the test:

# head -c +1169599 tests/input/sample-pages-articles.xml.bz2  > tests/output/start-of-file.bz2
# cat tests/output/start-of-file.bz2 tests/output/append-from-offset-1169598.bz2 | bzcat | md5sum
# cat tests/input/sample-pages-articles.xml.bz2 | bzcat | md5sum
# expect these to be identical

test_setup() {
    rm -rf tests/output
    mkdir -p tests/output
}

if [ ! -e appendbz2 ]; then
    echo "Run this script from the dumps repo directory containing the appendbz2 binary."
    exit 1
fi

do_tests() {
    inputfile="$1"
    #bzcat append-this-text-1169598.txt.gz  | ../appendbz2 -c 1001478437 -o
    bzcat "$inputfile" | ./appendbz2 -c 1001478437 -o tests/output/append-from-offset-1169598.bz2
}

check_tests() {
    errors=0
    for outfile in append-from-offset-1169598.bz2; do
	cmp -s "tests/output/${outfile}" "tests/output_expected/appendbz2/${outfile}"
	if [ $? != 0 ]; then
	    echo "TEST FAILED, cmp of tests/output/${outfile} and tests/output_expected/appendbz2/${outfile}:"
	    cmp "tests/output/${outfile}" "tests/output_expected/appendbz2/${outfile}"
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
do_tests tests/input/append-this-text-1169598.txt.bz2
check_tests
