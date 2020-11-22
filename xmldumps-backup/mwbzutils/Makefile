# ------------------------------------------------------------------
# This Makefile builds binaries which rely on three source files
# from libbzip2 version 1.0.6. (See bz2libfuncs.c, bzlib.h and
# bzlib_private.h; the first is slightly modified while the
# second is unchanged from the library version.)
#
# The copyright for those two files  is as follows:
#
# bzip2/libbzip2 version 1.0.6 of 6 September 2010
# Copyright (C) 1996-2010 Julian Seward <jseward@bzip.org>
#
# Those files are released under the terms of the license contained
# in the file LICENSE_BZ.
#
# All other files are released under the GPL, copyright (C) Ariel T. Glenn
# 2010-2019: see the file COPYING for details.
# ------------------------------------------------------------------

VERSION        = "0.0.11"
CC            ?= gcc
BIGFILES       = -D_FILE_OFFSET_BITS=64
CPPFLAGS      += $(BIGFILES) -DVERSION=\"$(VERSION)\"
CFLAGS        ?= -Wall -Werror -O2

build: appendbz2 checkforbz2footer dumpbz2filefromoffset \
	dumplastbz2block findpageidinbz2xml \
	recompressxml writeuptopageid compressedmanpages \
	getlastidinbz2xml revsperpage showcrcs


NAME_APPENDBZ2               = "Given combined crc of prev content, write appendable bz2 output from stdin"
NAME_CHECKFORBZ2FOOTER       = "Check if bzip2 file ends with bz2 magic footer"
NAME_DUMPBZ2FILEFROMOFFSET   = "Write MediaWiki XML pages from bzip2 file starting from offset"
NAME_DUMPLASTBZ2BLOCK        = "Find last bz2 block in bzip2 file and dump contents"
NAME_FINDPAGEIDINBZ2XML      = "Display offset of bz2 block for given page id in bzip2 MediaWiki XML file"
NAME_GETLASTIDINBZ2XML       = "Display last page or rev id in bzip2 MediaWiki XML file"
NAME_RECOMPRESSXML           = "Bz2 compress MediaWiki XML input in batches of pages"
NAME_REVSPERPAGE             = "Display info about revisions per page from MediaWiki XML input"
NAME_SHOWCRCS                = "Show crcs and offsets of blocks in bz2-compressed file"
NAME_WRITEUPTOPAGEID         = "Write range of page content from MediaWiki XML input"

PREFIX        ?= "/usr/local"
BINDIR         = $(DESTDIR)$(PREFIX)/bin/
MANDIR         = $(DESTDIR)$(PREFIX)/share/man/man1/
DOCDIR         = $(DESTDIR)$(PREFIX)/share/doc/mwbzutils/

GZIP           = /bin/gzip
HELP2MAN       = /usr/bin/help2man
SHELL          = /bin/sh

DISTNAME       = mwbzutils-$(VERSION)

LIBS           = -lbz2
OBJSBZ         = bzlibfuncs.o
OBJS           = mwbzlib.o $(OBJSBZ)

appendbz2: $(OBJSBZ) mwbzlib.o appendbz2.o
	$(CC) $(LDFLAGS) -o appendbz2 appendbz2.o $(OBJS) $(LIBS)

checkforbz2footer: $(OBJSBZ) mwbzlib.o checkforbz2footer.o
	$(CC) $(LDFLAGS) -o checkforbz2footer checkforbz2footer.o $(OBJS) $(LIBS)

dumpbz2filefromoffset: $(OBJSBZ) mwbzlib.o dumpbz2filefromoffset.o
	$(CC) $(LDFLAGS) -o dumpbz2filefromoffset dumpbz2filefromoffset.o $(OBJS) $(LIBS)

dumplastbz2block: $(OBJSBZ) mwbzlib.o dumplastbz2block.o
	$(CC) $(LDFLAGS) -o dumplastbz2block dumplastbz2block.o $(OBJS) $(LIBS)

