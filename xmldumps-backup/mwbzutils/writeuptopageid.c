#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include <bzlib.h>
#include <zlib.h>

typedef enum { NoWrite, StartHeader, EndHeader, StartPage, AtPageID, WriteMem, Write, EndPage, AtLastPageID } States;

/* assume the header is never going to be longer than 1000 x 80 4-byte characters... how many
   namespaces will one project want? */
#define MAXHEADERLEN 524289

void usage(char *message) {
  char * help =
"Usage: writeuptopageid [--version|--help]\n"
"   or: writeuptopageid [--inpath <path>] <startpageid> <endpageid>\n"
"   or: writeuptopageid [--inpath <path>] --odir <path> --fspecs <filespec>[;<filespec>...]\n\n"
"Reads a MediaWiki XML file from the specified path or from stdin and writes a range\n"
"of pages from the file to stdout, starting with and including the startpageid, up to\n"
"but not including the endpageid.\n"
"This program can be used in processing XML dump files that were only partially\n"
"written, as well as in writing partial stub files for reruns of those dump files.\n"
"If endPageID is ommitted, all pages starting from startPageID will be copied.\n\n"
"Options:\n\n"
"  -i   --inpath    full path to input file; if omitted, stdin will be used instead;\n"
"                   if input file ends in .gz or .bz2 the appropriate decompression\n"
"                   will be used\n"
"  -o,  --odir      directory in which to write output file(s)\n"
"  -f,  --fspecs    series of output file specifications\n"
"                   each file spec consists of:\n"
"                   filename:startpage:lastpage\n"
"                   where lastpage may be omitted in the last file spec; it is\n"
"                   presumed both that the start and end page id ranges are in\n"
"                   in ascending order, and that there is no overlap in the ranges.\n"
"                   You can think of this as a lazy person's splitxml.\n"
"Flags:\n\n"
"  -h, --help       Show this help message\n"
"  -v, --version    Display the version of this program and exit\n\n"
"Arguments:\n\n"
"  <startpageid>   id of the first page to write\n"
"  <endpageid>     id of the page at which to stop writing; if omitted, all pages through eof\n"
"                   will be written\n\n"
"Report bugs in writeuptopageid to <https://phabricator.wikimedia.org/>.\n\n"
"See also checkforbz2footer(1), dumpbz2filefromoffset(1), dumplastbz2block(1),\n"
    "findpageidinbz2xml(1), recompressxml(1)\n\n";
 if (message) {
   fprintf(stderr,"%s\n\n",message);
 }
 fprintf(stderr,"%s",help);
 exit(-1);
}


void show_version(char *version_string) {
  char * copyright =
"Copyright (C) 2011, 2012, 2013 Ariel T. Glenn.\n"
"Copyright (C) 2015 John Vandenberg.\n"
"Copyright (C) 2017 Ariel T. Glenn.\n"
"Copyright (C) 2018 Marius Hoch, Ariel T. Glenn.  All rights reserved.\n\n"
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
  fprintf(stderr,"writeuptopageid %s\n", version_string);
  fprintf(stderr,"%s",copyright);
  exit(-1);
}

typedef struct {
  char buf[65536];
  int nextin;      /* pointer to next byte available for reading stuff in from file */
  int nextout;     /* pointer to next byte available for consumption by caller */
  int bytes_avail; /* number of bytes avail for consumption */
} bz2buffer_t;

typedef struct {
  char *path;
  FILE *fin;

  BZFILE *bzstream;
  gzFile gzstream;
  int gz_bufsize;

  int bzerror;
  int bz_verbosity;
  int bz_small;
  char *bz_unused;
  int bz_nUnused;
  bz2buffer_t *bz_buffer;

  int (*open)();
  char *(*fgets)();
  int (*close)();
} InputHandler;

int bz2_open_i(InputHandler *ih);
char *bz2_fgets_i(InputHandler *ih, char *buffer, int bytecount);
int bz2_close_i(InputHandler *ih);

int gz_open_i(InputHandler *ih);
char *gz_fgets_i(InputHandler *ih, char *buffer, int bytecount);
int gz_close_i(InputHandler *ih);

int txt_open_i(InputHandler *ih);
char *txt_fgets_i(InputHandler *ih, char *buffer, int bytecount);
int txt_close_i(InputHandler *ih);

void free_bz2buf(bz2buffer_t *b) {
  if (b) free(b);
  return;
}

