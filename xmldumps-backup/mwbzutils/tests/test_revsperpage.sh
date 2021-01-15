#!/bin/bash

# test revsperpage with the various options

test_setup() {
    rm -rf tests/output
    mkdir tests/output
    if [ -e "/usr/bin/zcat" ]; then
        ZCAT="/usr/bin/zcat"
    else
        ZCAT="/bin/zcat"
    fi
}

if [ ! -e revsperpage ]; then
    echo "Run this script from the dumps repo directory containing the revsperpage binary."
    exit 1
fi

do_tests() {
    inputfile="$1"
    $ZCAT "$inputfile" | ./revsperpage --all > tests/output/all.txt
    $ZCAT "$inputfile" | ./revsperpage --all --title > tests/output/all-titles.txt
    $ZCAT "$inputfile" | ./revsperpage --all --title --bytes > tests/output/all-titles-bytes.txt
    $ZCAT "$inputfile" | ./revsperpage --all --maxrevlen > tests/output/all-maxrevlen.txt
    $ZCAT "$inputfile" | ./revsperpage --all --title --cutoff 5 > tests/output/all-titles-cutoff.txt
    $ZCAT "$inputfile" | ./revsperpage --all --batch 4 > tests/output/all-batch.txt
    $ZCAT "$inputfile" | ./revsperpage --title --maxrevlen --bytes > tests/output/maxrevlen-bytes-titles.txt
    $ZCAT "$inputfile" | ./revsperpage --maxrevlen --bytes --batch 4 > tests/output/maxrevlen-bytes-batch.txt
}

check_tests() {
    errors=0
    for outfile in all.txt all-titles.txt all-titles-bytes.txt all-maxrevlen.txt all-titles-cutoff.txt all-batch.txt maxrevlen-bytes-titles.txt maxrevlen-bytes-batch.txt; do
	cmp -s "tests/output/${outfile}" "tests/output_expected/revsperpage/${outfile}"
	if [ $? != 0 ]; then
	    echo "TEST FAILED, diff between tests/output/${outfile} and tests/output_expected/revsperpage/${outfile}:"
	    /usr/bin/diff "tests/output/${outfile}" "tests/output_expected/revsperpage/${outfile}"
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
do_tests tests/input/sample-stubs.gz
check_tests


