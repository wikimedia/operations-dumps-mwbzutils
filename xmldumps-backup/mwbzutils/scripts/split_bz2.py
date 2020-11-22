#!/usr/bin/python3
'''
split large bz2 xml dump files into smaller ones, with mw header
and footer
'''
import os
import re
import sys
import getopt
from subprocess import Popen, PIPE


# last block needs to be bigger than this; the number gets
# adjusted for small files
MINSIZE = 25000000


def usage(message=None):
    '''
    display a nice usage message along with an optional message
    typically describing an error
    '''
    if message:
        sys.stderr.write(message + "\n")
    usage_message = """Usage:
   split_bz2.py --files <path>[:lastpage][,<path>[:lastpage]...] --splitsize <int>
                --odir <path> [--batchsize] [--verbose] [--dryrun]
or:
   split_bz2.py --help

Splits large bz2-compressed xml dump files into smaller ones.
This script depends on dumpbz2filefromoffset and writeuptopageid
from the mwbzutils package.

Arguments:

 --files     (-f):  comma-separated list of bz2 files to split; if the file
                    name does not conform to the standard blot.xml-pnnnpmmm.bz2
                    format or there is no good bz2 end block, the filename must
                    be followed by a colon and the id of the last page to be
                    contained in the last file of the split, otherwise the last
                    page is taken from the pmmm value in the filename
 --splitsize (-s):  how large each output piece should roughly be, compressed;
                    the nth split of an input file will start with the first
                    page found after n*offset into the compressed file;
                    sizes ending in K, M or G will be multiplied by 1,000,
                    1,000,000 or 1,000,000,000 respectively.
 --odir      (-o):  path of existing directory in which to write output files;
                    if relative path is given, the path will be expanded relative
                    to the current working directory from where the script is
                    invoked, not the directory where the script resides
 --batchsize (-b):  number of split processes to run at once; default 1

Flags:

 --dryrun    (-d):  don't run the commands to split the file, print the commands
                    that would be run
 --verbose   (-v):  display various progress messages while running
 --help      (-h):  show this help message

Example:

 python split_bz2.py --files elwikt-20200214-pages-meta-history4.xml.bz2:492784
     --splitsize 30M --batchsize 3 --odir testout --verbose
"""
    sys.stderr.write(usage_message)
    sys.exit(1)


def validate_args(files, splitsize, outputdir, batchsize):
    '''
    complain about various args if not set or they have bad
    values
    '''
    if not files:
        usage("Mandatory files argument not specified")

    if not splitsize:
        usage("Mandatory splitsize argument not specified")
    if not splitsize.isdigit():
        if splitsize.endswith(('K', 'M', 'G')):
            if not splitsize[:-1].isdigit():
                usage("splitsize must be a number or a number followed by K, M, G")

    if not outputdir:
        usage("Mandatory odir argument not specified")
    if not os.path.isabs(outputdir):
        outputdir = os.path.join(os.getcwd(), outputdir)
    if not os.path.exists(outputdir):
        usage("No such directory: " + outputdir)

    if not batchsize.isdigit():
        usage("batchsize must be an number")


def get_files_pages(files):
    '''
    get page numbers from filespecs
    '''
    files_pages = []
    for file in files:
        if ':' in file:
            filename, lastpage = file.split(':', 1)
            if not lastpage.isdigit():
                usage("bad files option supplied")
        else:
            filename = file
            lastpage = None
        files_pages.append((filename, lastpage))
    return files_pages


def convert_num(number):
    '''
    convert K, M, or G number to int by multiplying
    appropriately; if no such ending, it's bytes and
    just return those
    '''
    if number.isdigit():
        return int(number)
    if number.endswith('K'):
        return int(number[:-1]) * 1000
    if number.endswith('M'):
        return int(number[:-1]) * 1000000
    if number.endswith('G'):
        return int(number[:-1]) * 1000000000
    return None


