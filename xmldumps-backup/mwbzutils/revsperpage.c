#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>


typedef enum { None, StartPage, Title, StartNS, PageId, StartRev, ByteLen, EndPage, EndMW } States;

void usage(char *message) {
  char * help =
"Usage: revsperpage [--version|--help]\n"
"   or: revsperpage [--all] {--bytes] [--maxrevlen] [--title] [--batch <number>] [--concise] [--cutoff <number>]\n"
"Reads a MediaWiki XML 'stubs' file from from stdin and writes information about the revisions for\n"
"each of the pages.\n\n"
"Options:\n\n"
"  -B   --batch      display cumulative numbers for the specified number of pages instead of each page\n"
"  -C   --cutoff     display information only for pages with more revisions than the specified number\n"
"Flags:\n\n"
"  -a   --all        display the page id along with number of revisions for each page for all namespaces;\n"
"                    if this option is omitted, the page id is not shown, and the revision count is shown only\n"
"                    for pages in namespace 0\n"
"  -b   --bytes      display the sum of byte lengths for the revisions for each page\n"
"  -c   --concise    don't display field names in each line, and use : as the field separator\n"
"  -m   --maxrevlen  display the max byte length for revisions of each page\n"
"  -t   --title      display the page title for each page\n"
"  -h, --help       Show this help message\n"
"  -v, --version    Display the version of this program and exit\n\n"
"Note that some options may not make good sense used together. For example, one can use --batch and\n"
" --title togther but only the title of the last page in the aggregated batch will be displayed,\n"
"which is most likely not what you want.\n"
"Report bugs in revsperpage to <https://phabricator.wikimedia.org/>.\n\n"
"See also checkforbz2footer(1), dumpbz2filefromoffset(1), dumplastbz2block(1),\n"
    "findpageidinbz2xml(1), recompressxml(1), writeuptopage(1)\n\n";
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
  fprintf(stderr,"revsperpage %s\n", version_string);
  fprintf(stderr,"%s",copyright);
  exit(-1);
}

/* note that even if we have only read a partial line
   of text from the body of the page, (cause the text
   is longer than our buffer), it's fine, since the
   <> delimiters only mark xml, they can't appear
   in the page text.

   returns new state */
States setState (char *line, States currentState) {
  if (!strncmp(line,"<page>",6)) {
    return(StartPage);
  }
  else if (!strncmp(line,"<title>",7)) {
    return(Title);
  }
  else if (currentState == Title && !strncmp(line, "<ns>", 4)) {
    return(StartNS);
  }
  else if (currentState == StartNS && !strncmp(line,"<id>",4)) {
    return(PageId);
  }
  else if (!strncmp(line,"<revision>",10)) {
    return(StartRev);
  }
  else if (!strncmp(line,"<text ",6)) {
    return(ByteLen);
  }
  else if (!strncmp(line, "</page>", 6)) {
      return(EndPage);
  }
  else if (!strncmp(line, "</mediawiki",11)) {
    return(EndMW);
  }
  return(currentState);
}

long int get_bytelen(char *text) {
  long int length = 0L;
  char *entry = NULL;

  /* typical entry in stubs used to be: <text id="11453" bytes="4837" />
     then: <text xml:space="preserve" bytes="141920" id="87207" />
     now: <text bytes="2052" id="335706323" /> which is very annoying */

  /* 'bytes=' */
  entry = strstr(text, " bytes=\"");

  if (entry == NULL)
    return(length);

  entry += 8;
  if (! *entry)
      return(length);

  /* byte length */
  entry = strtok(entry, "\"");
  if (entry == NULL) {
      /* should never happen but let's be safe */
      return(length);
  }
  length = strtol(entry, NULL, 10);
  return(length);
}

