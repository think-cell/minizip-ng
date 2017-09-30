/* ioapi_buf.c -- IO base function header for compress/uncompress .zip
   files using zlib + zip or unzip API

   This version of ioapi is designed to buffer IO.

   Copyright (C) 2012-2017 Nathan Moinvaziri
      https://github.com/nmoinvaz/minizip

   This program is distributed under the terms of the same license as zlib.
   See the accompanying LICENSE file for the full text of the license.
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "zlib.h"
#include "ioapi.h"

#include "ioapi_buf.h"

#ifndef IOBUF_BUFFERSIZE
#  define IOBUF_BUFFERSIZE (UINT16_MAX)
#endif

#if defined(_WIN32)
#  include <conio.h>
#  define PRINTF  _cprintf
#  define VPRINTF _vcprintf
#else
#  define PRINTF  printf
#  define VPRINTF vprintf
#endif

//#define IOBUF_VERBOSE

#ifdef __GNUC__
#ifndef max
#define max(x,y) ({ \
const typeof(x) _x = (x);	\
const typeof(y) _y = (y);	\
(void) (&_x == &_y);		\
_x > _y ? _x : _y; })
#endif /* __GNUC__ */

#ifndef min
#define min(x,y) ({ \
const typeof(x) _x = (x);	\
const typeof(y) _y = (y);	\
(void) (&_x == &_y);		\
_x < _y ? _x : _y; })
#endif
#endif

typedef struct mzstream_buffered_s {
  mzstream  stream;
  char      readbuf[IOBUF_BUFFERSIZE];
  uint32_t  readbuf_len;
  uint32_t  readbuf_pos;
  uint32_t  readbuf_hits;
  uint32_t  readbuf_misses;
  char      writebuf[IOBUF_BUFFERSIZE];
  uint32_t  writebuf_len;
  uint32_t  writebuf_pos;
  uint32_t  writebuf_hits;
  uint32_t  writebuf_misses;
  uint64_t  position;
  int32_t   error;
} mzstream_buffered;

#if defined(IOBUF_VERBOSE)
#  define mzstream_buffered_print(o,s,f,...) mzstream_buffered_print_internal(o,s,f,__VA_ARGS__);
#else
#  define mzstream_buffered_print(o,s,f,...)
#endif

void mzstream_buffered_printinternal(voidpf stream, char *format, ...)
{
    mzstream_buffered *buffered = (mzstream_buffered *)stream;
    va_list arglist;
    PRINTF("Buf stream %p - ", buffered);
    va_start(arglist, format);
    VPRINTF(format, arglist);
    va_end(arglist);
}

int32_t ZCALLBACK mzstream_buffered_open(voidpf stream, const char *filename, int mode)
{
    mzstream_buffered *buffered = (mzstream_buffered *)stream;
    mzstream_buffered_print(opaque, buffered, "open [num %d mode %d]\n", number_disk, mode);
    return mzstream_open(stream, filename, mode);
}

int32_t mzstream_buffered_flush(voidpf stream, uint32_t *written)
{
    mzstream_buffered *buffered = (mzstream_buffered *)stream;
    uint32_t total_bytes_written = 0;
    uint32_t bytes_to_write = buffered->writebuf_len;
    uint32_t bytes_left_to_write = buffered->writebuf_len;
    uint32_t bytes_written = 0;

    *written = 0;

    while (bytes_left_to_write > 0)
    {
        bytes_written = mzstream_write(buffered->stream.base, buffered->writebuf + (bytes_to_write - bytes_left_to_write), bytes_left_to_write);
        if (bytes_written != bytes_left_to_write)
            return MZSTREAM_ERR;

        buffered->writebuf_misses += 1;

        mzstream_buffered_print(opaque, stream, "write flush [%d:%d len %d]\n", bytes_to_write, bytes_left_to_write, buffered->writebuf_len);

        total_bytes_written += bytes_written;
        bytes_left_to_write -= bytes_written;
        buffered->position += bytes_written;
    }

    buffered->writebuf_len = 0;
    buffered->writebuf_pos = 0;

    *written = total_bytes_written;
    return MZSTREAM_OK;
}

