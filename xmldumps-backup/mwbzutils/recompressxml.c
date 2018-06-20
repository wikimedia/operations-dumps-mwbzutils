#include <unistd.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <regex.h>
#include <ctype.h>
#include <inttypes.h>
#include "iohandlers.h"
#include "bzlib.h"

char inBuf[4096];
char outBuf[8192];

char inBuf_indx[4096];
char outBuf_indx[8192];

char *pageOpenTag = "<page>\n";

char *pageTitleExpr = "<title>(.+)</title>\n";
regmatch_t *matchPageTitleExpr;
regex_t compiledMatchPageTitleExpr;

char *idExpr = "<id>([0-9]+)</id>\n";
regmatch_t *matchIdExpr;
regex_t compiledMatchIdExpr;

void usage(char *message) {
  char * help =
"Usage: recompressxml --pagesperstream n [--buildindex filename] [--verbose]\n"
"   or: recompressxml [--version|--help]\n\n"
"Reads a stream of XML pages from stdin and writes to stdout the bz2 compressed\n"
"data, one bz2 stream (header, blocks, footer) per specified number of pages.\n\n"
"Options:\n\n"
"  -p, --pagesperstream:  Compress this number of pages in each complete\n"
"                         bz2stream before opening a new stream.  The siteinfo\n"
"                         header is written to a separate stream at the beginning\n"
"                         of all output, and the closing mediawiki tag is written\n"
"                         into a separate stream at the end.\n"
"  -b, --buildindex:      Generate a file containing an index of pages ids and titles\n"
"                         per stream.  Each line contains: offset-to-stream:pageid:pagetitle\n"
"                         If filename ends in '.bz2' or '.bz2' plus a file extension .[a-z]*,\n"
"                         the file will be written in bz2 format; if it ends in '.gz' or \n"
"                         '.gz' plus a file extension .[a-z]*, it wll be written in gz format.\n"
"  -i  --inpath:          If not specified, input stream will be read from stdin. Otherwise,\n"
"                         it will be read from the specified file; if the file ends in gz\n"
"                         of .bz2 it will be decompressed on the fly.\n"
"  -o  --outpath:         If not specified, output will be written to stdout, otherwise to\n"
"                         the file specified. If the filename ends in .bz2 or .gz, it will\n"
"                         use the appropriate compression. IF NOT it will be written\n"
"                         uncompressed, which probably defeats the point of this program.\n"
"  -v, --verbose:         Write lots of debugging output to stderr.  This option can be used\n"
"                         multiple times to increase verbosity.\n"
"  -h, --help             Show this help message\n"
"  -V, --version          Display the version of this program and exit\n\n"
"Report bugs in recompressxml to <https://phabricator.wikimedia.org/>.\n\n"
"See also checkforbz2footer(1), dumpbz2filefromoffset(1), dumplastbz2block(1),\n"
"findpageidinbz2xml(1), writeuptopageid(1)\n\n";
  if (message) {
    fprintf(stderr,"%s\n\n",message);
  }
  fprintf(stderr,"%s",help);
  exit(-1);
}

void show_version(char *version_string) {
  char * copyright =
"Copyright (C) 2011, 2012, 2013 Ariel T. Glenn.  All rights reserved.\n\n"
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
  fprintf(stderr,"recompressxml %s\n", version_string);
  fprintf(stderr,"%s",copyright);
  exit(-1);
}

void setupRegexps() {
  matchPageTitleExpr = (regmatch_t *)malloc(sizeof(regmatch_t)*2);
  regcomp(&compiledMatchPageTitleExpr, pageTitleExpr, REG_EXTENDED);
  matchIdExpr = (regmatch_t *)malloc(sizeof(regmatch_t)*2);
  regcomp(&compiledMatchIdExpr, idExpr, REG_EXTENDED);
  return;
}

int startsPage(char *buf) {
  while (*buf == ' ') buf++;

  if (!strcmp(buf,pageOpenTag)) return 1;
  else return 0;
}

