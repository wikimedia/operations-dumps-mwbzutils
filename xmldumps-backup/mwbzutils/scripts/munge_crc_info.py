#!/usr/bin/python3
'''
reads a file of output from the showcrcs util and writes out
int along with hex values, and combined crc for every block
'''
import sys
import getopt


def usage(message=None):
    """display usage info for this script plus optionally
    a preceeding message"""
    if message is not None:
        sys.stderr.write(message)
        sys.stderr.write("\n")
    usage_message = """
Usage:  python3 munge_crc_info.py  --input <filename>
        [--verbose] [--help] [--version]

Given a file with entries containing a file offset and a block crc,
compute the cumulative crc at each block, and write out the offset,
block crc in decimal and hex, and the cumulative crc in decimal and hex.

Entries in the file should look like:
offset:65258451445 CRC:0x3000ea37

Arguments:
--input   (-i)   filename
--verbose (-v)   Show processing messages
--help    (-h)   Show this help message

Example:
  python3 munge_crc_info.py -v -i some-crcs.txt
"""
    sys.stderr.write(usage_message)
    sys.exit(1)


def get_args():
    """get command line args"""
    args = {'inputfile': None, 'verbose': False}

    try:
        (options, remainder) = getopt.gnu_getopt(sys.argv[1:], "i:vh",
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
    with open(args['inputfile'], "r") as infile:
        while True:
            line = infile.readline()
            if not line:
                break
            line = line.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            # offset:65258451445 CRC:0x3000ea37
            fields = line.split()
            if not fields[0].startswith('offset'):
                continue
            if not fields[1].startswith('CRC'):
                continue
            _label, offset_text = fields[0].split(':', 1)
            _label, block_crc_text = fields[1].split(':', 1)
            offset = int(offset_text)
            block_crc = int(block_crc_text, 16)

            cumul_crc = combine_crc(cumul_crc, (block_crc ^ 0xffffffff))
            cumul_crc &= 0xffffffff
            display_block_info(block_crc, cumul_crc, offset)
        print("And now the big finish, the final computed crc")
        display_file_crc(cumul_crc)


if __name__ == '__main__':
    do_main()