def parse_args():
    '''
    get command line args, validate and return them
    '''
    splitsize = None
    files = None
    outputdir = None
    batchsize = "1"
    flags = {'verbose': False, 'dryrun': False}

    try:
        (options, remainder) = getopt.gnu_getopt(
            sys.argv[1:], "b:f:o:s:dvh", ["batchsize=", "files=", "odir=", "splitsize=",
                                          "dryrun", "verbose", "help"])

    except getopt.GetoptError as err:
        usage("Unknown option specified: " + str(err))

    for (opt, val) in options:
        if opt in ["-b", "--batchsize"]:
            batchsize = val
        elif opt in ["-f", "--files"]:
            files = val.split(',')
        elif opt in ["-o", "--odir"]:
            outputdir = val
        elif opt in ["-s", "--splitsize"]:
            splitsize = val
        elif opt in ["-d", "--dryrun"]:
            flags['dryrun'] = True
        elif opt in ["-v", "--verbose"]:
            flags['verbose'] = True
        elif opt in ["-h", "--help"]:
            usage('Help for this script\n')
        else:
            usage("Unknown option specified: <%s>" % opt)

    if remainder:
        usage("Unknown option(s) specified: {opt}".format(opt=remainder[0]))

    validate_args(files, splitsize, outputdir, batchsize)
    files_pages = get_files_pages(files)
    return convert_num(splitsize), files_pages, outputdir, int(batchsize), flags


def get_file_offsets(filename, splitsize):
    '''
    given a path to a file and the size of each
    desired piece, return an ascending list of offsets into the
    file. if the last offset is "too close" to the end of the
    file, so that maybe there wouldn't be a bzip2 block found
    in there or there would be just a teeny tiny file compared
    to the rest, drop that one from the list
    '''
    filesize = os.stat(filename).st_size
    # need a plausible minimum length here for the end block(s)
    # and allow testing on tiny tiny files
    if MINSIZE >= filesize / 2:
        margin = int(filesize / 10)
    else:
        margin = MINSIZE
    if filesize <= splitsize + margin:
        return None
    offsets = list(range(0, filesize, splitsize))
    if filesize - offsets[-1] < margin:
        offsets = offsets[:-1]
    return offsets


def get_todo_basics(files_pages, splitsize):
    '''
    return dict of input filenames, and offsets/last page of input files
    '''
    todos = {}
    for entry in files_pages:
        file_to_split = entry[0]
        offsets = get_file_offsets(file_to_split, splitsize)
        if offsets is None:
            print("File", file_to_split, "smaller than or not much bigger than splitsize, skipping")
            continue
        lastpage = entry[1]
        if lastpage is None:
            # expect stuff.xml-p<start>p<end>.bz2[.maybejunkhere]
            fields = file_to_split.split('.')
            for field in fields:
                if field.startswith("xml-p"):
                    lastpage = field.split("p")[-1]
        if lastpage is None:
            print("Failed to get last page from filename", file_to_split,
                  "Is it a valid name?", file=sys.stderr)
            usage()
        todos[file_to_split] = {'lastpage': lastpage, 'offsets': offsets}
    return todos


def get_pageid_commands(filename, offset):
    '''
    given an input bz2 filename and an offset into the file,
    generate the pipeline commands to run that will produce
    the first page id after that offset
    '''
    dumpbz2_cmd = ["/usr/local/bin/dumpbz2filefromoffset", filename, str(offset)]
    egrep_cmd = ["/bin/grep", "-m", "1", "-A", "5", "-a", "<page>"]
    return [dumpbz2_cmd, egrep_cmd]


def run_pageid_commands(commands, flags):
    '''given a pipeline of commands, run them and get the output,
    expected to be one line'''
    dumpbz2 = commands[0]
    egrep = commands[1]
    p_dumpbz2 = Popen(dumpbz2, stdout=PIPE)
    p_egrep = Popen(egrep, stdin=p_dumpbz2.stdout, stdout=PIPE)
    p_dumpbz2.stdout.close()

    page_header = p_egrep.communicate()[0]
    if not page_header:
        return None
    # process output as bytes because the bz2 block may start in the
    # middle of a unicode character
    if b'<page>' not in page_header:
        if flags['verbose']:
            print("page header with no page tag!", page_header, file=sys.stderr)
        return None
    result = re.search(b'<page>.*?<id>([0-9]+)</id>', page_header, re.DOTALL)
    if not result:
        return None

    page_id = result.group(1)
    return page_id.decode('utf-8')


