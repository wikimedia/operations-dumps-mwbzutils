#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <getopt.h>
#include "bzlib.h"
/* needed for EState which lets us get at the
   combined crc of the bz2 stream struct */
#include "bzlib_private.h"

/*
 * bz2compress contents from a file or from stdin,
 * without bz2 header and using combined crc provided
 * via command line arg, to generate contents that
 * could be appended to an incomplete file which
 * has that same combined crc.
 *
 * use case:
 *
 * given a partial xml page content dump file,
 * manually find the last block before a
 * block starting the last incomplete page;
 * split the file into two pieces, one containing
 * everything up through that block, the other containing
 * the rest;
 * uncompress "the rest" and keep everything through the
 * <?page> tag, appending </mediawiki> afterwards;
 * get the combined crc of all blocks in the first piece;
 * give the uncompressed cleaned up xml from the second piece,
 * plus the combined crc from the first piece, to this
 * util, save the output in a third piece;
 * cat the first and third pieces together to have a complete
 * file, leaving only the pages after this file for
 * a rerun of the page content dump
 */

void usage(char *message) {
  char * help =
"Usage: appendbz2 --outfile |--help\n"
"Given a combined crc and text to stdin, bz2compress the text\n"
"and write the contents to a file without a BZ2 header, such\n"
"that it could be appended to a partial bzipped file with blocks\n"
"that have the same combined crc.\n"
"Exits with 0 on success, 1 on error.\n\n"
"Options:\n\n"
"  -b, --bufsize     size of input and output buffers\n"
"  -c, --crc         combinedcrc\n"
"  -o, --outfile     name of file in which to write compressed data\n\n"
"Flags:\n\n"
"  -v, --verbose     print the state of the bz2 stream buffer often\n"
"  -V, --version    Display the version of this program and exit\n\n"
"  -h, --help        Show this help message\n\n"
"Report bugs in appendbz2 to <https://phabricator.wikimedia.org/>.\n\n"
"See also checkforbz2footer(1), dumplastbz2block(1), findpageidinbz2xml(1),\n"
    "recompressxml(1), writeuptopageid(1)\n\n";
  if (message) {
    fprintf(stderr,"%s\n\n",message);
  }
  fprintf(stderr,"%s",help);
  exit(-1);
}

void show_version(char *version_string) {
  char * copyright =
"Copyright (C) 2020 Ariel T. Glenn.  All rights reserved.\n\n"
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
  fprintf(stderr,"appendbz2 %s\n", version_string);
  fprintf(stderr,"%s",copyright);
  exit(-1);
}

int bz2compr(bz_stream *bz2_strm, char *inbuf, char *outbuf, unsigned int bufsize,
	     int bz_action, int fout, int verbose) {
  int bz2_eof = 0;
  int res;

  /* display_strm(bz2_strm, inbuf, outbuf, verbose); */
  res = BZ2_bzCompress(bz2_strm, bz_action);

  if (bz2_strm->avail_out != bufsize) {
    write(fout, outbuf, bufsize - bz2_strm->avail_out);
    bz2_strm->next_out = outbuf;
    bz2_strm->avail_out = bufsize;
  }
  if (res == BZ_STREAM_END) {
    res = BZ2_bzCompressEnd(bz2_strm);
    if (res != BZ_OK) {
      fprintf(stderr, "failed to free bz2 stream resources\n");
    }
    bz2_eof = 1;
  }
  return bz2_eof;
}