int32_t ZCALLBACK mzstream_buffered_read(voidpf stream, void *buf, uint32_t size)
{
    mzstream_buffered *buffered = (mzstream_buffered *)stream;
    uint32_t buf_len = 0;
    uint32_t bytes_to_read = 0;
    uint32_t bytes_to_copy = 0;
    uint32_t bytes_left_to_read = size;
    uint32_t bytes_read = 0;

    mzstream_buffered_print(opaque, stream, "read [size %ld pos %lld]\n", size, buffered->position);

    if (buffered->writebuf_len > 0)
        mzstream_buffered_print(opaque, stream, "switch from write to read, not yet supported [%lld]\n", buffered->position);

    while (bytes_left_to_read > 0)
    {
        if ((buffered->readbuf_len == 0) || (buffered->readbuf_pos == buffered->readbuf_len))
        {
            if (buffered->readbuf_len == IOBUF_BUFFERSIZE)
            {
                buffered->readbuf_pos = 0;
                buffered->readbuf_len = 0;
            }

            bytes_to_read = IOBUF_BUFFERSIZE - (buffered->readbuf_len - buffered->readbuf_pos);

            if (mzstream_read(buffered->stream.base, buffered->readbuf + buffered->readbuf_pos, bytes_to_read) != bytes_to_read)
                return 0;

            buffered->readbuf_misses += 1;
            buffered->readbuf_len += bytes_read;
            buffered->position += bytes_read;

            mzstream_buffered_print(opaque, stream, "filled [read %d/%d buf %d:%d pos %lld]\n", bytes_read, bytes_to_read, buffered->readbuf_pos, buffered->readbuf_len, buffered->position);

            if (bytes_read == 0)
                break;
        }

        if ((buffered->readbuf_len - buffered->readbuf_pos) > 0)
        {
            bytes_to_copy = min(bytes_left_to_read, (uint32_t)(buffered->readbuf_len - buffered->readbuf_pos));
            memcpy((char *)buf + buf_len, buffered->readbuf + buffered->readbuf_pos, bytes_to_copy);

            buf_len += bytes_to_copy;
            bytes_left_to_read -= bytes_to_copy;

            buffered->readbuf_hits += 1;
            buffered->readbuf_pos += bytes_to_copy;

            mzstream_buffered_print(opaque, stream, "emptied [copied %d remaining %d buf %d:%d pos %lld]\n", bytes_to_copy, bytes_left_to_read, buffered->readbuf_pos, buffered->readbuf_len, buffered->position);
        }
    }

    return size - bytes_left_to_read;
}

int32_t ZCALLBACK mzstream_buffered_write(voidpf stream, const void *buf, uint32_t size)
{
    mzstream_buffered *buffered = (mzstream_buffered *)stream;
    uint32_t bytes_to_write = size;
    uint32_t bytes_left_to_write = size;
    uint32_t bytes_to_copy = 0;
    uint32_t bytes_flushed = 0;
    int64_t ret = 0;


    mzstream_buffered_print(opaque, stream, "write [size %ld len %d pos %lld]\n", size, buffered->writebuf_len, buffered->position);

    if (buffered->readbuf_len > 0)
    {
        buffered->position -= buffered->readbuf_len;
        buffered->position += buffered->readbuf_pos;

        buffered->readbuf_len = 0;
        buffered->readbuf_pos = 0;

        mzstream_buffered_print(opaque, stream, "switch from read to write [%lld]\n", buffered->position);

        if (mzstream_seek(buffered->stream.base, buffered->position, MZSTREAM_SEEK_SET) == MZSTREAM_ERR)
            return MZSTREAM_ERR;
    }

    while (bytes_left_to_write > 0)
    {
        bytes_to_copy = min(bytes_left_to_write, (uint32_t)(IOBUF_BUFFERSIZE - min(buffered->writebuf_len, buffered->writebuf_pos)));

        if (bytes_to_copy == 0)
        {
            if (mzstream_buffered_flush(stream, &bytes_flushed) == MZSTREAM_ERR)
                return MZSTREAM_ERR;
            if (bytes_flushed == 0)
                return 0;

            continue;
        }

        memcpy(buffered->writebuf + buffered->writebuf_pos, (char *)buf + (bytes_to_write - bytes_left_to_write), bytes_to_copy);

        mzstream_buffered_print(opaque, stream, "write copy [remaining %d write %d:%d len %d]\n", bytes_to_copy, bytes_to_write, bytes_left_to_write, buffered->writebuf_len);

        bytes_left_to_write -= bytes_to_copy;

        buffered->writebuf_pos += bytes_to_copy;
        buffered->writebuf_hits += 1;
        if (buffered->writebuf_pos > buffered->writebuf_len)
            buffered->writebuf_len += buffered->writebuf_pos - buffered->writebuf_len;
    }

    return size - bytes_left_to_write;
}

int64_t mzstream_buffered_tellinternal(voidpf stream, uint64_t position)
{
    mzstream_buffered *buffered = (mzstream_buffered *)stream;
    buffered->position = position;
    mzstream_buffered_print(opaque, stream, "tell [pos %llu readpos %d writepos %d err %d]\n", buffered->position, buffered->readbuf_pos, buffered->writebuf_pos, errno);
    if (buffered->readbuf_len > 0)
        position -= (buffered->readbuf_len - buffered->readbuf_pos);
    if (buffered->writebuf_len > 0)
        position += buffered->writebuf_pos;
    return position;
}

int64_t ZCALLBACK mzstream_buffered_tell(voidpf stream)
{
    mzstream_buffered *buffered = (mzstream_buffered *)stream;
    int64_t position = mzstream_tell(buffered->stream.base);
    return mzstream_buffered_tellinternal(stream, position);
}