char *hasPageTitle(char *buf) {
  static char pageTitle[513];
  int length = 0;

  pageTitle[0]='\0';

  while (*buf == ' ') buf++;

  if (regexec(&compiledMatchPageTitleExpr, buf,  2,  matchPageTitleExpr, 0 ) == 0) {
    if (matchPageTitleExpr[1].rm_so >=0) {
      length = matchPageTitleExpr[1].rm_eo - matchPageTitleExpr[1].rm_so;
      if (length > 512) {
	fprintf(stderr,"Page title length > 512 bytes... really? Bailing.\n");
	exit(1);
      }
      strncpy(pageTitle,buf+matchPageTitleExpr[1].rm_so, length);
      pageTitle[length] = '\0';
    }
  }
  return(pageTitle);
}

int hasId(char *buf) {
  int id = 0;

  while (*buf == ' ') buf++;

  if (regexec(&compiledMatchIdExpr, buf,  2,  matchIdExpr, 0 ) == 0) {
    if (matchIdExpr[1].rm_so >=0) {
      id = atoi(buf+matchIdExpr[1].rm_so);
    }
  }
  return(id);
}

int endsXmlBlock(char *buf, int header) {
  char *pageCloseTag = "</page>\n";
  char *mediawikiCloseTag = "</mediawiki>\n";
  char *siteinfoCloseTag = "</siteinfo>\n";

  while (*buf == ' ') buf++;

  /* if we are trying to process the header, check for that only */
  if (header) {
    if (!strcmp(buf,siteinfoCloseTag)) return 1;
    else return 0;
  }

  /* normal check for end of page, end of content */
  if (!strcmp(buf,pageCloseTag) || !strcmp(buf,mediawikiCloseTag)) return 1;
  else return 0;
}

void writeCompressedXmlBlock(int header, int count, off_t *fileOffset, InputHandler *ihandler,
			     OutputHandler *ohandler, OutputHandler *index_ohandler,int verbose)
 {
  int wroteSomething = 0;
  int blocksDone = 0;

  char *pageTitle = NULL;
  int pageId = 0;
  enum States{WantPage,WantPageTitle,WantPageId};
  int state = WantPage;

  /* if we're past the first block, we append the rest */
  if (!header && ohandler->path != NULL)
    outputhandler_appendmode(ohandler);

  if (ohandler->closed && ohandler->open != NULL)
    ohandler->open(ohandler);
  if (verbose > 1)
    fprintf(stderr,"opened the output file if needed\n");

  while (ihandler->fgets(ihandler, inBuf, sizeof(inBuf)-1) != NULL) {
    if (verbose > 1) {
      fprintf(stderr,"input buffer is: ");
      fprintf(stderr,"%s",inBuf);
    }

    wroteSomething = 1;
    if (index_ohandler) {
      if (verbose > 2) {
	fprintf(stderr,"doing index check\n");
      }
      if (state == WantPage) {
	if (verbose > 2) {
	  fprintf(stderr,"checking for page tag\n");
	}
	if (startsPage(inBuf)) {
	  state = WantPageTitle;
	}
      }
      else if (state == WantPageTitle) {
	if (verbose > 1) {
	  fprintf(stderr,"checking for page title tag\n");
	}
	pageTitle = hasPageTitle(inBuf);
	if (pageTitle[0]) {
	  state = WantPageId;
	}
      }
      else if (state == WantPageId) {
	if (verbose > 1) {
	  fprintf(stderr,"checking for page id tag\n");
	}
	pageId = hasId(inBuf);
	if (pageId) {
	  state = WantPage;
	  if (verbose) {
	    fprintf(stderr,"writing line to index file\n");
	  }
	  sprintf(outBuf_indx,"%"PRId64":%d:%s\n",*fileOffset,pageId,pageTitle);
	  index_ohandler->write(index_ohandler,outBuf_indx,strlen(outBuf_indx));
	  pageId = 0;
	  pageTitle = NULL;
	}
      }
    }
    if (inBuf[0])
      ohandler->write(ohandler, inBuf, strlen(inBuf));
    if (endsXmlBlock(inBuf, header)) {
      /* special case: doing the siteinfo stuff at the beginning */
      inBuf[0] = '\0';
      if (verbose) {
	fprintf(stderr,"end of header, page, or mw found\n");
      }
      if (header) {
	*fileOffset = outputhandler_get_offset(ohandler);
	return;
      }
      blocksDone++;
      if (blocksDone % count == 0) {
	if (verbose) fprintf(stderr, "end of xml block found\n");
	/* close down stream, we are done with this block */
	if (ohandler->close)
	  ohandler->close(ohandler);
	*fileOffset = outputhandler_get_offset(ohandler);
	return;
      }
    }
    inBuf[0] = '\0';
  }
  if (verbose) fprintf(stderr,"eof reached\n");
  if (wroteSomething) {
    /* close down stream, we are done with this block */
    if (ohandler->close)
      ohandler->close(ohandler);
    *fileOffset = outputhandler_get_offset(ohandler);
    return;
  }
  /* done with all input so close up shop */
  if (ohandler->close)
    ohandler->close(ohandler);
  return;
}

