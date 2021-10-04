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
    ih->eof = txt_eof_i;
    return(ih);
  }

  if (strlen(path) > 4 && !strcmp(path + strlen(path) - 4, ".bz2")) {
    ih->open = bz2_open_i;
    ih->fgets = bz2_fgets_i;
    ih->close = bz2_close_i;
    ih->eof = bz2_eof_i;
  }
  else if (strlen(path) > 3 && !strcmp(path + strlen(path) - 3, ".gz")) {
    ih->open = gz_open_i;
    ih->fgets = gz_fgets_i;
    ih->close = gz_close_i;
    ih->eof = gz_eof_i;
  }
  else {
    ih->open = txt_open_i;
    ih->fgets = txt_fgets_i;
    ih->close = txt_close_i;
    ih->eof = txt_eof_i;
  }
  return(ih);
}

int isfull(bz2buffer_t *buf) {
  if (buf->nextin == sizeof(buf->buf)) return(1);
  else return(0);
}

int isempty(bz2buffer_t *buf) {
  if (buf->bytes_avail == 0) return(1);
  return(0);
}

int fill_buffer(bz2buffer_t *buf, BZFILE *fd) {
  int result;

  if (isfull(buf)) return(1);
  result = BZ2_bzread(fd, buf->buf + buf->nextin, sizeof(buf->buf) - buf->nextin);
  if (result > 0) {
    buf->nextin += result;
    buf->bytes_avail += result;
  }
  return result;
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
  int result = 0;

  out[0]='\0';
  if (!buf->bytes_avail) {
    result = fill_buffer(buf, fd);
    if (result < 0)
      return(NULL);
  }
  if (!buf->bytes_avail) {
    return(NULL);
  }

  while (((newline_ind = has_newline(buf)) == -1) && (out_space_remaining > buf->bytes_avail)) {
    strncpy(out+out_ind, buf->buf + buf->nextout, buf->bytes_avail);
    out_ind += buf->bytes_avail;
    out[out_ind] = '\0';
    out_space_remaining -= buf->bytes_avail;
    buf->nextout = buf->nextin = buf->bytes_avail = 0;
    result = fill_buffer(buf, fd);
    if (result < 0)
      return(NULL);
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
    if (!ih->fin) {
      fprintf(stderr, "failed to open input file for read\n");
      exit(-1);
    }
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

int bz2_eof_i(InputHandler *ih) {
  if (feof(ih->fin) && isempty(ih->bz_buffer))
    return(1);
  return(0);
}

int gz_open_i(InputHandler *ih) {
  if (ih->path != NULL) {
    ih->gzstream = gzopen(ih->path, "rb");
    gzbuffer(ih->gzstream, ih->gz_bufsize);
  }
  if (!ih->gzstream) {
    fprintf(stderr, "error trying to open %s for decompression\n",
            ih->path);
    exit(-1);
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

int gz_eof_i(InputHandler *ih) {
  if (gzeof(ih->gzstream))
    return(1);
  return(0);
}

int txt_open_i(InputHandler *ih) {
  if (ih->path != NULL) {
    ih->fin = fopen(ih->path, "r");
    if (!ih->fin) {
      fprintf(stderr, "failed to open %s for decompression\n", ih->path);
      exit(-1);
    }
  }
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

int txt_eof_i(InputHandler *ih) {
  if (feof(ih->fin))
	   return(1);
  return(0);
}

OutputHandler *outputhandler_init(char *path) {
  OutputHandler *oh = NULL;
  char *dotPosition = NULL;

  oh = (OutputHandler *)malloc(sizeof(OutputHandler));
  if (oh == NULL) {
    fprintf(stderr, "failed to allocate output handler\n");
    exit(1);
  }
  oh->open = NULL;

  oh->path = path;
  oh->fout = NULL;
  oh->bzstream = NULL;
  oh->bzerror = 0;
  oh->bz_blocksize = 9;  /* 900k */
  oh->bz_verbosity = 0;  /* no extra messages */
  oh->bz_workfactor = 0; /* use the default */

  oh->gzstream = NULL;
  oh->gz_bufsize = 65536;

  oh->bytes_in_low = 0;
  oh->bytes_in_hi = 0;
  oh->bytes_out_low = 0;
  oh->bytes_out_hi = 0;

  oh->bytes_in_low_cumul = 0;
  oh->bytes_in_hi_cumul = 0;
  oh->bytes_out_low_cumul = 0;
  oh->bytes_out_hi_cumul = 0;

  oh->offset_gz = 0;
  oh->offset_txt = 0;

  oh->closed = 1;

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
    dotPosition = strrchr(path, '.');
    if (dotPosition != NULL) {
      *dotPosition = '\0';
      if (strlen(path) > 4 && !strcmp(path+(strlen(path)-4),".bz2")) {
        /* filename ends in .bz2.something */
	oh->open = bz2_open_o;
	oh->write = bz2_write_o;
	oh->close = bz2_close_o;
      }
      else if (strlen(path) > 3 && !strcmp(path+(strlen(path)-3),".gz")) {
        /* filename ends in .gz.something */
	oh->open = gz_open_o;
	oh->write = gz_write_o;
	oh->close = gz_close_o;
      }
      *dotPosition = '.';
    }
  }

  if (oh->open == NULL) {
    /* not set in stanzas above */
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
  oh->closed = 0;
  return(1);
}

int bz2_open_a(OutputHandler *oh) {
  if (oh->path != NULL)
    oh->fout = fopen(oh->path, "a");

  oh->bzstream = BZ2_bzWriteOpen(&(oh->bzerror), oh->fout, oh->bz_blocksize,
                                 oh->bz_verbosity, oh->bz_workfactor);
  if (oh->bzerror != BZ_OK) {
    fprintf(stderr, "error %d trying to open %s for compression\n",
            oh->bzerror, oh->path);
    exit(-1);
  }
  oh->closed = 0;
  return(1);
}

void outputhandler_appendmode(OutputHandler *oh) {
  char *dotPosition = NULL;

  oh->open = NULL;

  if (strlen(oh->path) > 4 && !strcmp(oh->path + strlen(oh->path) - 4, ".bz2"))
    oh->open = bz2_open_a;
  else if  (strlen(oh->path) > 3 && !strcmp(oh->path + strlen(oh->path) - 3, ".gz"))
    oh->open = gz_open_a;
  else {
    dotPosition = strrchr(oh->path, '.');
    if (dotPosition != NULL) {
      *dotPosition = '\0';
      if (strlen(oh->path) > 4 && !strcmp(oh->path+(strlen(oh->path)-4),".bz2")) {
        /* filename ends in .bz2.something */
	oh->open = bz2_open_a;
      }
      else if (strlen(oh->path) > 3 && !strcmp(oh->path+(strlen(oh->path)-3),".gz")) {
        /* filename ends in .gz.something */
	oh->open = gz_open_a;
      }
      *dotPosition = '.';
    }
  }

  if (oh->open == NULL) {
    /* not set in stanzas above */
    oh->open = txt_open_a;
  }
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
  off_t counter;

  BZ2_bzWriteClose64(&(oh->bzerror), oh->bzstream, 0,
		     &(oh->bytes_in_low), &(oh->bytes_in_hi),
		     &(oh->bytes_out_low), &(oh->bytes_out_hi));

  counter = ((off_t)oh->bytes_out_low_cumul | (off_t)oh->bytes_out_hi_cumul << 32);
  counter += ((off_t)oh->bytes_out_low | (off_t)oh->bytes_out_hi << 32);
  oh->bytes_out_low_cumul = (unsigned int) (counter & 0xffffffff);
  oh->bytes_out_hi_cumul = (unsigned int) (counter >> 32) & 0xffffffff;

  counter = ((off_t)oh->bytes_in_low_cumul | (off_t)oh->bytes_in_hi_cumul << 32);
  counter += ((off_t)oh->bytes_in_low | (off_t)oh->bytes_in_hi << 32);
  oh->bytes_in_low_cumul = (unsigned int) (counter & 0xffffffff);
  oh->bytes_in_hi_cumul = (unsigned int) (counter >> 32) & 0xffffffff;

  if (oh->fout != stdout)
    fclose(oh->fout);
  oh->closed = 1;
  return(0);
}

int gz_open_o(OutputHandler *oh) {
  oh->gzstream = gzopen(oh->path, "w");
  gzbuffer(oh->gzstream, oh->gz_bufsize);
  oh->closed = 0;
  return(0);
}

int gz_open_a(OutputHandler *oh) {
  oh->gzstream = gzopen(oh->path, "a");
  gzbuffer(oh->gzstream, oh->gz_bufsize);
  oh->closed = 0;
  return(0);
}

int gz_write_o(OutputHandler *oh, char *buffer, int bytecount) {
  return(gzwrite(oh->gzstream, buffer, bytecount));
}

int gz_close_o(OutputHandler *oh) {
  gzflush(oh->gzstream, Z_FINISH);
  oh->offset_gz = gzoffset(oh->gzstream);
  gzclose(oh->gzstream);
  oh->closed = 1;
  return(0);
}

int txt_open_o(OutputHandler *oh) {
  oh->fout = fopen(oh->path, "w");
  oh->closed = 0;
  return(0);
}

int txt_open_a(OutputHandler *oh) {
  oh->fout = fopen(oh->path, "a");
  oh->closed = 0;
  return(0);
}

int txt_write_o(OutputHandler *oh, char *buffer, int bytecount) {
 size_t results;

  results = fwrite(buffer, 1, bytecount, oh->fout);
  oh->offset_txt += results;
  return(results);
}

int txt_close_o(OutputHandler *oh) {
  fclose(oh->fout);
  oh->closed = 1;
  return(0);
}

off_t outputhandler_get_offset(OutputHandler *oh) {
  if (oh->gzstream)
    return oh->offset_gz;
  else if (oh->bzstream)
    return ((off_t)oh->bytes_out_low_cumul | ((off_t)oh->bytes_out_hi_cumul << 32));
  else
    return oh->offset_txt;
}