bz2buffer_t *init_bz2buf() {
  bz2buffer_t *buf;

  buf = (bz2buffer_t *) malloc(sizeof(bz2buffer_t));
  if (!buf) {
    fprintf(stderr,"failed to get memory for bz2 input buffer\n");
    return(NULL);
  }
  buf->nextin = buf->nextout = buf->bytes_avail = 0;
  return(buf);
}

InputHandler *inputhandler_init(char *path) {
  InputHandler *ih = NULL;
  ih = (InputHandler *)malloc(sizeof(InputHandler));
  if (ih == NULL) {
    fprintf(stderr, "failed to allocate input handler\n");
    exit(1);
  }
  ih->path = path;
  ih->fin = NULL;
  ih->bz_buffer = init_bz2buf();
  ih->bzstream = NULL;
  ih->bzerror = 0;
  ih->bz_verbosity = 0;  /* no extra messages */
  ih->bz_small = 0;      /* don't try to save memory */
  ih->bz_unused = NULL;
  ih->bz_nUnused = 0;

  ih->gzstream = NULL;
  ih->gz_bufsize = 65536;

  if (path == NULL) {
    ih->fin = stdin;
    ih->open = NULL;
    ih->fgets = txt_fgets_i;
    ih->close = NULL;
    return(ih);
  }

  if (strlen(path) > 4 && !strcmp(path + strlen(path) - 4, ".bz2")) {
    ih->open = bz2_open_i;
    ih->fgets = bz2_fgets_i;
    ih->close = bz2_close_i;
  }
  else if (strlen(path) > 3 && !strcmp(path + strlen(path) - 3, ".gz")) {
    ih->open = gz_open_i;
    ih->fgets = gz_fgets_i;
    ih->close = gz_close_i;
  }
  else {
    ih->open = txt_open_i;
    ih->fgets = txt_fgets_i;
    ih->close = txt_close_i;
  }
  return(ih);
}

int isfull(bz2buffer_t *buf) {
  if (buf->nextin == sizeof(buf->buf)) return(1);
  else return(0);
}

void fill_buffer(bz2buffer_t *buf, BZFILE *fd) {
  int result;

  if (isfull(buf)) return;
  result = BZ2_bzread(fd, buf->buf + buf->nextin, sizeof(buf->buf) - buf->nextin);
  if (result) {
    buf->nextin += result;
    buf->bytes_avail += result;
  }
  return;
}

int has_newline(bz2buffer_t *buf) {
  int ind = 0;
  while (ind < buf->bytes_avail) {
    if (buf->buf[buf->nextout+ind] == '\n') return(ind+1);
    ind++;
  }
  return(-1);
}

/*
  returns:
    pointer to the output buffer if any data was read and copied
    NULL otherwise

  this function will read one line of output from file and copy it
  into out, at most out_size -1 bytes are copied, a '\0' will be placed at the end,
  if no input is copied the holder will contain the empty string
*/
char *bz2gets(BZFILE *fd, bz2buffer_t *buf, char *out, int out_size) {
  int newline_ind = -1;
  int out_ind = 0;
  int out_space_remaining = out_size -1;

  out[0]='\0';
  if (!buf->bytes_avail) fill_buffer(buf, fd);
  if (!buf->bytes_avail) {
    return(0);
  }

  while (((newline_ind = has_newline(buf)) == -1) && (out_space_remaining > buf->bytes_avail)) {
    strncpy(out+out_ind, buf->buf + buf->nextout, buf->bytes_avail);
    out_ind += buf->bytes_avail;
    out[out_ind] = '\0';
    out_space_remaining -= buf->bytes_avail;
    buf->nextout = buf->nextin = buf->bytes_avail = 0;
    fill_buffer(buf, fd);
    if (!buf->bytes_avail) {
      out[out_ind] = '\0';
      if (out_ind) return(out);
      else return(NULL);
    }
  }
  if (out_space_remaining) {
    if (newline_ind >=0 && newline_ind < out_space_remaining) {
      strncpy(out+out_ind, buf->buf + buf->nextout, newline_ind);
      out_ind += newline_ind;
      out[out_ind] = '\0';
      buf->nextout += newline_ind;
      buf->bytes_avail -= (newline_ind);
    }
    else {
      strncpy(out+out_ind, buf->buf + buf->nextout, out_space_remaining);
      out_ind+= out_space_remaining;
      out[out_ind] = '\0';
      buf->nextout += out_space_remaining;
      buf->bytes_avail -= out_space_remaining;
    }
    /* if the buffer is empty set things up correctly for that case */
    if (buf->nextout == sizeof(buf->buf) && !buf->bytes_avail) {
      buf->nextout = 0;
      buf->nextin = 0;
    }
  }
  out[out_ind] = '\0';
  if (!out_ind) return(NULL);
  else return(out);
}

