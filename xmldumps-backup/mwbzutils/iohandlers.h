#ifndef _IOHANDLERS_H
#define _IOHANDLERS_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include <bzlib.h>
#include <zlib.h>

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

void free_bz2buf(bz2buffer_t *b);

bz2buffer_t *init_bz2buf();

InputHandler *inputhandler_init(char *path);

int isfull(bz2buffer_t *buf);
void fill_buffer(bz2buffer_t *buf, BZFILE *fd);
int has_newline(bz2buffer_t *buf);
char *bz2gets(BZFILE *fd, bz2buffer_t *buf, char *out, int out_size);

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

OutputHandler *outputhandler_init(char *path);
#endif
