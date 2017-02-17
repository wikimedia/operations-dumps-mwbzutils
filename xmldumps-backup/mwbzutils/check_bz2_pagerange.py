"""
check that bz2 checkpoint content file has the pages
in it that the filename says it does, by checking
first and last pageid of actual content against
those in the filename
"""

import os
import sys
import getopt
import bz2
from subprocess import Popen, PIPE
import requests


def usage(message=None):
    if message is not None:
        sys.stderr.write(message)
        sys.stderr.write("\n")
    usage_message = """
Usage:  check_bz2_pagerange.py --wiki <name> --date <yyyymmdd> [--renames]

Arguments:
--wiki    (-w):  name of wiki as it appears in dblists
--date    (-d):  date of dump run in YYYYMMDD format
--renames (-r):  instead of regular output, generate commands for renames of bad files

Example:
  python check_bz2_pagerange.py -w enwiki -d 20170201
"""
    sys.stderr.write(usage_message)
    sys.exit(1)


def get_pageid_from_filename(path, which):
    basename = os.path.basename(path)

    # enwiki-20170201-pages-meta-history9.xml-p001888020p001938728.bz2
    pages = basename.split('-')[5]
    # p001888020p001938728.bz2
    last = pages.split('p')[which]
    # 001938728.bz2
    return last.split('.')[0].lstrip('0')


def get_last_pageid_from_name(name):
    return get_pageid_from_filename(name, 2)


def get_first_pageid_from_name(name):
    return get_pageid_from_filename(name, 1)


def get_basename(path):
    filename = os.path.basename(path)
    # enwiki-20170201-pages-meta-history9.xml-p001888020p001938728.bz2
    fields = filename.split('-')
    return '-'.join(fields[0:5])


def get_ext(path):
    # /blah/.../enwiki-20170201-pages-meta-history9.xml-p001888020p001938728.bz2
    return path.split('.')[-1]


def assemble_name(basename, first_id, last_id, ext):
    # enwiki-20170201-pages-meta-history9.xml-p001888020p001938728.bz2
    return (basename + "-p{first}p{last}." + ext).format(first=first_id, last=last_id)


def get_args():
    wikiname = None
    date = None
    renames = False

    try:
        (options, remainder) = getopt.gnu_getopt(
            sys.argv[1:], "w:d:rh",
            ["wiki=", "date=", "renames", "help"])

    except getopt.GetoptError as err:
        usage("Unknown option specified: " + str(err))

    for (opt, val) in options:
        if opt in ["-w", "--wiki"]:
            wikiname = val
        elif opt in ["-d", "--date"]:
            date = val
        elif opt in ["-r", "--renames"]:
            renames = True
        elif opt in ["-h", "--help"]:
            usage('Help for this script\n')
        else:
            usage("Unknown option specified: <%s>" % opt)

    if remainder:
        usage("Unknown option specified: <%s>" % remainder[0])

    if not wikiname or not date:
        usage("One of the mandatory arguments 'wikiname' or 'date' was not specified")
    if not date.isdigit() and len(date) != 8:
        usage("Date argument must be of the form YYYYMMDD")

    return wikiname, date, renames


def get_dumpdir(wikiname, date):
    # FIXME get this from config file
    return os.path.join("/mnt/data/xmldatadumps/public", wikiname, date)


def get_bz2_content_files(wikiname, date):
    dumpdir = get_dumpdir(wikiname, date)
    files = os.listdir(dumpdir)
    return [os.path.join(dumpdir, filename) for filename in files if filename.endswith('.bz2')
            and 'meta-history' in filename]


def get_last_revid_from_file(filename):
    # FIXME get this from config file too
    command = ["/usr/local/bin/getlastidinbz2xml", "-f", filename, "-t", "rev"]
    proc = Popen(command, stdout=PIPE, stderr=PIPE)
    output, error = proc.communicate()
    if proc.returncode:
        sys.stderr.write("failed to get revid from filename %s\n" % filename)
        if error:
            sys.stderr.write(error)
        return None
    else:
        if not output.startswith("rev_id:"):
            # bad output line, who knows
            sys.stderr.write("failed to get revid from filename, got %s\n" % output)
            return None
        return output.strip().split(':')[1]


def get_content(url):
    headers = {
        'user-agent':
        'check_bz2_pageragne.py/0.6 (XML dumps aux script; aglenn@wikimedia.org)'
    }
    resp = requests.get(url, headers=headers)
    if resp.status_code == requests.codes.ok:
        return resp.text
    else:
        sys.stderr.write("bad response for url %s, %d" %(url, resp.status_code))
        return None


def get_hostname(filename):
    # <base>https://en.wikipedia.org/wiki/Main_Page</base>
    with bz2.BZ2File(filename) as fhandle:
        while True:
            line = fhandle.readline()
            if "<base>" in line:
                break
        if not line:
            return None
        line = line.strip()
        if not line.startswith('<base>') or not line.endswith('</base>'):
            return None
        url = line[6:-7]
        return url.split('/')[2]


def get_pageid_of_revid_via_api(last_revid, filename):
    hostname = get_hostname(filename)
    apistring = "https://{hostname}/w/api.php?action=query&format=xml&revids={revid}"
    url = apistring.format(hostname=hostname, revid=last_revid)
    content = get_content(url)
    # <api batchcomplete=""><query><pages><page _idx="22086" \
    # pageid="22086" ns="1" title="Talk:Fertility awareness"/></pages></query></api>
    fields = content.split()
    for field in fields:
        if field.startswith("pageid="):
            return field.split('=')[1].strip('"')
    return None


def get_first_pageid_from_content(filename):
    with bz2.BZ2File(filename) as fhandle:
        while True:
            line = fhandle.readline()
            if "<page>" in line:
                break
        if not line:
            return None
        while True:
            line = fhandle.readline()
            if "<page>" in line or "<revision>" in line:
                # no id found. broken file. bail.
                return None
            elif "<id>" in line:
                # <id>2439434</id>
                line = line.strip()
                if not line.startswith('<id>') or not line.endswith('</id>'):
                    return None
                return line[4:-5]
        return None


def do_main():
    wikiname, date, renames = get_args()
    files_to_check = get_bz2_content_files(wikiname, date)
    for filename in sorted(files_to_check):
        claims_first = get_first_pageid_from_name(filename)
        claims_last = get_last_pageid_from_name(filename)
        last_revid = get_last_revid_from_file(filename)
        last_has = get_pageid_of_revid_via_api(last_revid, filename)
        first_has = get_first_pageid_from_content(filename)
        if first_has == claims_first and last_has == claims_last:
            if not renames:
                print "OK", filename
        else:
            if not renames:
                print "BAD", filename, "first_claimed/has:", claims_first,
                print first_has, "last_claimed/has:", claims_last, last_has
            else:
                new_name = assemble_name(get_basename(filename), first_has.zfill(9),
                                         last_has.zfill(9), get_ext(filename))
                path = os.path.join(os.path.dirname(filename), new_name)
                print "mv", filename, path


if __name__ == '__main__':
    do_main()