int bz2_open_i(InputHandler *ih) {
  if (ih->path != NULL) {
    ih->fin = fopen(ih->path, "rb");
    /* fixme check if successfull */
  }
  ih->bzstream = BZ2_bzReadOpen(&(ih->bzerror), ih->fin, ih->bz_verbosity, ih->bz_small,
                                ih->bz_unused, ih->bz_nUnused);
  if (ih->bzerror != BZ_OK) {
    fprintf(stderr, "error %d trying to open %s for decompression\n",
            ih->bzerror, ih->path);
    exit(-1);
  }
  return(1);
}

char *bz2_fgets_i(InputHandler *ih, char *buffer, int bytecount) {
  char *ret = NULL;

  ret = bz2gets(ih->bzstream, ih->bz_buffer, buffer, bytecount);
  if (ret == NULL && ih->bzerror != BZ_OK) {
    fprintf(stderr, "error %d trying to read from %s\n",
            ih->bzerror, ih->path);
    return(NULL);
  }
  return(ret);
}

int bz2_close_i(InputHandler *ih) {
  BZ2_bzReadClose(&(ih->bzerror), ih->bzstream);
  if (ih->fin != stdout)
    fclose(ih->fin);
  return(0);
}

int gz_open_i(InputHandler *ih) {
  if (ih->path != NULL) {
    ih->gzstream = gzopen(ih->path, "rb");
    gzbuffer(ih->gzstream, ih->gz_bufsize);
  }
  return(0);
}

char *gz_fgets_i(InputHandler *ih, char *buffer, int bytecount) {
  return(gzgets(ih->gzstream, buffer, bytecount));
}

int gz_close_i(InputHandler *ih) {
  if (ih->path != NULL)
    gzclose(ih->gzstream);
  return(0);
}

int txt_open_i(InputHandler *ih) {
  if (ih->path != NULL)
    ih->fin = fopen(ih->path, "r");
  return(0);
}

char *txt_fgets_i(InputHandler *ih, char *buffer, int bytecount) {
  return(fgets(buffer, bytecount, ih->fin));
}

int txt_close_i(InputHandler *ih) {
  if (ih->path != NULL)
    fclose(ih->fin);
  return(0);
}

typedef struct {
  FILE *fin;
  char *path;
  FILE *fout;
  BZFILE *bzstream;
  gzFile gzstream;
  int gz_bufsize;

  int bzerror;
  int bz_blocksize;
  int bz_verbosity;
  int bz_workfactor;

  int (*open)();
  int (*write)();
  int (*close)();
} OutputHandler;

int bz2_open_o(OutputHandler *oh);
int bz2_write_o(OutputHandler *oh, char *buffer, int bytecount);
int bz2_close_o(OutputHandler *oh);

int gz_open_o(OutputHandler *oh);
int gz_write_o(OutputHandler *oh, char *buffer, int bytecount);
int gz_close_o(OutputHandler *oh);

int txt_open_o(OutputHandler *oh);
int txt_write_o(OutputHandler *oh, char *buffer, int bytecount);
int txt_close_o(OutputHandler *oh);

