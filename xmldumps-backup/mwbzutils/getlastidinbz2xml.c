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
"Usage: getlastidinbz2xml --filename file --type type [--verbose]\n"
"       [--help] [--version]\n\n"
"Show the last page or rev id in the specified MediaWiki XML dump file.\n"
"This assumes that the last bz2 block(s) of the file are intact.\n"
"Exits with 0 in success, -1 on error.\n\n"
"Options:\n\n"
"  -f, --filename   name of file to search\n"
"  -t, --type       type of id to find: 'page' or 'rev'\n"
"  -v, --verbose    show search process; specify multiple times for more output\n"
"  -h, --help       Show this help message\n"
"  -V, --version    Display the version of this program and exit\n\n"
"Report bugs in getlastidinbz2xml to <https://phabricator.wikimedia.org/>.\n\n"
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
  fprintf(stderr,"getlastidinbz2xml %s\n", version_string);
  fprintf(stderr,"%s",copyright);
  exit(-1);
}

/*
 if any id of the specified type is found, appropriate updates will be made to id_info
 no updates are made to the buffer about consumed data, the caller
 is responsible
 */
void find_last_id_in_buffer(buf_info_t *buffer, id_info_t *id_info,
			    bz_info_t *bfile, char *type, int verbose) {
  regmatch_t *match_id;
  regex_t compiled_id;
  char *page_id_pattern = "<page>\n[ ]+<title>[^<]+</title>\n([ ]+<ns>[0-9]+</ns>\n)?[ ]+<id>([0-9]+)</id>\n";
  char *rev_id_pattern = "<revision>\n[ ]+<id>([0-9]+)</id>\n";

  char *match_from ;
  int index;

  if (buffer_is_empty(buffer)) return;

  match_id = (regmatch_t *)malloc(sizeof(regmatch_t)*3);
  match_from = (char *)buffer->next_to_read;

  if (! strcmp(type, "rev")) {
    index = 1;
    regcomp(&compiled_id, rev_id_pattern, REG_EXTENDED);
  }
  else if (! strcmp(type, "page")) {
    index = 2;
    regcomp(&compiled_id, page_id_pattern, REG_EXTENDED);
  }
  else {
    fprintf(stderr, "unknown type of tag to find, %s, giving up\n", type);
    exit(-1);
  }

  while (regexec(&compiled_id, match_from, 3, match_id, 0) == 0) {
    /* found one, yay */
    if (match_id[index].rm_so >=0) {
        id_info->id = atoi((char *)(match_from +match_id[index].rm_so));
        id_info->position = bfile->block_start;
        id_info->bits_shifted = bfile->bits_shifted;
	/* get ready to search rest of buffer */
	match_from += match_id[0].rm_eo;
    }
    else {
      /* should never happen */
      fprintf(stderr,"regex gone bad...\n");
      exit(-1);
    }
  }
  free(match_id);
  regfree(&compiled_id);
  return;
}


void init_id_info(id_info_t *id_info) {
  id_info->bits_shifted = -1;
  id_info->position = (off_t)-1;
  id_info->id = -1;
  return;
}

/*
   get the last page or rev id after position in file
   expect position to be the start of a bz2 block
   if an id tag is found, the structure id_info will be updated accordingly
   returns:
      1 if an id tag found,
      0 if no id tag found,
      -1 on error
*/
int get_last_id_after_offset(int fin, id_info_t *id_info,
			     bz_info_t *bfile, off_t upto,
			     char *type, int verbose) {
  int length=5000; /* output buffer size */

  buf_info_t *b;
  const int KEEP = 310;

  b = init_buffer(length);
  init_id_info(id_info);

    /* try to fill the buffer, unless of course we hit eof */
    /* could be a case where they read no bytes, more bytes are avail in buffer,
       we hit eof. what then? */
    /* while ((res = get_buffer_of_uncompressed_data(b, fin, bfile, FORWARD) >=0) && (! bfile->eof)) { */
    /* while (!get_buffer_of_uncompressed_data(b, fin, bfile, FORWARD) && (! bfile->eof)) { */


    while (get_buffer_of_uncompressed_data(b, fin, bfile, FORWARD) >= 0 && (! bfile -> eof)) {
      find_last_id_in_buffer(b, id_info, bfile, type, verbose);
      /* did we hit eof? then th-th-that's all folks */
      if (bfile->eof)
	break;

      /*
	We keep reading more buffers because we want the _last_ page/rev id,
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
	   We keep that much in case somewhere near the end was a page/rev
	   tag or a page/rev id tag that got cut off in the middle.
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
      find_last_id_in_buffer(b, id_info, bfile, type, verbose);
      BZ2_bzDecompressEnd(&(bfile->strm));
      free_buffer(b);
      free(b);
      if (id_info->id == -1) return 0; /* not found */
      else if (id_info->id > 0) return 1; /* found */
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
  fprintf(stderr,"Failed to find any id tags in file, exiting\n");
  close(fin);
  exit(1);
}

int main(int argc, char **argv) {
  int fin, res, id=0;
  off_t block_end, block_start, upto;
  id_info_t id_info;
  char *filename = NULL;
  char *type = NULL;
  int optindex=0;
  bz_info_t bfile;
  int verbose = 0;
  int optc;
  int result;

  struct option optvalues[] = {
    {"help", 0, 0, 'h'},
    {"filename", 1, 0, 'f'},
    {"type", 1, 0, 't'},
    {"verbose", 0, 0, 'v'},
    {"version", 0, 0, 'V'},
    {NULL, 0, NULL, 0}
  };

  while (1) {
    optc = getopt_long_only(argc,argv,"f:hvV", optvalues, &optindex);
    if (optc=='f') {
     filename=optarg;
    }
    else if (optc == 't') {
     type = optarg;
    }
    else if (optc == 'h')
      usage(NULL);
    else if (optc == 'v')
      verbose++;
    else if (optc == 'V')
      show_version(VERSION);
    else if (optc == -1) break;
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
  bfile.header_read = 0;

  /* start at end of file */
  block_end = bfile.position;
  upto = block_end;

  block_start = (off_t)-1;
  id = 0;

  while (!id) {
    bfile.initialized = 0;
    init_decompress(&bfile);

    block_start = find_first_bz2_block_from_offset(&bfile, fin, block_end, BACKWARD, bfile.file_size, 1);

    if (block_start <= (off_t) 0) giveup(fin);
    BZ2_bzDecompressEnd (&(bfile.strm));

    res = get_last_id_after_offset(fin, &id_info, &bfile, upto, type, verbose);
    if (res > 0) {
      id = id_info.id;
    }
    else {
      upto = block_end;
      block_end = block_start - (off_t) 1;
      if (block_end <= (off_t) 0) giveup(fin);
    }
    BZ2_bzDecompressEnd (&(bfile.strm));
  }
  if (!id) giveup(fin);

  fprintf(stdout, "%s_id:%d\n", type, id);
  close(fin);
  exit(0);
}