findpageidinbz2xml: $(OBJSBZ) mwbzlib.o httptiny.o findpageidinbz2xml.o
	$(CC) $(LDFLAGS) -o findpageidinbz2xml findpageidinbz2xml.o httptiny.o $(OBJS) $(LIBS) -lz

getlastidinbz2xml: $(OBJSBZ) mwbzlib.o getlastidinbz2xml.o
	$(CC) $(LDFLAGS) -o getlastidinbz2xml getlastidinbz2xml.o $(OBJS) $(LIBS)

recompressxml: $(OBJSBZ) iohandlers.o recompressxml.o
	$(CC) $(LDFLAGS) -o recompressxml iohandlers.o recompressxml.o $(LIBS) -lz

revsperpage: revsperpage.o
	$(CC) $(LDFLAGS) -o revsperpage revsperpage.o

showcrcs: $(OBJSBZ) iohandlers.o showcrcs.o
	$(CC) $(LDFLAGS) -o showcrcs showcrcs.o $(OBJS) $(LIBS)

writeuptopageid: $(OBJSBZ) iohandlers.o writeuptopageid.o
	$(CC) $(LDFLAGS) -o writeuptopageid iohandlers.o writeuptopageid.o $(LIBS) -lz

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

compressedmanpages: docs/appendbz2.1.gz docs/dumplastbz2block.1.gz \
	docs/findpageidinbz2xml.1.gz \
	docs/checkforbz2footer.1.gz docs/dumpbz2filefromoffset.1.gz \
	docs/recompressxml.1.gz docs/revsperpage.1.gz docs/showcrcs.1.gz \
	docs/writeuptopageid.1.gz

docs/%.1.gz: docs/%.1
	cat $< | $(GZIP) > $@

# this target should only be made when updating the source if the version
# or the usage mssages change
manpages: appendbz2.1 dumplastbz2block.1 findpageidinbz2xml.1 \
	checkforbz2footer.1 dumpbz2filefromoffset.1 \
	recompressxml.1 revsperpage.1 writeuptopageid.1 \
	getlastidinbz2xml.1 showcrcs.1
	echo "Don't forget to commit your manpage changes to the repo"

appendbz2.1 : appendbz2
	$(HELP2MAN) --section 1 --no-info --name $(NAME_APPENDBZ2) \
		--no-discard-stderr ./checkforbz2footer > docs/appendbz2.1
checkforbz2footer.1 : checkforbz2footer
	LC_TIME=C $(HELP2MAN) --section 1 --no-info --name $(NAME_CHECKFORBZ2FOOTER) \
		--no-discard-stderr ./checkforbz2footer > docs/checkforbz2footer.1
dumpbz2filefromoffset.1 : dumpbz2filefromoffset
	LC_TIME=C $(HELP2MAN) --section 1 --no-info --name $(NAME_DUMPBZ2FILEFROMOFFSET) \
		--no-discard-stderr ./dumpbz2filefromoffset > docs/dumpbz2filefromoffset.1
dumplastbz2block.1 : dumplastbz2block
	LC_TIME=C $(HELP2MAN) --section 1 --no-info --name $(NAME_DUMPLASTBZ2BLOCK) \
		--no-discard-stderr ./dumplastbz2block > docs/dumplastbz2block.1
findpageidinbz2xml.1 : findpageidinbz2xml
	LC_TIME=C $(HELP2MAN) --section 1 --no-info --name $(NAME_FINDPAGEIDINBZ2XML) \
		--no-discard-stderr ./findpageidinbz2xml > docs/findpageidinbz2xml.1
getlastidinbz2xml.1 : getlastidinbz2xml
	LC_TIME=C $(HELP2MAN) --section 1 --no-info --name $(NAME_GETLASTIDINBZ2XML) \
		--no-discard-stderr ./getlastidinbz2xml > docs/getlastidinbz2xml.1
recompressxml.1 : recompressxml
	LC_TIME=C $(HELP2MAN) --section 1 --no-info --name $(NAME_RECOMPRESSXML) \
		--no-discard-stderr ./recompressxml > docs/recompressxml.1