OutputHandler *outputhandler_init(char *path) {
  OutputHandler *oh = NULL;
  oh = (OutputHandler *)malloc(sizeof(OutputHandler));
  if (oh == NULL) {
    fprintf(stderr, "failed to allocate output handler\n");
    exit(1);
  }
  oh->path = path;
  oh->fout = NULL;
  oh->bzstream = NULL;
  oh->bzerror = 0;
  oh->bz_blocksize = 9;  /* 900k */
  oh->bz_verbosity = 0;  /* no extra messages */
  oh->bz_workfactor = 0; /* use the default */

  oh->gzstream = NULL;
  oh->gz_bufsize = 65536;

  if (path == NULL) {
    oh->fout = stdout;
    oh->open = NULL;
    oh->write = txt_write_o;
    oh->close = NULL;
    return(oh);
  }

  if (strlen(path) > 4 && !strcmp(path + strlen(path) - 4, ".bz2")) {
    oh->open = bz2_open_o;
    oh->write = bz2_write_o;
    oh->close = bz2_close_o;
  }
  else if (strlen(path) > 3 && !strcmp(path + strlen(path) - 3, ".gz")) {
    oh->open = gz_open_o;
    oh->write = gz_write_o;
    oh->close = gz_close_o;
  }
  else {
    /*
    only for DEBUG perf testing

    oh->open = gz_open_o;
    oh->write = gz_write_o;
    oh->close = gz_close_o;
    */
    oh->open = txt_open_o;
    oh->write = txt_write_o;
    oh->close = txt_close_o;
  }
  return(oh);
}

int bz2_open_o(OutputHandler *oh) {
  oh->fout = fopen(oh->path, "w");
  oh->bzstream = BZ2_bzWriteOpen(&(oh->bzerror), oh->fout, oh->bz_blocksize,
                                 oh->bz_verbosity, oh->bz_workfactor);
  if (oh->bzerror != BZ_OK) {
    fprintf(stderr, "error %d trying to open %s for compression\n",
            oh->bzerror, oh->path);
    exit(-1);
  }
  return(1);
}

int bz2_write_o(OutputHandler *oh, char *buffer, int bytecount) {
  BZ2_bzWrite(&(oh->bzerror), oh->bzstream, buffer, bytecount);
  if (oh->bzerror != BZ_OK) {
    fprintf(stderr, "error %d trying to write to %s\n",
            oh->bzerror, oh->path);
    return(0);
  }
  return(bytecount);
}

int bz2_close_o(OutputHandler *oh) {
  unsigned int bytes_in;
  unsigned int bytes_out;

  BZ2_bzWriteClose(&(oh->bzerror), oh->bzstream, 0, &bytes_in, &bytes_out);
  fclose(oh->fout);
  return(0);
}

int gz_open_o(OutputHandler *oh) {
  oh->gzstream = gzopen(oh->path, "w");
  gzbuffer(oh->gzstream, oh->gz_bufsize);
  return(0);
}

int gz_write_o(OutputHandler *oh, char *buffer, int bytecount) {
  return(gzwrite(oh->gzstream, buffer, bytecount));
}

int gz_close_o(OutputHandler *oh) {
  gzclose(oh->gzstream);
  return(0);
}

int txt_open_o(OutputHandler *oh) {
  oh->fout = fopen(oh->path, "w");
  return(0);
}

int txt_write_o(OutputHandler *oh, char *buffer, int bytecount) {
  return(fwrite(buffer, 1, bytecount, oh->fout));
}

int txt_close_o(OutputHandler *oh) {
  fclose(oh->fout);
  return(0);
}

/* note that even if we have only read a partial line
   of text from the body of the page, (cause the text
   is longer than our buffer), it's fine, since the
   <> delimiters only mark xml, they can't appear
   in the page text.

   returns new state */
States setState(char *line, States currentState, int startPageID, int endPageID, int *readPageID) {
  int pageID = 0;

  if (currentState == EndHeader) {
    /* if we have junk after the header we don't write it.
     commands like dumpbz2filefromoffset can produce such streams. */
    if (strncmp(line,"<page>",6)) {
      return(NoWrite);
    }
  }

  if (!strncmp(line,"<mediawiki",10)) {
    return(StartHeader);
  }
  else if (!strncmp(line,"</siteinfo>",11)) {
    return(EndHeader);
  }
  else if (!strncmp(line,"<page>",6)) {
    return(StartPage);
  }
  /* there are also user ids, revision ids, etc... pageid will be the first one */
  else if (currentState == StartPage && (!strncmp(line, "<id>", 4))) {
    /* dig the id out, format is <id>num</id> */
    pageID = atoi(line+4);
    if (endPageID && (pageID >= endPageID)) {
      *readPageID = pageID;
      return(AtLastPageID);
    }
    else if (pageID >= startPageID) {
      return(WriteMem);
    }
    else {
      /* we don't write anything */
      return(NoWrite);
    }
  }
  else if (currentState == WriteMem) {
    return(Write);
  }
  else if (!strncmp(line, "</page>", 6)) {
    if (currentState == Write) {
      return(EndPage);
    }
    else {
      /* don't write anything */
      return(NoWrite);
    }
  }
  else if (!strncmp(line, "</mediawiki",11)) {
    return(NoWrite);
  }
  return(currentState);
}

