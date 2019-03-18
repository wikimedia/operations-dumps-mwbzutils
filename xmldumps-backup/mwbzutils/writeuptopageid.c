#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include <bzlib.h>
#include <zlib.h>
#include "iohandlers.h"

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
"  -H  --noheader   don't write mw/siteinfo header\n"
"  -F  --nofooter   don't write mediawiki footer\n"
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
int writeIfNeeded(char *line, States state, int noheader, OutputHandler *ohandler) {
  if ((state == StartHeader || state == EndHeader) && noheader)
    return(1);
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

void write_output(char *inpath, int startPageID, int endPageID,
		  int noheader, int nofooter) {
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
    if (!writeIfNeeded(line, state, noheader, ohandler)) {
      fprintf(stderr, "failed to write text, bailing\n");
      exit(-1);
    }
    if (state == AtLastPageID) {
      /* we are done. */
      break;
    }
  }
  if (!nofooter)
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
    usage(NULL);
  }
  next = strtok_r(NULL, ":", &holder);
  if (next == NULL) {
    fprintf(stderr, "missing page start id in fspec %s\n", fspec_string);
    fspec_free(new_fspec);
    usage(NULL);
  }
  if (! is_numeric(next)) {
    fprintf(stderr, "non-numeric page start id in fspec %s, got %s\n", fspec_string, next);
    fspec_free(new_fspec);
    usage(NULL);
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
    fprintf(stderr, "bad fspec %s, expected numeric for next piece but got %s\n", fspec_string, next);
    fspec_free(new_fspec);
    usage(NULL);
  }
  new_fspec->endid = atoi(next);
  next = strtok_r(NULL, ":", &holder);
  if (next != NULL) {
  /* if there's stuff still left in the string then it's crap */
    fprintf(stderr, "non-numeric page end id in fspec %s, got %s\n", fspec_string, next);
    fspec_free(new_fspec);
    usage(NULL);
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

void write_output_files(char *inpath, char *odir, char *fspecs,
			int noheader, int nofooter) {
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
          if (!noheader && mwheader[0] && !writeHeader(mwheader, ohandler)) {
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
        if (!writeIfNeeded(line, state, noheader, ohandler)) {
          fprintf(stderr, "failed to write text, bailing\n");
          exit(-1);
        }
        if (state == AtLastPageID) {
          /* we are done. */
          break;
        }
      }
      if (!nofooter)
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
  int noheader = 0;
  int nofooter = 0;
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
    {"nofooter", 0, 0, 'F'},
    {"noheader", 0, 0, 'H'},
    {"help", 0, 0, 'h'},
    {"version", 0, 0, 'v'},
    {NULL, 0, NULL, 0}
  };

  while (1) {
    optc = getopt_long_only(argc,argv,"i:o:f:FHhv", optvalues, &optindex);
    if (optc == 'v')
      show_version(VERSION);
    else if (optc == 'i')
      inpath = optarg;
    else if (optc == 'o')
      odir = optarg;
    else if (optc == 'f')
      fspecs = optarg;
    else if (optc == 'F')
      nofooter = 1;
    else if (optc == 'H')
      noheader = 1;
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
    write_output_files(inpath, odir, fspecs, noheader, nofooter);
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

  write_output(inpath, startPageID, endPageID, noheader, nofooter);
  exit(0);
}
