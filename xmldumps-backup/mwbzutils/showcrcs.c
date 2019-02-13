#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <regex.h>
#include <inttypes.h>
#include <zlib.h>
#include "mwbzutils.h"

/* stolen from lbzip2 */
#define combine_crc(cc,c) (((cc) << 1) ^ ((cc) >> 31) ^ (c) ^ -1)

void usage(char *message) {
  char * help =
"Usage: showcrcs --filename file\n"
"       [--verbose] [--help] [--version]\n\n"
"Show the offsets of all bz2 blocks in file, in order, along with their crcs.\n"
"Blocks are detected by checking for start of block markers and doing partial\n"
"decompression to be sure that the marker is not just part of some compressed\n"
"data.\n\n"
"Options:\n\n"
"  -f, --filename   name of file to search\n"
"  -v, --verbose    Show processing messages\n"
"  -h, --help       Show this help message\n"
"  -V, --version    Display the version of this program and exit\n\n"
"Report bugs in showcrcs to <https://phabricator.wikimedia.org/>.\n\n"
"See also dumpbz2filefromoffset(1), dumplastbz2block(1), findpageidinbz2xml(1),\n"
    "recompressxml(1), writeuptopageid(1)\n\n";
  if (message) {
    fprintf(stderr,"%s\n\n",message);
  }
  fprintf(stderr,"%s",help);
  exit(-1);
}

void show_version(char *version_string) {
  char * copyright =
"Copyright (C) 2019 Ariel T. Glenn.  All rights reserved.\n\n"
"This program is free software: you can redistribute it and/or modify it\n"
"under the  terms of the GNU General Public License as published by the\n"
"Free Software Foundation, either version 2 of the License, or (at your\n"
"option) any later version.\n\n"
"This  program  is  distributed  in the hope that it will be useful, but\n"
"WITHOUT ANY WARRANTY; without even the implied warranty of \n"
"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General\n"
"Public License for more details.\n\n"
"You should have received a copy of the GNU General Public License along\n"
"with this program.  If not, see <http://www.gnu.org/licenses/>\n\n"
    "Written by Ariel T. Glenn.\n";
  fprintf(stderr,"showcrcs %s\n", version_string);
  fprintf(stderr,"%s",copyright);
  exit(-1);
}

/*
   find the first bz2 block marker in the file,
   from its current position,
   then set up for decompression from that point
   returns:
     0 on success
     -1 if no marker or other error
 */
void init_bz2_info(bz_info_t *bfile, int fin) {
  bfile->bufin_size = BUFINSIZE;
  bfile->marker = init_marker();
  bfile->bytes_read = 0;
  bfile->bytes_written = 0;
  bfile->eof = 0;
  bfile->file_size = get_file_size(fin);
  bfile->header_read = 0;

  bfile->initialized++;
}

void show_crc(unsigned char *otherbuffer, int fin, off_t block_start, int bits_shifted, uint64_t *block_crc, int verbose) {
  uint64_t crc = (uint64_t)0;
  unsigned char buffer[5];
  off_t seekres;
  int res = 0;

  /* block marker is 6 bytes long, if it's bit-shifted then some bits of the crc
   will be in the 6th byte, otherwise only (byte-aligned) in the 7th*/
  if (bits_shifted)
    seekres = lseek(fin, block_start + (off_t)6, SEEK_SET);
  else
    seekres = lseek(fin, block_start + (off_t)7, SEEK_SET);
  if (seekres == (off_t)-1) {
    fprintf(stderr,"lseek of file failed\n");
    exit(1);
  }
  /* we need the next 4 bytes for the crc, 5 if we have bit-shifting so just get 5 */
  res = read(fin, buffer, 5);
  if (res == -1) {
    fprintf(stderr,"read of file failed\n");
    exit(-1);
  }
  if (verbose)
    fprintf(stdout, "buffer: %02x %02x %02x %02x %02x\n", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);
  crc += (uint64_t) buffer[0] & bit_mask(8 - bits_shifted, MASKRIGHT);
  crc = crc << 8;
  if (verbose > 1)
    fprintf(stdout, "crc with buffer[0] and shifted: 0x%lx\n", crc);
  crc += (uint64_t) buffer[1];
  crc = crc << 8;
  if (verbose > 1)
    fprintf(stdout, "crc with buffer[1] and shifted: 0x%lx\n", crc);
  crc += (uint64_t) buffer[2];
  crc = crc << 8;
  if (verbose > 1)
    fprintf(stdout, "crc with buffer[2] and shifted: 0x%lx\n", crc);
  crc += (uint64_t) buffer[3];
  if (bits_shifted) {
    if (verbose)
      fprintf(stdout, "block crc bits shifted by %d\n", bits_shifted);
    crc = crc << bits_shifted;
    if (verbose > 1)
      fprintf(stdout, "crc with buffer[3] and shifted: 0x%lx\n", crc);
    crc += (uint64_t) (buffer[4] & bit_mask(bits_shifted, MASKLEFT)) >> (8 - bits_shifted);
  }
  crc &= 0xffffffff;
  fprintf(stdout, "CRC:0x%08lx\n", crc);
  *block_crc = crc;
}