int mzstream_buffered_seekinternal(voidpf stream, uint64_t offset, int origin)
{
    mzstream_buffered *buffered = (mzstream_buffered *)stream;
    uint32_t bytes_flushed = 0;

    mzstream_buffered_print(opaque, stream, "seek [origin %d offset %llu pos %lld]\n", origin, offset, buffered->position);

    switch (origin)
    {
        case MZSTREAM_SEEK_SET:

            if (buffered->writebuf_len > 0)
            {
                if ((offset >= buffered->position) && (offset <= buffered->position + buffered->writebuf_len))
                {
                    buffered->writebuf_pos = (uint32_t)(offset - buffered->position);
                    return MZSTREAM_OK;
                }
            }
            if ((buffered->readbuf_len > 0) && (offset < buffered->position) && (offset >= buffered->position - buffered->readbuf_len))
            {
                buffered->readbuf_pos = (uint32_t)(offset - (buffered->position - buffered->readbuf_len));
                return MZSTREAM_OK;
            }

            if (mzstream_buffered_flush(stream, &bytes_flushed) == MZSTREAM_ERR)
                return MZSTREAM_ERR;

            buffered->position = offset;
            break;

        case MZSTREAM_SEEK_CUR:

            if (buffered->readbuf_len > 0)
            {
                if (offset <= (buffered->readbuf_len - buffered->readbuf_pos))
                {
                    buffered->readbuf_pos += (uint32_t)offset;
                    return MZSTREAM_OK;
                }
                offset -= (buffered->readbuf_len - buffered->readbuf_pos);
                buffered->position += offset;
            }
            if (buffered->writebuf_len > 0)
            {
                if (offset <= (buffered->writebuf_len - buffered->writebuf_pos))
                {
                    buffered->writebuf_pos += (uint32_t)offset;
                    return MZSTREAM_OK;
                }
                //offset -= (buffered->writebuf_len - buffered->writebuf_pos);
            }

            if (mzstream_buffered_flush(stream, &bytes_flushed) == MZSTREAM_ERR)
                return MZSTREAM_ERR;

            break;

        case MZSTREAM_SEEK_END:

            if (buffered->writebuf_len > 0)
            {
                buffered->writebuf_pos = buffered->writebuf_len;
                return MZSTREAM_OK;
            }
            break;
    }

    buffered->readbuf_len = 0;
    buffered->readbuf_pos = 0;
    buffered->writebuf_len = 0;
    buffered->writebuf_pos = 0;
    return MZSTREAM_ERR;
}

int32_t ZCALLBACK mzstream_buffered_seek(voidpf stream, uint64_t offset, int origin)
{
    mzstream_buffered *buffered = (mzstream_buffered *)stream;
    if (mzstream_buffered_seekinternal(stream, offset, origin) == MZSTREAM_ERR)
        return MZSTREAM_ERR;
    return mzstream_seek(buffered->stream.base, offset, origin);
}

int32_t ZCALLBACK mzstream_buffered_close(voidpf stream)
{
    mzstream_buffered *buffered = (mzstream_buffered *)stream;
    uint32_t bytes_flushed = 0;
    mzstream_buffered_flush(stream, &bytes_flushed);
    mzstream_buffered_print(opaque, stream, "close\n");
    if (buffered->readbuf_hits + buffered->readbuf_misses > 0)
        mzstream_buffered_print(opaque, stream, "read efficency %.02f%%\n", (buffered->readbuf_hits / ((float)buffered->readbuf_hits + buffered->readbuf_misses)) * 100);
    if (buffered->writebuf_hits + buffered->writebuf_misses > 0)
        mzstream_buffered_print(opaque, stream, "write efficency %.02f%%\n", (buffered->writebuf_hits / ((float)buffered->writebuf_hits + buffered->writebuf_misses)) * 100);
    return mzstream_close(&buffered->stream.base);
}

int32_t ZCALLBACK mzstream_buffered_error(voidpf stream)
{
    mzstream_buffered *buffered = (mzstream_buffered *)stream;
    return mzstream_error(&buffered->stream.base);
}

voidpf mzstream_buffered_alloc(void)
{
    mzstream_buffered *buffered = NULL;

    buffered = (mzstream_buffered *)malloc(sizeof(mzstream_buffered));
    if (buffered == NULL)
        return NULL;

    memset(buffered, 0, sizeof(mzstream_buffered));

    buffered->stream.open = mzstream_buffered_open;
    buffered->stream.read = mzstream_buffered_read;
    buffered->stream.write = mzstream_buffered_write;
    buffered->stream.tell = mzstream_buffered_tell;
    buffered->stream.seek = mzstream_buffered_seek;
    buffered->stream.close = mzstream_buffered_close;
    buffered->stream.error = mzstream_buffered_error;
    buffered->stream.alloc = mzstream_buffered_alloc;
    buffered->stream.free = mzstream_buffered_free;

    return (voidpf)buffered;
}

void mzstream_buffered_free(voidpf stream)
{
    mzstream_buffered *buffered = (mzstream_buffered *)stream;
    if (buffered != NULL)
        free(buffered);
}