/* returns 1 on success, 0 on error */
int writeMemoryIfNeeded(char *mem, States state, OutputHandler *ohandler) {
  int res = 0;

  if (state == WriteMem) {
    /* res = ohandler->write(mem,strlen(mem),1,stdout); */
    res = ohandler->write(ohandler, mem, strlen(mem));
    return(res);
  }
  return(1);
}

void clearMemoryIfNeeded(char *mem, States state) {
  if (state == WriteMem || state == NoWrite) {
    mem[0]='\0';
  }
}

/* returns 1 on success, 0 on error */
int writeIfNeeded(char *line, States state, OutputHandler *ohandler) {
  if (state == StartHeader || state == EndHeader || state == WriteMem || state == Write || state == EndPage) {
    return(ohandler->write(ohandler, line,strlen(line)));
  }
  return(1);
}

/*  returns 1 on success, 0 on error */
int saveInMemIfNeeded(char *mem, char *line, States state) {
  if (state == StartPage || state == AtLastPageID) {
    if (strlen(mem) + strlen(line) < MAXHEADERLEN) {
      strcpy(mem + strlen(mem),line);
    }
    else {
      /* we actually ran out of room, who knew */
      return(0);
    }
  }
  return(1);
}

/*  returns 1 on success, 0 on error */
int saveMWHeaderIfNeeded(char *header, char *line, States state) {
  if (state == StartHeader || state == EndHeader) {
    if (strlen(header) + strlen(line) < MAXHEADERLEN) {
      strcpy(header + strlen(header),line);
    }
    else {
      /* we actually ran out of room, who knew */
      return(0);
    }
  }
  return(1);
}

/*  returns 1 on success, 0 on error */
int writeHeader(char *header, OutputHandler *ohandler) {
  return(ohandler->write(ohandler, header, strlen(header)));
}

void write_output(char *inpath, int startPageID, int endPageID) {
  char *text;
  char line[4097];
  /* order of magnitude of 2K lines of 80 chrs each,
     no header of either a page nor the mw header should
     ever be longer than that. At least not for some good
     length of time. */
  char mem[MAXHEADERLEN];
  States state = NoWrite;
  InputHandler *ihandler = NULL;
  OutputHandler *ohandler = NULL;
  int readPageID = 0;

  ihandler = inputhandler_init(inpath);
  if (ihandler->open)
    ihandler->open(ihandler);

  ohandler = outputhandler_init(NULL);
  while (ihandler->fgets(ihandler, line, sizeof(line)-1) != NULL) {
    text=line;
    while (*text && isspace(*text))
      text++;
    state = setState(text, state, startPageID, endPageID, &readPageID);
    if (!saveInMemIfNeeded(mem, line, state)) {
      fprintf(stderr, "failed to save text in temp memory, bailing\n");
      exit(-1);
    };
    if (!writeMemoryIfNeeded(mem, state, ohandler)) {
      fprintf(stderr, "failed to write text from memory, bailing\n");
      exit(-1);
    }
    clearMemoryIfNeeded(mem, state);
    if (!writeIfNeeded(line, state, ohandler)) {
      fprintf(stderr, "failed to write text, bailing\n");
      exit(-1);
    }
    if (state == AtLastPageID) {
      /* we are done. */
      break;
    }
  }
  ohandler->write(ohandler, "</mediawiki>\n", 13);
}

typedef struct {
  char *filename;
  int startid;
  int endid;
} fspec;

int is_numeric(char *maybe_number) {
  /* return 0 if all chars in null-terminated string are digits */
  while (*maybe_number) {
    if (!isdigit(*maybe_number)) return 0;
    maybe_number++;
  }
  return 1;
}

void fspec_free(fspec *free_me) {
  if (free_me == NULL) return;
  if (free_me->filename != NULL) free(free_me);
  free(free_me);
}

