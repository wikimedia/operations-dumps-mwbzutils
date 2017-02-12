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

void usage(char *message) {
  char * help =
"Usage: getlastpageidinbz2xml --filename file [--verbose]\n"
"       [--help] [--version]\n\n"
"Show the last page id in the specified MediaWiki XML dump file.\n"
"This assumes that the last bz2 block(s) of the file are intact.\n"
"Exits with 0 in success, -1 on error.\n\n"
"Options:\n\n"
"  -f, --filename   name of file to search\n"
"  -v, --verbose    show search process; specify multiple times for more output\n"
"  -h, --help       Show this help message\n"
"  -V, --version    Display the version of this program and exit\n\n"
"Report bugs in getlastpageidinbz2xml to <https://phabricator.wikimedia.org/>.\n\n"
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
"Copyright (C) 2017 Ariel T. Glenn.  All rights reserved.\n\n"
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
  fprintf(stderr,"getlastpageidinbz2xml %s\n", version_string);
  fprintf(stderr,"%s",copyright);
  exit(-1);
}

/*
 if any page id is found, appropriate updates will be made to pinfo
 no updates are made to the buffer about consumed data, the caller
 is responsible
 */
void find_last_pageid_in_buffer(buf_info_t *buffer, page_info_t *pinfo,
				bz_info_t *bfile, int verbose) {
  regmatch_t *match_page_id;
  regex_t compiled_page_id;

  char *page_id = "<page>\n[ ]+<title>[^<]+</title>\n([ ]+<ns>[0-9]+</ns>\n)?[ ]+<id>([0-9]+)</id>\n"; 

  char *match_from ;

  regcomp(&compiled_page_id, page_id, REG_EXTENDED);
  match_page_id = (regmatch_t *)malloc(sizeof(regmatch_t)*3);

  if (buffer_is_empty(buffer)) return;

  match_from = (char *)buffer->next_to_read;
  while (regexec(&compiled_page_id, match_from, 3, match_page_id, 0) == 0) {
    /* found one, yay */
    if (match_page_id[2].rm_so >=0) {
        pinfo->page_id = atoi((char *)(match_from +match_page_id[2].rm_so));
        pinfo->position = bfile->block_start;
        pinfo->bits_shifted = bfile->bits_shifted;
	/* get ready to search rest of buffer */
	match_from += match_page_id[0].rm_eo;
    }
    else {
      /* should never happen */
      fprintf(stderr,"regex gone bad...\n"); 
      exit(-1);
    }
  }
  free(match_page_id);
  regfree(&compiled_page_id);
  return;
}


void init_pinfo(page_info_t *pinfo) {
  pinfo->bits_shifted = -1;
  pinfo->position = (off_t)-1;
  pinfo->page_id = -1;
  return;
}