def get_outfile_templ(filename):
    '''
    return a formattable output filename based on the input one
    '''
    # could look like wikidatawiki-20201101-pages-meta-history23.xml-p55730033p57265088.bz2
    # or could look like blotty_blot.bz2 or any inbetween thing
    # we want the filename to end in .xml-p{start}p{end}.bz2[...]
    if not filename.endswith('bz2'):
        # this is an error
        return None

    basefilename = os.path.basename(filename)

    prange = 'p{start}p{end}.'

    if '.xml-' in basefilename:
        basename, _junk, ext = basefilename.rsplit('.', 2)
        return basename + '.xml-' + prange + ext

    if basefilename.endswith('.xml.bz2'):
        basename, ext = basefilename.rsplit('.', 1)
        return basename + '-' + prange + ext

    # remaining case: filename ends in '.bz2'
    basename, ext = basefilename.rsplit('.', 1)
    return basename + '.xml-' + prange + ext


def get_writeupto_commands(todos, filename, offset, next_offset):
    '''
    return the pipeline of commands to do writeuptopageid for a particular
    output piece from a given offset of the input file

    For anyone reading this in the future:

    the writeupto filespec requires the filename, the start page, and the
    end page:   "filename:start:end"

    the filename should contain the start and end page id; this is not
    needed by the writeuptopageid util, but it is the required format
    for dump output files.

    the start page in the filename is the actual first page that will be
    written into the file.

    the last page in the filename may not necessarily be in the file;
    it might be a deleted page. what is expected by the dumps scripts
    is that the next filename in sequence will have its first page name
    sequential to this last page.

    the last page in the filespec ("end") is the last page in the filename
    PLUS ONE because $someone (me) decided that the range should be
    (first, last) exclusive of the last page, since python loves that sort
    of thing. This was a Bad Idea and we won't fix it now.
    '''
    start = todos[filename]['entries'][offset]['firstpage']
    if next_offset is not None:
        end = str(int(todos[filename]['entries'][next_offset]['firstpage']) - 1)
    else:
        end = str(todos[filename]['lastpage'])
    outfile = todos[filename]['outputfiletempl'].format(start=start, end=end)
    outfile_spec = outfile + ":" + start + ":" + str(int(end) + 1)
    dumpbz2_cmd = ["/usr/local/bin/dumpbz2filefromoffset", filename, str(offset)]
    writeuptopageid_cmd = ["/usr/local/bin/writeuptopageid", "-o",
                           todos[filename]['odir'], "-f", outfile_spec]
    return [dumpbz2_cmd, writeuptopageid_cmd], os.path.join(todos[filename]['odir'], outfile)


def get_batch(todos, batches, flags):
    '''get a batch of commands where the output file isn't already written'''
    count = 0
    to_run = []
    for filename in todos:
        for offset in todos[filename]['entries']:
            if todos[filename]['entries'][offset]['outputfile'] is None:
                continue
            if 'batched' in todos[filename]['entries'][offset]:
                # yeah this means we scan through the entire list every time
                # which is a bit crap but these are short lists, screw it
                continue
            if os.path.exists(todos[filename]['entries'][offset]['outputfile']):
                if flags['verbose']:
                    print("skipping commands", todos[filename]['entries'][offset],
                          "because outfile", todos[filename]['entries'][offset]['outputfile'],
                          "exists already", file=sys.stderr)
                continue
            to_run.append(todos[filename]['entries'][offset])
            todos[filename]['entries'][offset]['batched'] = True
            count += 1
            if count >= batches:
                return to_run
    return to_run