int main(int argc,char **argv) {
  States state = None;
  char *text;
  char line[4097];
  int revisions = 0;
  long int length = 0L;
  long int revlen;
  long int maxrevlen = 0L;
  int batch = 0;
  int batchstart = 1;
  int concise = 0;
  int good = 0;
  int pagecount = 0;
  int all = 0;
  int do_length = 0;
  int do_title = 0;
  int pageid = 0;
  int cutoff = 0;
  int do_maxrevlen = 0;
  char *title = NULL;

  int optc;
  int optindex=0;

  struct option optvalues[] = {
    {"all", 0, 0, 'a'},
    {"batch", 1, 0, 'B'},
    {"bytes", 0, 0, 'b'},
    {"concise", 0, 0, 'c'},
    {"cutoff", 1, 0, 'C'},
    {"maxrevlen", 0, 0, 'm'},
    {"title", 0, 0, 't'},
    {"help", 0, 0, 'h'},
    {"version", 0, 0, 'v'},
    {NULL, 0, NULL, 0}
  };

  while (1) {
    optc = getopt_long_only(argc,argv,"aB:bcC:mthv", optvalues, &optindex);
    if (optc == 'v')
      show_version(VERSION);
    else if (optc == 'a')
      all = 1;
    else if (optc == 'b')
      do_length=1;
    else if (optc == 'B') {
      if (isdigit(optarg[0]))
	batch = strtol(optarg, NULL, 10);
      else
	usage("Option --batch requires a number\n");
    }
    else if (optc == 'c')
      concise = 1;
    else if (optc == 'C') {
      if (isdigit(optarg[0])) {
	cutoff = strtol(optarg, NULL, 10);
      }
      else
	usage("Option --cutoff requires a number\n");
    }
    else if (optc == 'm')
      do_maxrevlen=1;
    else if (optc == 't')
      do_title=1;
    else if (optc == 'h')
      usage(NULL);
    else if (optc == -1) break;
    else {
      usage("Unknown option or other error\n");
    }
  }


  while (fgets(line, sizeof(line)-1, stdin) != NULL) {
    text=line;
    while (*text && isspace(*text))
      text++;
    state = setState(text, state);
    if (state == StartPage) {
      /* always reset this on a new page; it lets us exclude pages
	 in the wrong namespace if desired */
      good = 0;

      if (batchstart) {
        if (batch > 0) {
	  /* we are accumulating values from several page entries,
	     but now starting a new batch of those; if we aren't
	     batching then batchstart should always be 1 and we should
	     reset after every page. */
	  batchstart = 0;
	  pagecount = 1;
	}
	revisions = 0;
	length = 0L;
	maxrevlen = 0L;
	if (title != NULL)
	  free(title);
      }
    }

    if (state == StartNS) {
      if (!all && strncmp(text,"<ns>0</ns>",10)) {
	good = 0;
      }
      else {
	good = 1;
      }
    }
    if (state == ByteLen && good) {
      revlen = get_bytelen(text);
      if (revlen > maxrevlen)
        maxrevlen = revlen;
      length+= revlen;
      state = None;
    }
    if (state == PageId) {
      text+=4; /* skip <id> tag */
      pageid = strtol(text, NULL, 10);
      state = None;
    }
    if (state == Title) {
      text+=7; /* skip <title> tag */
      title = strndup(text, strlen(text) - 9);
    }
    if (state == StartRev && good) {
      revisions++;
      state = None;
    }
    if ((state == EndPage && (!batch || (pagecount == batch))) ||
	(state == EndMW && batch))  {

      if (revisions && revisions > cutoff) {
	if (all) {
	  if (concise) fprintf(stdout, "%d:",pageid);
	  else fprintf(stdout, "page:%d ",pageid);
	}
	if (do_length) {
	  if (concise) fprintf(stdout, "%ld:",length);
	  else fprintf(stdout, "bytes:%ld ",length);
	}
	if (do_maxrevlen) {
	  if (concise) fprintf(stdout, "%ld:",maxrevlen);
	  else fprintf(stdout, "maxrevlen:%ld ",maxrevlen);
	}
	if (concise) fprintf(stdout, "%d",revisions);
	else fprintf(stdout, "revs:%d",revisions);
	if (do_title) {
	  if (concise) fprintf(stdout, ":%s\n",title);
	  else fprintf(stdout, " title:%s\n",title);
	}
	else
	  fprintf(stdout, "\n");
      }
      /* clear this so we don't display the last entry twice */
      revisions = 0;
    }
    if (state == EndPage || state == EndMW)  {
      state = None;
      if (good) {
	pagecount += 1;
	if (pagecount > batch) batchstart = 1;
      }
    }
  }
  exit(0);
}