/* 
   get the last page id after position in file 
   expect position to be the start of a bz2 block
   if a pageid is found, the structure pinfo will be updated accordingly
   returns:
      1 if a pageid found,
      0 if no pageid found,
      -1 on error
*/
int get_last_page_id_after_offset(int fin, page_info_t *pinfo,
				  bz_info_t *bfile, off_t upto, int verbose) {
  int length=5000; /* output buffer size */

  buf_info_t *b;
  const int KEEP = 310;

  b = init_buffer(length);
  init_pinfo(pinfo);

    /* try to fill the buffer, unless of course we hit eof */
    /* could be a case where they read no bytes, more bytes are avail in buffer,
       we hit eof. what then? */
    /* while ((res = get_buffer_of_uncompressed_data(b, fin, bfile, FORWARD) >=0) && (! bfile->eof)) { */
    /* while (!get_buffer_of_uncompressed_data(b, fin, bfile, FORWARD) && (! bfile->eof)) { */


    while (get_buffer_of_uncompressed_data(b, fin, bfile, FORWARD) >= 0 && (! bfile -> eof)) {
      find_last_pageid_in_buffer(b, pinfo, bfile, verbose);
      /* did we hit eof? then th-th-that's all folks */
      if (bfile->eof)
	break;

      /*
	We keep reading more buffers because we want the _last_ pageid,
	not the first one
      */
      else if (buffer_is_empty(b)) {
	/* entire buffer is now available for next read */
	bfile->strm.next_out = (char *)b->buffer;
	bfile->strm.avail_out = bfile->bufout_size;
	b->next_to_fill = b->buffer;
      }
      else if (b->bytes_avail> KEEP) {
	/* dump contents of buffer except last KEEP chars,
	   move those to front so we can keep reading.
	   We keep that much in case somewhere near the end was a page
	   tag or a page id tag that got cut off in the middle.
	*/
	move_bytes_to_buffer_start(b, b->end - KEEP, KEEP);
	bfile->strm.next_out = (char *)b->next_to_fill;
	bfile->strm.avail_out = b->end - b->next_to_fill;
      }
      else {
	/* move available bytes (don't have KEEP) up to front */
	move_bytes_to_buffer_start(b, b->next_to_read, b->bytes_avail);
	bfile->strm.next_out = (char *)b->next_to_fill;
	bfile->strm.avail_out = b->end - b->next_to_fill;
      }
      if (bfile->position > upto) {
	/* we're done */
	break;
      }
    }
    if (bfile->eof || bfile->position > upto) {
    /* see what's left in the buffer after eof. maybe we got something good */
      find_last_pageid_in_buffer(b, pinfo, bfile, verbose);
      BZ2_bzDecompressEnd(&(bfile->strm));
      free_buffer(b);
      free(b);
      if (pinfo->page_id == -1) return 0; /* not found */
      else if (pinfo->page_id > 0) return 1; /* found */
      else return(-1); /* error */
    }
    else {
      /* we have an error from get_buffer_of_uncompressed_data */
      BZ2_bzDecompressEnd(&(bfile->strm));
      free_buffer(b);
      free(b);
      fprintf(stderr,"freed buffer\n");
      return(-1); /* error */
    }
}


int giveup(int fin) {
  fprintf(stderr,"Failed to find any page ids in file, exiting\n");
  close(fin);
  exit(1);
}

int main(int argc, char **argv) {
  int fin, res, page_id=0;
  off_t block_end, block_start, upto;
  page_info_t pinfo;
  char *filename = NULL;
  int optindex=0;
  bz_info_t bfile;
  int verbose = 0;
  int optc;
  int result;

  struct option optvalues[] = {
    {"filename", 1, 0, 'f'},
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

  bfile.file_size = get_file_size(fin);
  bfile.footer = init_footer();
  bfile.marker = init_marker();
  result = check_file_for_footer(fin, &bfile);
  if (result == -1) {
    bfile.position = bfile.file_size;
  }
  else {
    bfile.position = bfile.file_size - (off_t)11; /* size of footer, perhaps with 1 byte extra */
  }
  bfile.position -=(off_t)6; /* size of marker */
  bfile.initialized = 0;
  bfile.bytes_read = 0;

  /* start at end of file */
  block_end = bfile.position;
  upto = block_end;

  block_start = (off_t)-1;
  page_id = 0;

  while (!page_id) {
    bfile.initialized = 0;
    /* calling this explicitly without setting bfile.initialized to 0 above, does not fix problem,
       we get the -2 param errors again */

    /* this init does not malloc anything */
    init_decompress(&bfile);

    /* this calls init_decompress which calls BZ2_bzDecompressInit which sets strm->s and then strm->s->strm = strm 
    but it must not do it every time, when I add the above then the initialize works, why?? */
    block_start = find_first_bz2_block_from_offset(&bfile, fin, block_end, BACKWARD);
    
    if (block_start <= (off_t) 0) giveup(fin);
    /* this calls get_buffer_of_uncompressed_data which calls get_and_decompress_data which COULD
       call init_bz2_file, let's see if it does or not... not the second time!  let's force it*/
    BZ2_bzDecompressEnd (&(bfile.strm));

    res = get_last_page_id_after_offset(fin, &pinfo, &bfile, upto, verbose);
    if (res > 0) {
      page_id = pinfo.page_id;
    }
    else {
      /* look for previous block */
      /* FIXME this must be broken somehow. */
      upto = block_end;
      block_end = block_start - (off_t) 1;
      if (block_end <= (off_t) 0) giveup(fin);
    }
    /* ths seems not to free enough stuff, check around. we have a leak */
    /* even after adding this above we stil have the same leak wtf*/
    BZ2_bzDecompressEnd (&(bfile.strm));
  }
  if (!page_id) giveup(fin);

  fprintf(stderr, "page_id:%d\n", page_id);
  close(fin);
  exit(0);
}  