def run_split_commands(commands):
    '''actually run the pipeline of commands, returning commands
    with errors, if there are any'''
    processes = []
    errors = []

    for command in commands:
        dumpbz2 = command[0]
        writeupto = command[1]
        p_dumpbz2 = Popen(dumpbz2, stdout=PIPE)
        p_writeupto = Popen(writeupto, stdin=p_dumpbz2.stdout)
        p_dumpbz2.stdout.close()
        processes.append([p_dumpbz2, p_writeupto, command])
    for proc in processes:
        proc[0].wait()
        proc[1].wait()
        if proc[1].returncode:
            errors.append(proc[2])
    return errors


def maybe_run_split_commands(to_run, flags):
    '''run or display the commands simultaenously'''
    commands = [entry['commands'] for entry in to_run]
    if flags['dryrun']:
        print("would run:", commands, file=sys.stderr)
        return []

    if flags['verbose']:
        print("running commands:", commands, file=sys.stderr)
    return run_split_commands(commands)


def set_first_page_ids(todos, filename, flags):
    '''
    find and store the first page id after each offset in the
    (compressed) file. if none is found we silently skip
    that offset.
    '''
    ordered_offsets = sorted(list(todos[filename]['offsets']))
    for offset in ordered_offsets:
        commands = get_pageid_commands(filename, offset)
        page_id = run_pageid_commands(commands, flags)
        if flags['verbose']:
            print("got first page id", page_id, "for offset:", offset, "of file:",
                  filename, "with commands", commands, file=sys.stderr)
        if not page_id:
            # we are too close to the end of the file maybe? can't think of
            # another reason but anyways let's carry on
            continue
        todos[filename]['entries'][offset] = {}
        todos[filename]['entries'][offset]['firstpage'] = page_id


def set_split_commands(todos, filename, flags):
    '''
    given the first page id after every offset,
    generate and stash the commands to write the pieces
    of the file

    Note that output filenames must contain the first and last
    page id, so we must look at the first page id from the next offset
    to determine the last page.
    The last offset will get its last page from the
    todos[filename]['lastpage'] value
    '''
    ordered_offsets = sorted(list(todos[filename]['offsets']))
    if flags['verbose']:
        print("offsets are:", ordered_offsets, file=sys.stderr)
    for offset in ordered_offsets:
        todo_entry_offset = todos[filename]['entries'][offset]

        next_index = ordered_offsets.index(offset) + 1
        if next_index == len(ordered_offsets):
            next_offset = None
        else:
            next_offset = ordered_offsets[next_index]
            if (todo_entry_offset['firstpage'] ==
                    todos[filename]['entries'][next_offset]['firstpage']):
                # this offset and the next one have the same first page id, skip this one then.
                todo_entry_offset['commands'] = None
                todo_entry_offset['outputfile'] = None
                if flags['verbose']:
                    print("chunk at offset", offset, "skipped, same page id as next offset",
                          file=sys.stderr)
                continue
        todo_entry_offset['commands'], todo_entry_offset['outputfile'] = (
            get_writeupto_commands(todos, filename, offset, next_offset))


def fill_in_todos(todos, outputdir, flags):
    '''fill in the todos the rest of the way with
    output directory info, commands, etc.'''
    for filename in todos:
        todos[filename]['odir'] = outputdir
        todos[filename]['outputfiletempl'] = get_outfile_templ(filename)
        todos[filename]['entries'] = {}
        set_first_page_ids(todos, filename, flags)
        set_split_commands(todos, filename, flags)


def do_main():
    '''
    entry point
    '''
    splitsize, files_pages, outputdir, batches, flags = parse_args()

    todos = get_todo_basics(files_pages, splitsize)

    fill_in_todos(todos, outputdir, flags)

    while True:
        to_run = get_batch(todos, batches, flags)
        if not to_run:
            break
        errors = maybe_run_split_commands(to_run, flags)
        if errors:
            print("The following commands failed from this batch:", file=sys.stderr)
            for error in errors:
                print(error, file=sys.stderr)


if __name__ == '__main__':
    do_main()