revsperpage.1 : revsperpage
	LC_TIME=C $(HELP2MAN) --section 1 --no-info --name $(NAME_REVSPERPAGE) \
		--no-discard-stderr ./revsperpage > docs/revsperpage.1
showcrcs.1 : showcrcs
	LC_TIME=C $(HELP2MAN) --section 1 --no-info --name $(NAME_SHOWCRCS) \
		--no-discard-stderr ./showcrcs > docs/showcrcs.1
writeuptopageid.1 : writeuptopageid
	LC_TIME=C $(HELP2MAN) --section 1 --no-info --name $(NAME_WRITEUPTOPAGEID) \
		--no-discard-stderr ./writeuptopageid > docs/writeuptopageid.1

install: dumplastbz2block findpageidinbz2xml checkforbz2footer dumpbz2filefromoffset \
	recompressxml writeuptopageid compressedmanpages getlastidinbz2xml
	install --directory                             $(BINDIR)
	install --mode=755   appendbz2                  $(BINDIR)
	install --mode=755   checkforbz2footer          $(BINDIR)
	install --mode=755   dumplastbz2block           $(BINDIR)
	install --mode=755   dumpbz2filefromoffset      $(BINDIR)
	install --mode=755   findpageidinbz2xml         $(BINDIR)
	install --mode=755   getlastidinbz2xml          $(BINDIR)
	install --mode=755   recompressxml              $(BINDIR)
	install --mode=755   revsperpage                $(BINDIR)
	install --mode=755   showcrcs                   $(BINDIR)
	install --mode=755   writeuptopageid            $(BINDIR)
	install --mode=755   scripts/munge_crc_info.py  $(BINDIR)
	install --mode=755   scripts/show_byte_aligned_crcs.py     $(BINDIR)
	install --mode=755   scripts/split_bz2.py       $(BINDIR)
	install --directory                             $(MANDIR)
	install --mode=644   docs/*.1.gz                $(MANDIR)
	install --directory                             $(DOCDIR)
	install --mode=644   README                     $(DOCDIR)
	install --mode=644   LICENSE_BZ                 $(DOCDIR)
	install --mode=644   COPYING                    $(DOCDIR)

uninstall:
	rm -f $(BINDIR)appendbz2
	rm -f $(BINDIR)dumplastbz2block
	rm -f $(BINDIR)findpageidinbz2xml
	rm -f $(BINDIR)getlastidinbz2xml
	rm -f $(BINDIR)checkforbz2footer
	rm -f $(BINDIR)dumpbz2filefromoffset
	rm -f $(BINDIR)recompressxml
	rm -f $(BINDIR)revsperpage
	rm -f $(BINDIR)showcrcs
	rm -f $(BINDIR)writeuptopageid
	rm -f $(BINDIR)munge_crc_info.py
	rm -f $(BINDIR)show_byte_aligned_crcs.py
	rm -f $(BINDIR)split_bz2.py
	rm -f $(MANDIR)*.1.gz
	rm -f $(DOCDIR)README
	rm -f $(DOCDIR)LICENSE_BZ
	rm -f $(DOCDIR)COPYING

clean:
	rm -f *.o *.a appendbz2 dumplastbz2block findpageidinbz2xml \
		getlastidinbz2xml \
		checkforbz2footer dumpbz2filefromoffset \
		recompressxml revsperpage showcrcs writeuptopageid \
		docs/*.1.gz

distclean: clean
	rm -f $(DISTNAME)
	rm -f *.tar.gz

reallyclean: distclean
	rm -f docs/*.1

dist:
	rm -f $(DISTNAME)
	ln -s -f . $(DISTNAME)
	tar cvf $(DISTNAME).tar \
	   $(DISTNAME)/*.c \
	   $(DISTNAME)/*.h \
	   $(DISTNAME)/Makefile \
	   $(DISTNAME)/LICENSE_BZ \
	   $(DISTNAME)/COPYING \
	   $(DISTNAME)/README \
	   $(DISTNAME)/CHANGES \
	   $(DISTNAME)/docs/*1
	gzip -v $(DISTNAME).tar