/*
   from current point in the file, find the next bz2 block and display
   crc/offset information
 */
off_t do_next_block(bz_info_t *bfile, int fin, off_t offset, uint64_t *block_crc, off_t filesize, int verbose) {
  offset = find_first_bz2_block_from_offset(bfile, fin, offset, FORWARD, filesize, 0);
  if (!offset) {
    return(0);
  }
  else if (offset > (off_t)0) {
    fprintf(stdout, "offset:%"PRId64" ", offset);
    show_crc(bfile->block_info, fin, bfile->block_start, bfile->bits_shifted, block_crc, verbose);
    return(offset);
  }
  else {
    fprintf(stderr,"Failed to find the next block marker due to some error\n");
    exit(-1);
  }
}

void show_stream_crc(bz_info_t *bfile, int fin, int verbose) {
  /*
    find the stream crc from the bzip2 footer at the
    end of the file and display it
  */
  int bits_shifted = 0;
  uint64_t stream_crc = (uint64_t)0;
  unsigned char buffer[12];
  int ind = 0;

  bfile->footer = init_footer();
  bits_shifted = check_file_for_footer(fin, bfile);
  if (bits_shifted == -1) {
    fprintf(stderr, "failed to find bz2 footer\n");
    exit(1);
  }
  read_footer(buffer, fin);
  if (verbose)
    fprintf(stdout, "buffer: %02x %02x %02x %02x %02x\n", buffer[6], buffer[7], buffer[8], buffer[9], buffer[10]);

  if (bits_shifted)
    ind = 6;
  else
    ind = 7;
  stream_crc += (uint64_t) buffer[ind++] & bit_mask(8 - bits_shifted, MASKRIGHT);
  stream_crc = stream_crc << 8;
  stream_crc += (uint64_t) buffer[ind++];
  stream_crc = stream_crc << 8;
  stream_crc += (uint64_t) buffer[ind++];
  stream_crc = stream_crc << 8;
  stream_crc += (uint64_t) buffer[ind++];
  if (bits_shifted) {
    if (verbose)
      fprintf(stdout, "stream_crc bits shifted by %d\n", bits_shifted);
    stream_crc = stream_crc << bits_shifted;
    stream_crc += (uint64_t) (buffer[ind++] & bit_mask(bits_shifted, MASKLEFT)) >> (8 - bits_shifted);
  }
  stream_crc &= 0xffffffff;
  fprintf(stdout, "extracted_stream_CRC:0x%lx\n", stream_crc);
}

int main(int argc, char **argv) {
  int fin;
  char *filename = NULL;
  int verbose = 0;
  int optindex=0;
  int optc;
  bz_info_t bfile;
  off_t offset = (off_t)0;
  off_t filesize = (off_t)0;
  uint64_t block_crc = 0u;
  uint64_t computed_cumul_crc = 0u;

  struct option optvalues[] = {
    {"filename", 1, 0, 'f'},
    {"help", 0, 0, 'h'},
    {"verbose", 0, 0, 'v'},
    {"version", 0, 0, 'V'},
    {NULL, 0, NULL, 0}
  };

  while (1) {
    optc=getopt_long_only(argc,argv,"f:hvV", optvalues, &optindex);
    if (optc=='f') {
     filename=optarg;
    }
    else if (optc=='h')
      usage(NULL);
    else if (optc=='v')
      verbose++;
    else if (optc=='V')
      show_version(VERSION);
    else if (optc==-1) break;
    else usage("Unknown option or other error\n");
  }

  if (! filename) {
    usage(NULL);
  }

  fin = open (filename, O_RDONLY);
  if (fin < 0) {
    fprintf(stderr,"Failed to open file %s for read\n", filename);
    exit(1);
  }

  bfile.initialized = 0;
  bfile.marker = NULL;

  init_bz2_info(&bfile, fin);
  filesize = get_file_size(fin);

  while (1) {
    offset = do_next_block(&bfile, fin, offset, &block_crc, filesize, verbose);
    if (!offset)
      break;
    offset += (off_t)1;
    if (verbose) {
      fprintf(stdout, "1's complement of block crc: 0x%lx\n", block_crc ^ 0xffffffff);
      fprintf(stderr, "current cumul crc: 0x%lx, ", computed_cumul_crc);
    }
    computed_cumul_crc = combine_crc(computed_cumul_crc, (block_crc ^ 0xffffffff));
    computed_cumul_crc &= 0xffffffff;
    if (verbose)
      fprintf(stderr, " NEW cumul crc: 0x%lx\n", computed_cumul_crc);
  }
  computed_cumul_crc &= 0xffffffff;
  fprintf(stdout, "computed_stream_CRC:0x%lx\n", computed_cumul_crc);
  show_stream_crc(&bfile, fin, verbose);
  exit(0);
}
