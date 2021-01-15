#!/bin/bash

# test writeuptopageid

test_setup() {
    rm -rf tests/output
    mkdir -p tests/output/temp
    cd tests/output_expected/writeuptopageid
    cp stubs-p40p44.xml.gz stubs-p40p44-filespec.xml.gz
    cp siteinfo-header-footer.gz siteinfo-header-footer-before-range.gz
    cp siteinfo-header-footer.gz siteinfo-header-footer-after-range.gz
    cd ../../../
    if [ -e "/usr/bin/zcat" ]; then
        ZCAT="/usr/bin/zcat"
    else
        ZCAT="/bin/zcat"
    fi
}

if [ ! -e writeuptopageid ]; then
    echo "Run this script from the dumps repo directory containing the writeuptopageid binary."
    exit 1
fi

test_cleanup() {
    cd tests/output_expected/writeuptopageid
    rm -f siteinfo-header-footer-after-range.gz
    rm -f siteinfo-header-footer-before-range.gz
    rm -f stubs-p40p44-filespec.xml.gz
    cd ../../../
}

do_tests() {
    inputfile="$1"
    # without filespec
    $ZCAT "$inputfile" | ./writeuptopageid 40 45 | gzip > tests/output/stubs-p40p44.xml.gz
    $ZCAT "$inputfile" | ./writeuptopageid 20 21 | gzip > tests/output/siteinfo-header-footer-before-range.gz
    $ZCAT "$inputfile" | ./writeuptopageid 70 80 | gzip > tests/output/siteinfo-header-footer-after-range.gz
    # with filespec
    $ZCAT "$inputfile" | ./writeuptopageid -o tests/output -f stubs-p40p44-filespec.xml.gz:40:45
    $ZCAT "$inputfile" | ./writeuptopageid -o tests/output -f header.gz:20:30 -F
    $ZCAT "$inputfile" | ./writeuptopageid -o tests/output -f footer.gz:20:30 -H
    $ZCAT "$inputfile" | ./writeuptopageid -o tests/output -f empty.gz:20:30 -F -H
}

check_tests() {
    errors=0
    for outfile in stubs-p40p44.xml.gz siteinfo-header-footer-before-range.gz siteinfo-header-footer-after-range.gz stubs-p40p44-filespec.xml.gz header.gz footer.gz empty.gz; do
	$ZCAT "tests/output/${outfile}" > "tests/output/temp/got.txt"
	$ZCAT "tests/output_expected/writeuptopageid/${outfile}" > "tests/output/temp/expected.txt"
	cmp -s "tests/output/temp/got.txt" "tests/output/temp/expected.txt"
	if [ $? != 0 ]; then
	    echo "TEST FAILED, diff between tests/output/${outfile} and tests/output_expected/writeuptopageid/${outfile}:"
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
do_tests tests/input/sample-stubs.gz
check_tests
test_cleanup
