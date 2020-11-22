#!/usr/bin/python3
'''
display offset and crc of all byte-aligned bz2
blocks in file, plus the combined crc of these
blocks
'''
import os
import sys
import getopt


MARKER = b"1AY&SY"
FOOTER = b"\x17\x72\x45\x38\x50\x90"


def usage(message=None):
    """display usage info for this script plus optionally
    a preceeding message"""
    if message is not None:
        sys.stderr.write(message)
        sys.stderr.write("\n")
    usage_message = """
Usage:  show_byte_aligned_crcs.py  --input <filename>
        [--verbose] [--help] [--version]

Show the offsets of all byt-aligned bz2 blocks in the file, in order,
 along with their crcs.
Blocks are detected by checking for start of block markers. We do not check
that the block decompressses, nor do we search for non-byte-aligned blocks.

Arguments:
--input   (-i)   filename
--verbose (-v)   Show processing messages
--help    (-h)   Show this help message

Example:
  python3 show_byte_aligned_crcs.py -v -i elwikt-20181213-pages-articles3.xml_lbzip2.bz2
"""
    sys.stderr.write(usage_message)
    sys.exit(1)


def get_args():
    """get command line args"""
    args = {'inputfile': None, 'verbose': False}

    try:
        (options, remainder) = getopt.gnu_getopt(sys.argv[1:], "i:vVh",
                                                 ["input=", "verbose", "help"])

    except getopt.GetoptError as err:
        usage("Unknown option specified: " + str(err))

    for (opt, val) in options:
        if opt in ["-i", "--input"]:
            args['inputfile'] = val
        elif opt in ["-v", "--verbose"]:
            args['verbose'] = True
        elif opt in ["-h", "--help"]:
            usage('Help for this script\n')
        else:
            usage("Unknown option specified: <%s>" % opt)

    if remainder:
        usage("Unknown option specified: <%s>" % remainder[0])

    if not args['inputfile']:
        usage("The mandatory argument 'input' was not specified")

    return args


def find_block_marker(infile):
    """
    find the next byte aligned bz2 block marker in the file from the
    current offset, set the file to the byte right after the end of the
    bz2 marker, return the offset to the starting byte of the block,
    or leave the file at eof and return None
    """
    eof = False
    carryover = b""
    first = True
    while not eof:
        contents = infile.read(4096)
        if not contents:
            return None
        read_count = len(contents)
        contents = carryover + contents
        where = contents.find(MARKER)
        if where == -1:
            if first:
                first = False
            else:
                carryover = contents[-6:]
            continue

        # from current position, move back the number of bytes read,
        # then move forward the position of marker in buffer,
        # but we added 6 extra bytes into the buffer so move backward that 6 bytes,
        # but we want to move to the end of the marker so move forward 6 extra bytes
        to_seek_backwards = read_count - where
        if not carryover:
            to_seek_backwards -= 6
        infile.seek(-1 * to_seek_backwards, os.SEEK_CUR)
        return infile.tell() - 6


def get_block_crc(infile):
    """
    read bytes of crc from current file position and return the value
    """
    crc_bytes = infile.read(4)
    if not crc_bytes:
        return None

    # return int.from_bytes(crc_bytes, sys.byteorder)
    return int.from_bytes(crc_bytes, byteorder='big')


def display_block_info(block_crc, cumul_crc, offset):
    """display block crc, cumul crc, offset to block"""
    if block_crc is None:
        block_val = "None"
    else:
        block_val = hex(block_crc)
    print("Block_CRC:", block_val, "(", block_crc, "), Cumul_CRC:", hex(cumul_crc),
          "(", cumul_crc, ")", "Offset:", offset)


def display_file_crc(file_crc):
    """display a file crc"""
    if file_crc is None:
        print("No crc retrieved for the file")
    else:
        print("File CRC:", hex(file_crc))


def get_file_crc(infile):
    """go to the end of the file, find the bz2 footer
    and the file crc, and return that crc as an int;
    we assume the footer is byte aligned; if not, the
    information won't be available"""
    infile.seek(-10, os.SEEK_END)

    # make sure we have the bz2 footer here
    end_marker = infile.read(6)
    if end_marker != FOOTER:
        return None

    crc_bytes = infile.read(4)
    if not crc_bytes:
        return None

    # return int.from_bytes(crc_bytes, sys.byteorder)
    return int.from_bytes(crc_bytes, byteorder='big')


def combine_crc(combined, block):
    '''
    generate combined crc from the previous combined crc
    and the new block crc
    '''
    return ((combined) << 1) ^ ((combined) >> 31) ^ (block) ^ -1


def do_main():
    """entry point"""
    args = get_args()
    cumul_crc = 0
    with open(args['inputfile'], "rb") as infile:
        while True:
            offset = find_block_marker(infile)
            if offset is None:
                break
            block_crc = get_block_crc(infile)
            cumul_crc = combine_crc(cumul_crc, (block_crc ^ 0xffffffff))
            cumul_crc &= 0xffffffff
            display_block_info(block_crc, cumul_crc, offset)
        file_crc = get_file_crc(infile)
        display_file_crc(file_crc)
        print("And now the big finish, the computed crc")
        display_file_crc(cumul_crc)


if __name__ == '__main__':
    do_main()