int compress(unsigned int crc, char *outfilename, int bufsize, int verbose) {
  bz_stream bz2_strm;
  int bz2_blocksize = 9;
  int bz2_verbosity = 0;
  int bz2_workfactor = 0;

  EState * stream_state = NULL;

  char *inbuf = NULL;
  char *outbuf = NULL;

  int input_eof = 0;
  int bz2_eof = 0;
  int fin;
  int fout;
  int bytes_read;

  int bz_action = BZ_RUN;

  inbuf = (char *)malloc((size_t)bufsize);
  if (inbuf == NULL) {
    fprintf(stderr,"failed to allocate %d for input buffer\n", bufsize);
    return(1);
  }
  outbuf = (char *)malloc((size_t)bufsize);
  if (outbuf == NULL) {
    fprintf(stderr,"failed to allocate %d for output buffer\n", bufsize);
    return(1);
  }
  fin = 0;
  fout = open(outfilename, O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if (fout == -1) {
    fprintf(stderr, "failed to open output file %s\n", outfilename);
    return(1);
  }

  /* init the bz_stream structure; these mean use regular malloc/free */
  bz2_strm.bzalloc = NULL;
  bz2_strm.bzfree = NULL;
  bz2_strm.opaque = NULL;

  BZ2_bzCompressInit(&bz2_strm, bz2_blocksize, bz2_verbosity, bz2_workfactor);

  /* next unread byte of input,
     may be updated by bz2 lib to location after end of input buffer */
  bz2_strm.next_in = inbuf;
  /* num plaintext bytes unread */
  bz2_strm.avail_in = 0;
  /* next address where compressed bytes are to be placed,
     may be updated by bz2 lib to location after end of output buffer */
  bz2_strm.next_out = outbuf;
  /* num bytes available in which to place compressed data */
  bz2_strm.avail_out = bufsize;

  stream_state = bz2_strm.state;
  if (stream_state == NULL) {
    fprintf(stderr, "Memory issue most likely, bz2 stream state is NULL, giving up\n");
    return(1);
  }
  /* shove in our combined crc from the file we want to extend... */
  stream_state->combinedCRC = crc;
  /* lie about our block number so we skip writing the BZ2 header */
  stream_state->blockNo = 2;
  /* stream_state->verbosity = 2; */
  /* process all the input */
  while (!input_eof) {
    /* display_strm(&bz2_strm, inbuf, outbuf, verbose); */
    if (!bz2_strm.avail_in) {
      /* all the buffer is used, start over */
      bz2_strm.next_in = inbuf;
      bytes_read = read(fin, bz2_strm.next_in, bufsize - bz2_strm.avail_in);
      if (! bytes_read) {
	/* no bytes read, no updates of bz2_strm needed */
	input_eof = 1;
	break;
      }
      else {
	/* input avail to bz2: what we read plus what it left from
	   the previous decompression */
	bz2_strm.avail_in += bytes_read;
      }
    }
    bz2_eof = bz2compr(&bz2_strm, inbuf, outbuf, (unsigned int) bufsize,
		       bz_action, fout, verbose);
  }
  /* process any data left in bz2 internal compression buffers */
  bz_action = BZ_FINISH;
  while (! bz2_eof) {
    bz2_eof = bz2compr(&bz2_strm, inbuf, outbuf, bufsize, bz_action, fout, verbose);
  }
  return(0);
}

int main(int argc, char **argv) {
  char *outfile = NULL;
  unsigned int combined_crc = 0;
  int verbose = 0;
  int bufsize = 4096;
  int res = 0;

  int optc;
  int optindex = 0;

  struct option optvalues[] = {
    {"help", 0, 0, 'h'},
    {"crc", 1, 0, 'c'},
    {"outfile", 1, 0, 'o'},
    {"bufsize", 1, 0, 'b'},
    {"verbose", 0, 0, 'v'},
    {"version", 0, 0, 'V'},
    {NULL, 0, NULL, 0}
  };

  while (1) {
    optc=getopt_long_only(argc,argv,"c:o:b:hvV", optvalues, &optindex);
    if (optc == 'h')
      usage(NULL);
    else if (optc == 'v')
      verbose++;
    else if (optc=='V')
      show_version(VERSION);
    else if (optc == 'c')
      combined_crc = strtoul(optarg, NULL, 10);
    else if (optc == 'o')
      outfile = optarg;
    else if (optc == 'b')
      bufsize = strtol(optarg, NULL, 10);
    else if (optc == -1) break;
    else usage("Unknown option or other error\n");
  }

  if (combined_crc == 0) {
    usage("Missing crc argument.");
  }
  if (outfile == NULL) {
    usage("Missing outfile argument.");
  }
  res = compress(combined_crc, outfile, bufsize, verbose);
  exit(res);
}