fspec *get_new_fspec(char *fspec_string) {
  fspec *new_fspec = NULL;
  char *next = NULL;
  char *holder = NULL;

  new_fspec = (fspec *)malloc(sizeof(fspec));
  /* fixme should emit some sort of error? */
  if (new_fspec == NULL) return NULL;

  new_fspec->filename = strtok_r(fspec_string, ":", &holder);
  if (new_fspec->filename == NULL) {
    fprintf(stderr, "missing filename in fspec %s\n", fspec_string);
    fspec_free(new_fspec);
    return NULL;
  }
  next = strtok_r(NULL, ":", &holder);
  if (next == NULL) {
    fprintf(stderr, "missing page start id in fspec %s\n", fspec_string);
    fspec_free(new_fspec);
    return NULL;
  }
  if (! is_numeric(next)) {
    fprintf(stderr, "non-numeric page start id in fspec %s\n", fspec_string);
    fspec_free(new_fspec);
    return NULL;
  }
  new_fspec->startid = atoi(next);

  next = strtok_r(NULL, ":", &holder);
  if (next == NULL) {
    /* if there's nothing left at the end of the string ("name:startid:"),
       then it's still ok, put 0 as placeholder */
    new_fspec->endid = 0;
    return(new_fspec);
  }
  if (! is_numeric(next)) {
    /* fixme should emit some sort of error? */
    fspec_free(new_fspec);
    return NULL;
  }
  new_fspec->endid = atoi(next);
  next = strtok_r(NULL, ":", &holder);
  if (next != NULL) {
  /* if there's stuff still left in the string then it's crap */
    fprintf(stderr, "non-numeric page end id in fspec %s\n", fspec_string);
    fspec_free(new_fspec);
    return NULL;
  }
  return new_fspec;
}

fspec **get_fspec_list(char *fspecs_string) {
  int numspecs = 1;
  int count = 0;
  char *tmp = NULL;
  fspec **fspec_list = NULL;
  char *holder = NULL;

  tmp = fspecs_string;
  while (*tmp != '\0') {
    if (*tmp == ';') numspecs++;
    tmp++;
  }
  fspec_list = (fspec **)malloc(sizeof(fspec *)*(numspecs + 1));
  fspec_list[numspecs] = NULL;

  tmp = strtok_r(fspecs_string, ";", &holder);
  if (tmp == NULL) {
  /* fixme if it's NULL we should give up */
    fprintf(stderr, "badly formatted fspec list (empty?) %s\n", fspecs_string);
    exit(-1);
  }
  while (tmp != NULL) {
    fspec_list[count++] = get_new_fspec(tmp);
    tmp = strtok_r(NULL, ";", &holder);
  }
  return(fspec_list);
}

char *path_join(char *dir, char *filename) {
  char *path = NULL;

  if (dir == NULL || ! *dir) return(filename);
  if (filename == NULL || ! *filename) return(dir);
  path = (char *)malloc(strlen(dir) + strlen(filename) + 2);
  if (path == NULL) return(NULL);
  strcpy(path, dir);
  if (dir[strlen(dir)] == '/' && filename[0] == '/') {
    path[strlen(path)] = '\0';
  }
  else if (dir[strlen(dir)] != '/' && filename[0] != '/') {
    strcpy(path + strlen(path), "/");
  }
  strcpy(path + strlen(path), filename);
  return(path);
}