int main(int argc, char **argv) {
  int optindex=0;
  int optc;
  off_t offset;

  struct option optvalues[] = {
    {"buildindex", 1, 0, 'b'},
    {"inpath", 1, 0, 'i'},
    {"outpath", 1, 0, 'o'},
    {"help", 0, 0, 'h'},
    {"pagesperstream", 1, 0, 'p'},
    {"verbose", 0, 0, 'v'},
    {"version", 0, 0, 'V'},
    {NULL, 0, NULL, 0}
  };

  int count = 0;
  char *indexFilename = NULL;
  char *inpath = NULL;
  int verbose = 0;
  FILE *indexfd = NULL;
  char *outpath = NULL;
  InputHandler *ihandler = NULL;
  OutputHandler *ohandler = NULL;
  OutputHandler *index_ohandler = NULL;

  while (1) {
    optc=getopt_long_only(argc,argv,"p:b:i:o:v", optvalues, &optindex);
    if (optc=='b') {
      indexFilename = optarg;
    }
    else if (optc=='i') {
      inpath = optarg;
    }
    else if (optc=='o') {
      outpath = optarg;
    }
    else if (optc=='h')
      usage(NULL);
    else if (optc=='p') {
      if (!(isdigit(optarg[0]))) usage(NULL);
      count=atoi(optarg);
    }
    else if (optc=='v')
      verbose++;
    else if (optc=='V')
      show_version(VERSION);
    else if (optc==-1) break;
    else usage("unknown option or other error\n");
  }

  if (count <= 0) {
    usage("bad or no argument given for count.\n");
  }

  if (indexFilename) {
    if (verbose) {
      fprintf(stderr,"setting up index file creation.\n");
    }
    indexfd = fopen(indexFilename, "w");
    if (! indexfd) {
      usage("failed to open index file for write.\n");
    }
    index_ohandler = outputhandler_init(indexFilename);
    if (index_ohandler->open != NULL)
      index_ohandler->open(index_ohandler);
  }

  ihandler = inputhandler_init(inpath);
  if (ihandler->open != NULL) {
    ihandler->open(ihandler);
  }

  ohandler = outputhandler_init(outpath);

  setupRegexps();

  offset = (off_t)0;
  /* deal with the XML header */
  writeCompressedXmlBlock(1,count,&offset,ihandler,ohandler,index_ohandler,verbose);

  if (verbose) {
      if (ihandler->eof(ihandler))
          fprintf(stderr, "EOF reached for input file\n");
  }
  while (!ihandler->eof(ihandler)) {
    writeCompressedXmlBlock(0,count,&offset,ihandler,ohandler,index_ohandler,verbose);
    if (verbose) {
        if (ihandler->eof(ihandler))
            fprintf(stderr, "EOF reached for input file\n");
    }
  }

  if (indexFilename) {
    if (verbose) {
      fprintf(stderr,"closing index file.\n");
    }
    if (index_ohandler->close != NULL)
      index_ohandler->close(index_ohandler);
  }

  if (ihandler->close != NULL)
    ihandler->close(ihandler);
  exit(0);

}