void write_output_files(char *inpath, char *odir, char *fspecs) {
  char *text;
  char line[4097];
  /* order of magnitude of 2K lines of 80 chrs each,
     no header of either a page nor the mw header should
     ever be longer than that. At least not for some good
     length of time. */
  char mem[MAXHEADERLEN];
  char mwheader[MAXHEADERLEN];
  States state = NoWrite;
  InputHandler *ihandler = NULL;
  OutputHandler *ohandler = NULL;
  fspec **fspeclist = NULL;
  int count = 0;
  fspec *fspec_obj = NULL;
  char *ofilepath = NULL;
  int filestart = 1;
  int readPageID = 0;

  ihandler = inputhandler_init(inpath);
  if (ihandler->open)
    ihandler->open(ihandler);

  ohandler = outputhandler_init(ofilepath);

  fspeclist = get_fspec_list(fspecs);
  fspec_obj = fspeclist[count++];
  while (fspec_obj != NULL) {
      filestart = 1;
      ofilepath = path_join(odir, fspec_obj->filename);
      ohandler = outputhandler_init(ofilepath);
      ohandler->open(ohandler);

      /* this line here is busted somehow. woops */
      while (ihandler->fgets(ihandler, line, sizeof(line)-1) != NULL) {
        if (filestart) {
          if (mwheader[0] && !writeHeader(mwheader, ohandler)) {
            fprintf(stderr, "failed to write saved header, bailing\n");
            exit(-1);
          }
          /* if we are picking up after writing an output file already,
             there may be some page info in memory that we have not yet
             handled */
          if (state == AtLastPageID) {
            if (readPageID >= fspec_obj->startid) {
              state = WriteMem;
              if (!writeMemoryIfNeeded(mem, state, ohandler)) {
                fprintf(stderr, "failed to write text from memory, bailing\n");
                exit(-1);
              }
              clearMemoryIfNeeded(mem, state);
              state = Write;
            }
            else {
              state = NoWrite;
            }
          }
          filestart = 0;
        };
        text=line;
        while (*text && isspace(*text))
          text++;
        state = setState(text, state, fspec_obj->startid, fspec_obj->endid, &readPageID);
        if (!saveInMemIfNeeded(mem, line, state)) {
          fprintf(stderr, "failed to save text in temp memory, bailing\n");
          exit(-1);
        };
        if (!saveMWHeaderIfNeeded(mwheader, line, state)) {
          fprintf(stderr, "failed to save header, bailing\n");
          exit(-1);
        };
        if (!writeMemoryIfNeeded(mem, state, ohandler)) {
          fprintf(stderr, "failed to write text from memory, bailing\n");
          exit(-1);
        }
        clearMemoryIfNeeded(mem, state);
        if (!writeIfNeeded(line, state, ohandler)) {
          fprintf(stderr, "failed to write text, bailing\n");
          exit(-1);
        }
        if (state == AtLastPageID) {
          /* we are done. */
          break;
        }
      }
      ohandler->write(ohandler, "</mediawiki>\n", 13);
      ohandler->close(ohandler);
      free(ofilepath);
      fspec_obj = fspeclist[count++];
  }
  if (ihandler->close != NULL)
    ihandler->close(ihandler);
}

int main(int argc,char **argv) {
  long int startPageID = 0;
  long int endPageID = 0;
  char *nonNumeric = 0;
  char *inpath = NULL;
  char *odir = NULL;
  char *fspecs = NULL;

  int optc;
  int optindex=0;

  struct option optvalues[] = {
    {"inpath", 1, 0, 'i'},
    {"odir", 1, 0, 'o'},
    {"fspecs", 1, 0, 'f'},
    {"help", 0, 0, 'h'},
    {"version", 0, 0, 'v'},
    {NULL, 0, NULL, 0}
  };

  while (1) {
    optc = getopt_long_only(argc,argv,"i:o:f:hv", optvalues, &optindex);
    if (optc == 'v')
      show_version(VERSION);
    else if (optc == 'i')
      inpath = optarg;
    else if (optc == 'o')
      odir = optarg;
    else if (optc == 'f')
      fspecs = optarg;
    else if (optc == 'h')
      usage(NULL);
    else if (optc == 'v')
      show_version(VERSION);
    else if (optc == -1) break;
    else {
      usage("Unknown option or other error\n");
    }
  }

  /* if there's odir and/or filespec then we only do that, otherwise the rest */
  /* note we do not check that the filespecs are in order or that only the last
     one, if any, has the endpageid omitted. user beware. */
  if (odir != NULL && fspecs == NULL) {
    usage("The odir option requires the fspecs option.\n");
  }
  else if (fspecs != NULL && odir == NULL) {
    usage("The fspecs option requires the odir option.\n");
  }
  else if (odir != NULL && fspecs != NULL) {
    write_output_files(inpath, odir, fspecs);
    exit(0);
  }

  if (optind >= argc) {
    usage("Missing filename argument.");
  }
  errno = 0;
  startPageID = strtol(argv[optind], &nonNumeric, 10);
  if (startPageID == 0 ||
      *nonNumeric != 0 ||
      nonNumeric == (char *) &startPageID ||
      errno != 0) {
    usage("The value you entered for startPageID must be a positive integer.");
    exit(-1);
  }
  optind++;
  if (optind < argc) {
    endPageID = strtol(argv[optind], &nonNumeric, 10);
    if (endPageID == 0 ||
        *nonNumeric != 0 ||
        nonNumeric == (char *) &endPageID ||
        errno != 0) {
      usage("The value you entered for endPageID must be a positive integer.\n");
      exit(-1);
    }
  }

  write_output(inpath, startPageID, endPageID);
  exit(0);
}
