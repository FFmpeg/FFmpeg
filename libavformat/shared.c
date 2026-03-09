/*
 * Shared file cache protocol.
 * Copyright (c) 2026 Niklas Haas
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Based on cache.c by Michael Niedermayer
 */

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/crc.h"
#include "libavutil/error.h"
#include "libavutil/hash.h"
#include "libavutil/file_open.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"

#include "url.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * This hash should be resistant against collision attacks, so that an
 * attacker could not generate e.g. two different URIs that map to the same
 * cache file. This requires at least 64 bits of collision resistance in
 * practice (i.e. 128 bits = 16 bytes of hash size). However, we can be
 * conservative by computing e.g. a 256 bit hash and storing it inside the
 * file header for verification.
 *
 * Note that due to the way we use atomics, we should avoid zero bytes in
 * the resulting hash; hence we tweak the input slightly to avoid this.
 * The resulting loss in hash strength is negligible, since 32 bytes is
 * already much more than needed.
 */
#define HASH_METHOD    "SHA512/256"
#define HASH_SIZE      32

static int hash_uri(uint8_t hash[HASH_SIZE], const char *uri)
{
    struct AVHashContext *ctx = NULL;
    int ret = av_hash_alloc(&ctx, HASH_METHOD);
    if (ret < 0)
        return ret;

    av_assert0(av_hash_get_size(ctx) == HASH_SIZE);
    av_hash_init(ctx);
    av_hash_update(ctx, (const uint8_t *) uri, strlen(uri));
    av_hash_final(ctx, hash);
    av_hash_freep(&ctx);

    for (int i = 0; i < HASH_SIZE; i++)
        hash[i] = hash[i] ? hash[i] : ~hash[i]; /* prevent zero bytes */
    return 0;
}

#define HEADER_MAGIC   MKTAG(u'\xFF', 'S', 'h', '$')
#define HEADER_VERSION 2

enum BlockState {
    /* Reserved block state values */
    BLOCK_NONE = 0, ///< block is not cached
    BLOCK_PENDING,  ///< a thread is currently trying to write this block
    BLOCK_FAILED,   ///< the underlying I/O source failed to read this block

    /**
     * All other block states represent valid cached blocks, with the value
     * being the CRC of the block data.
     */
};

static uint16_t get_block_crc(const uint8_t *block, size_t block_size)
{
    uint16_t crc = av_crc(av_crc_get_table(AV_CRC_16_ANSI), 0, block, block_size);
    switch (crc) {
    case BLOCK_NONE:
    case BLOCK_FAILED:
    case BLOCK_PENDING:
        return ~crc; /* avoid reserved block states */
    default:
        return crc;
    }
}

typedef struct Block {
    atomic_ushort state; /* enum BlockState */
} Block;

typedef struct Spacemap {
    atomic_uint header_magic;
    atomic_ushort version;
    atomic_ushort block_shift;
    atomic_ullong filesize; /* byte offset of true EOF, or 0 if unknown */
    atomic_uchar hash[HASH_SIZE]; /* hash of resource URI / filename */
    char reserved[80];

    Block blocks[];
} Spacemap;

/* Set to value iff the current value is unset (zero) */
#define DEF_SET_ONCE(ctype, atype)                                              \
    static int set_once_##atype(atomic_##atype *const ptr, const ctype value)   \
    {                                                                           \
        ctype prev = 0;                                                         \
        av_assert1(value != 0);                                                 \
        if (atomic_compare_exchange_strong_explicit(                            \
                ptr, &prev, value, memory_order_release, memory_order_relaxed)) \
            return 1;                                                           \
        else if (prev == value)                                                 \
            return 0;                                                           \
        else                                                                    \
            return AVERROR(EINVAL);                                             \
    }

DEF_SET_ONCE(unsigned char,      uchar)
DEF_SET_ONCE(unsigned int,       uint)
DEF_SET_ONCE(unsigned short,     ushort)
DEF_SET_ONCE(unsigned long long, ullong)

typedef struct SharedContext {
    AVClass *class;
    URLContext *inner;
    int64_t inner_pos;

    /* options */
    char *cache_dir;
    int block_shift; ///< requested shift; may disagree with actual
    int read_only;
    int64_t timeout;
    int retry_errors;
    int verify;

    /* misc state */
    int64_t pos; ///< current logical position
    uint8_t *tmp_buf;
    int block_size;
    int write_err; ///< write error occurred

    /* cache file */
    uint8_t *cache_data; ///< optional mmap of the cache file
    char *cache_path;
    off_t cache_size; ///< size of mapped memory region (for munmap)
    int fd;

    /* space map */
    Spacemap *spacemap;
    char *map_path;
    off_t map_size;
    int mapfd;

    /* statistics */
    int64_t nb_hit;
    int64_t nb_miss;
} SharedContext;

static int shared_close(URLContext *h)
{
    SharedContext *s = h->priv_data;

    ffurl_close(s->inner);
    if (s->cache_data)
        munmap(s->cache_data, s->cache_size);
    if (s->spacemap)
        munmap(s->spacemap, s->map_size);
    if (s->fd != -1)
        close(s->fd);
    if (s->mapfd != -1)
        close(s->mapfd);
    av_freep(&s->cache_path);
    av_freep(&s->map_path);
    av_freep(&s->tmp_buf);

    av_log(h, AV_LOG_DEBUG, "Cache statistics: %"PRId64" hits, %"PRId64" misses\n",
           s->nb_hit, s->nb_miss);
    return 0;
}

static int cache_map(URLContext *h, int64_t filesize);
static int spacemap_init(URLContext *h, const uint8_t hash[HASH_SIZE]);
static int spacemap_grow(URLContext *h, int64_t block);

static int64_t get_filesize(URLContext *h)
{
    SharedContext *s = h->priv_data;
    return atomic_load_explicit(&s->spacemap->filesize, memory_order_relaxed);
}

static int set_filesize(URLContext *h, int64_t new_size)
{
    SharedContext *s = h->priv_data;
    int ret;

    if (!new_size)
        return 0;

    ret = set_once_ullong(&s->spacemap->filesize, new_size);
    if (ret < 0) {
        av_log(h, AV_LOG_ERROR, "Cached file size mismatch, expected: "
                "%"PRId64", got: %"PRIu64"!\n", new_size,
                (uint64_t) atomic_load(&s->spacemap->filesize));
        return ret;
    } else if (ret) {
        /* Opportunistically map the file; this also sets the correct filesize.
         * Ignore errors as this is not critical to the cache logic. */
        cache_map(h, new_size);
    }

    return ret;
}

static int shared_open(URLContext *h, const char *arg, int flags, AVDictionary **options)
{
    SharedContext *s = h->priv_data;
    int ret;

    if (!s->cache_dir || !s->cache_dir[0]) {
        av_log(h, AV_LOG_ERROR, "Missing path for shared cache! Specify a "
               "directory using the -cache_dir option.\n");
        return AVERROR(EINVAL);
    }

    s->fd = s->mapfd = -1; /* Set these early for shared_close() failure path */

    /* Open underlying protocol */
    av_strstart(arg, "shared:", &arg);
    ret = ffurl_open_whitelist(&s->inner, arg, flags, &h->interrupt_callback,
                               options, h->protocol_whitelist, h->protocol_blacklist, h);

    if (ret < 0)
        goto fail;

    uint8_t hash[HASH_SIZE];
    ret = hash_uri(hash, arg);
    if (ret < 0)
        goto fail;

    /* 128 bits is enough for collision resistance; we already store the full
     * hash inside the header for verification */
    char filename[2 * 16 + 1];
    for (int i = 0; i < FF_ARRAY_ELEMS(filename) / 2; i++)
        sprintf(&filename[i * 2], "%02X", hash[i]);
    s->cache_path = av_asprintf("%s/%s.cache",    s->cache_dir, filename);
    s->map_path   = av_asprintf("%s/%s.spacemap", s->cache_dir, filename);
    if (!s->cache_path || !s->map_path) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    av_log(h, AV_LOG_VERBOSE, "Opening cache file '%s' for URI: '%s'\n",
           s->cache_path, s->inner->filename);

    s->fd    = avpriv_open(s->cache_path, O_RDWR | O_CREAT, 0660);
    s->mapfd = avpriv_open(s->map_path,   O_RDWR | O_CREAT, 0660);
    if (s->fd < 0 || s->mapfd < 0) {
        ret = AVERROR(errno);
        av_log(h, AV_LOG_ERROR, "Failed to open '%s': %s\n",
               s->fd < 0 ? s->cache_path : s->map_path, av_err2str(ret));
        goto fail;
    }

    ret = spacemap_init(h, hash);
    if (ret < 0)
        goto fail;

    s->block_size = 1 << atomic_load(&s->spacemap->block_shift);

    int64_t filesize = get_filesize(h);
    if (!filesize) {
        /* Filesize is not yet known, try to get it from the underlying URL */
        filesize = ffurl_size(s->inner);
        if (filesize < 0 && filesize != AVERROR(ENOSYS)) {
            ret = (int) filesize;
            goto fail;
        } else if (filesize > 0) {
            ret = set_filesize(h, filesize);
            if (ret < 0)
                goto fail;
        }
    }

    if (filesize > 0) {
        int64_t last_pos = filesize - 1;
        int64_t last_block = last_pos >> atomic_load(&s->spacemap->block_shift);
        ret = spacemap_grow(h, last_block);
        if (ret < 0)
            goto fail;

        /* If filesize is known, we can directly mmap() the cache file */
        ret = cache_map(h, filesize);
        if (ret < 0) {
            av_log(h, AV_LOG_WARNING, "Failed to map cache file: %s. Falling "
                   "back to normal read/write\n", av_err2str(ret));
            ret = 0;
        }
    }

    /* Temporary buffer needed for pread/pwrite() fallback */
    s->tmp_buf = av_malloc(s->block_size);
    if (!s->tmp_buf) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    h->max_packet_size = s->block_size;
    h->min_packet_size = s->block_size;
    ret = 0;

fail:
    if (ret < 0)
        shared_close(h);
    return ret;
}

static int cache_map(URLContext *h, int64_t filesize)
{
    SharedContext *s = h->priv_data;
    if (s->cache_size >= filesize || filesize > SIZE_MAX)
        return 0;

    if (s->cache_data) {
        munmap(s->cache_data, s->cache_size);
        s->cache_data = NULL;
        s->cache_size = 0;
    }

    struct stat st;
    int ret = fstat(s->fd, &st);
    if (ret < 0)
        return AVERROR(errno);

    if (st.st_size != filesize) {
        /* Ensure the file size is correct before mapping; this can happen if
         * another process wrote the correct filesize to the header but
         * crashed right before actually successfully resizing the file. */
        ret = ftruncate(s->fd, filesize);
        if (ret < 0)
            return AVERROR(errno);
    }

    s->cache_data = mmap(NULL, filesize, PROT_READ | PROT_WRITE, MAP_SHARED, s->fd, 0);
    if (s->cache_data == MAP_FAILED) {
        s->cache_data = NULL;
        return AVERROR(errno);
    }

    s->cache_size = filesize;
    return 0;
}

static int spacemap_remap(URLContext *h, size_t map_size)
{
    SharedContext *s = h->priv_data;
    int ret, did_grow = 0, locked = 0;
    if (map_size <= s->map_size)
        return 0;

    /* Opportunistically get current filesize before attempting to lock */
    struct stat st;
    ret = fstat(s->mapfd, &st);
    if (ret < 0) {
        ret = AVERROR(errno);
        goto fail;
    }

    if (st.st_size >= map_size)
        goto skip_resize;

    /* Lock the spacemap to ensure nobody else is currently resizing it */
    ret = flock(s->mapfd, LOCK_EX);
    if (ret < 0) {
        ret = AVERROR(errno);
        goto fail;
    }
    locked = 1;

    /* Refresh filesize after acquiring the lock */
    ret = fstat(s->mapfd, &st);
    if (ret < 0) {
        ret = AVERROR(errno);
        goto fail;
    }

    if (st.st_size >= map_size)
        goto skip_resize;

    ret = ftruncate(s->mapfd, map_size);
    if (ret < 0) {
        ret = AVERROR(errno);
        goto fail;
    }
    st.st_size = map_size;
    did_grow = 1;

skip_resize:
    if (s->spacemap)
        munmap(s->spacemap, s->map_size);
    s->map_size = st.st_size;
    s->spacemap = mmap(NULL, s->map_size, PROT_READ | PROT_WRITE, MAP_SHARED, s->mapfd, 0);
    if (s->spacemap == MAP_FAILED) {
        s->spacemap = NULL; /* for munmap check */
        s->map_size = 0;
        ret = AVERROR(errno);
        goto fail;
    }

    if (locked) {
        flock(s->mapfd, LOCK_UN);
        locked = 0;
    }

    return did_grow;

fail:
    if (locked)
        flock(s->mapfd, LOCK_UN);
    av_log(h, AV_LOG_ERROR, "Failed to resize space map: %s\n", av_err2str(ret));
    return ret;
}

static int spacemap_grow(URLContext *h, int64_t block)
{
    SharedContext *s = h->priv_data;
    int64_t num_blocks = block + 1;
    size_t map_bytes = sizeof(Spacemap) + num_blocks * sizeof(Block);

    /* When streaming files without known size, round up the number of blocks
     * to the nearest multiple of the block size to reduce the rate of resizes */
    if (!get_filesize(h)) {
        av_assert0(s->block_size > 0);
        map_bytes = FFALIGN(map_bytes, (int64_t) s->block_size);
    }

    if (map_bytes < num_blocks)
        return AVERROR(EINVAL); /* overflow */

    const off_t old_size = s->map_size;
    int ret = spacemap_remap(h, map_bytes);
    if (ret < 0)
        return ret;

    /* Report new size after successful grow */
    if (s->map_size > old_size) {
        num_blocks = (s->map_size - sizeof(Spacemap)) / sizeof(Block);
        av_log(h, AV_LOG_DEBUG,
               "%s %zu bytes, capacity: %"PRId64" blocks = %zu MB\n",
               ret ? "Resized spacemap to" : "Mapped spacemap with",
               (size_t) s->map_size, num_blocks,
               (num_blocks * (int64_t) s->block_size) >> 20);
    }
    return 0;
}

static int spacemap_init(URLContext *h, const uint8_t hash[HASH_SIZE])
{
    SharedContext *s = h->priv_data;
    int ret;

    ret = spacemap_remap(h, sizeof(Spacemap));
    if (ret < 0)
        return ret;

    if ((ret = set_once_uint(&s->spacemap->header_magic, HEADER_MAGIC)) < 0 ||
        (ret = set_once_ushort(&s->spacemap->version, HEADER_VERSION)) < 0)
    {
        av_log(h, AV_LOG_ERROR, "Shared cache spacemap header mismatch!\n");
        av_log(h, AV_LOG_ERROR, "  Expected magic: 0x%X, version: %d\n",
               HEADER_MAGIC, HEADER_VERSION);
        av_log(h, AV_LOG_ERROR, "  Got      magic: 0x%X, version: %d\n",
               atomic_load(&s->spacemap->header_magic),
               atomic_load(&s->spacemap->version));
        return ret;
    }

    ret = set_once_ushort(&s->spacemap->block_shift, s->block_shift);
    if (ret < 0) {
        const int shift = atomic_load(&s->spacemap->block_shift);
        av_log(h, AV_LOG_WARNING, "Shared cache uses block shift %d, "
               "but requested block shift is %d.\n", shift, s->block_shift);
        if (shift < 9 || shift > 30) {
            av_log(h, AV_LOG_ERROR, "Invalid block shift %d in cache file!\n", shift);
            return AVERROR(EINVAL);
        }
    }

    for (int i = 0; i < HASH_SIZE; i++) {
        ret = set_once_uchar(&s->spacemap->hash[i], hash[i]);
        if (ret < 0) {
            av_log(h, AV_LOG_ERROR, "Shared cache spacemap hash mismatch!\n");
            av_log(h, AV_LOG_ERROR, "  Expected hash: ");
            for (int j = 0; j < 32; j++)
                av_log(h, AV_LOG_ERROR, "%02X", hash[j]);
            av_log(h, AV_LOG_ERROR, "\n  Got      hash: ");
            for (int j = 0; j < 32; j++)
                av_log(h, AV_LOG_ERROR, "%02X", atomic_load(&s->spacemap->hash[j]));
            av_log(h, AV_LOG_ERROR, "\n");
            return ret;
        }
    }

    if (ret) /* set_once() return 1 if this is the first time setting the value */
        av_log(h, AV_LOG_DEBUG, "Initialized new cache spacemap.\n");

    return ret;
}

static int read_cache(SharedContext *s, uint8_t *buf, size_t size, off_t offset)
{
    if (s->cache_data) {
        av_assert1(offset + size <= s->cache_size);
        memcpy(buf, s->cache_data + offset, size);
        return 0;
    }

    while (size) {
        ssize_t ret = pread(s->fd, buf, size, offset);
        if (ret <= 0)
            return ret ? AVERROR(errno) : AVERROR(EIO);
        buf    += ret;
        offset += ret;
        size   -= ret;
    }

    return 0;
}

static int write_cache(SharedContext *s, const uint8_t *buf, size_t size, off_t offset)
{
    if (s->cache_data) {
        av_assert1(offset + size <= s->cache_size);
        memcpy(s->cache_data + offset, buf, size);
        return 0;
    }

    while (size) {
        ssize_t ret = pwrite(s->fd, buf, size, offset);
        if (ret <= 0)
            return ret ? AVERROR(errno) : AVERROR(EIO);
        buf    += ret;
        offset += ret;
        size   -= ret;
    }

    return 0;
}

static size_t clamp_size(URLContext *h, size_t size, int64_t pos)
{
    const int64_t filesize = get_filesize(h);
    if (!filesize)
        return size;
    else if (pos > filesize)
        return 0;
    else
        return FFMIN(filesize - pos, size);
}

static int shared_read(URLContext *h, unsigned char *buf, int size)
{
    SharedContext *s = h->priv_data;
    uint8_t *tmp;
    int ret;

    if (size <= 0)
        return 0;

    size = clamp_size(h, size, s->pos);
    if (size <= 0)
        return AVERROR_EOF;

    const int shift = atomic_load_explicit(&s->spacemap->block_shift, memory_order_relaxed);
    const int64_t block_id = s->pos >> shift;
    const int64_t offset = s->pos & (s->block_size - 1);
    const int64_t block_pos = block_id * s->block_size;
    int block_size = clamp_size(h, s->block_size, block_pos);
    ret = spacemap_grow(h, block_id);
    if (ret < 0)
        return ret;

    Block *const block = &s->spacemap->blocks[block_id];
    unsigned short state = atomic_load_explicit(&block->state, memory_order_acquire);
    int64_t pending_since = 0;
    int verify_read = 0, is_race = 0;

retry:
    switch (state) {
    default:
        /* We always need to read the entire block to verify integrity */
        block_size = clamp_size(h, block_size, block_pos); /* filesize may have changed */
        if (s->cache_data) {
            av_assert1(block_pos + block_size <= s->cache_size);
            tmp = s->cache_data + block_pos;
        } else {
            tmp = s->tmp_buf;
            ret = read_cache(s, tmp, block_size, block_pos);
            if (ret < 0) {
                av_log(h, AV_LOG_ERROR, "Failed to read from cache file: %s\n", av_err2str(ret));
                return ret;
            }
        }

        uint16_t crc = get_block_crc(tmp, block_size);
        if (crc != state) {
            av_log(h, AV_LOG_ERROR, "Cache corruption detected for block 0x%"PRIx64" at "
                   "offset 0x%"PRIx64": expected CRC: 0x%04X, got: 0x%04X\n",
                   block_id, block_pos, state, crc);
            return AVERROR(EIO);
        }

        tmp += (ptrdiff_t) offset;
        size = FFMIN(size, block_size - offset);
        if (s->verify) {
            verify_read = 1;
            break; /* fall through to the cache miss logic */
        }

        memcpy(buf, tmp, size);
        s->nb_hit++;
        s->pos += size;
        return size;

    case BLOCK_FAILED:
        if (!s->retry_errors)
            return AVERROR(EIO);
        av_fallthrough;
    case BLOCK_NONE:
        if (s->read_only)
            break; /* don't mark block as pending */
        if (atomic_compare_exchange_weak_explicit(&block->state, &state,
                                                  BLOCK_PENDING,
                                                  memory_order_acquire,
                                                  memory_order_acquire))
        {
            /* Acquired pending state, proceed to fetch the block */
            state = BLOCK_PENDING;
            break;
        }
        /* CAS failed, another thread changed the state; reload it */
        goto retry;

    case BLOCK_PENDING:
        /* Another thread is busy fetching this block, wait for it to finish */
        if (!s->timeout) {
            is_race = 1;
            break; /* no timeout requested, immediately race to fetch block */
        } else if (pending_since) {
            int64_t new = av_gettime_relative();
            if (new - pending_since >= s->timeout) {
                is_race = 1;
                break; /* timeout expired, try to fetch the block ourselves */
            }
        } else {
            pending_since = av_gettime_relative();
        }

        /* Make sure we try a few times before giving up */
        av_usleep(s->timeout >> 4);
        state = atomic_load_explicit(&block->state, memory_order_acquire);
        goto retry;
    }

    /* Cache miss, fetch this block from underlying protocol */
    s->nb_miss++;

    const int read_only = s->read_only || s->write_err || verify_read;
    int64_t inner_pos = read_only ? s->pos : block_pos;
    if (s->inner_pos != inner_pos) {
        inner_pos = ffurl_seek(s->inner, inner_pos, SEEK_SET);
        if (inner_pos < 0) {
            av_log(h, AV_LOG_ERROR, "Failed to seek underlying protocol: %s\n",
                   av_err2str(inner_pos));
            if (!read_only) {
                /* Release pending state to avoid stalling other threads. Don't
                 * mark this as failed, since the seek error may be unrelated to
                 * the block and should probably be tried again. */
                atomic_compare_exchange_strong_explicit(&block->state, &state,
                                                        BLOCK_NONE,
                                                        memory_order_relaxed,
                                                        memory_order_relaxed);
            }
            return inner_pos;
        }

        av_log(h, AV_LOG_DEBUG, "Inner seek to 0x%"PRIx64"\n", inner_pos);
        s->inner_pos = inner_pos;
    }

    if (read_only) {
        /* Directly defer to the underlying protocol */
        ret = ffurl_read(s->inner, buf, size);
        if (ret < 0)
            return ret;

        /* Verify the read data against the cached data if requested */
        if (verify_read && memcmp(buf, tmp, ret)) {
            av_log(h, AV_LOG_ERROR, "Cache verification failed for %d bytes "
                   "in block 0x%"PRIx64" at offset 0x%"PRIx64" + %"PRId64"!\n",
                   ret, block_id, block_pos, offset);
        }

        s->pos = s->inner_pos = inner_pos + ret;
        return ret;
    }

    int write_back = 1;
    if (s->cache_data && !is_race) {
        /* Read directly into memory mapped cache file */
        tmp = s->cache_data + block_pos;
        write_back = 0;
    } else if (size >= block_size && !offset) {
        /* Read directly into output buffer if aligned and large enough */
        tmp = buf;
    } else {
        /* Read into temporary buffer and copy later */
        tmp = s->tmp_buf;
    }

    /* Try and fetch the entire block */
    av_assert0(inner_pos == block_pos);
    int bytes_read = 0;
    while (bytes_read < block_size) {
        ret = ffurl_read(s->inner, &tmp[bytes_read], block_size - bytes_read);
        if (!ret || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            av_log(h, AV_LOG_ERROR, "Failed to read block 0x%"PRIx64": %s\n",
                   block_id, av_err2str(ret));
            int new_state = BLOCK_FAILED;
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EXIT)
                new_state = BLOCK_NONE; /* transient error, allow retries */

            /* Try to mark block as failed; ignore errors - any mismatch
             * here will mean that either another thread already marked it
             * as failed, or successfully cached it in the meantime */
            atomic_compare_exchange_strong_explicit(&block->state, &state,
                                                    new_state,
                                                    memory_order_relaxed,
                                                    memory_order_relaxed);
            return ret;
        }

        bytes_read += ret;
        s->inner_pos += ret;
    }

    if (bytes_read < block_size) {
        /* Learned location of true EOF, update filesize */
        ret = set_filesize(h, inner_pos + bytes_read);
        if (ret < 0)
            return ret;
    }

    if (bytes_read > 0) {
        ret = write_back ? write_cache(s, tmp, bytes_read, block_pos) : 0;
        if (ret < 0) {
            av_log(h, AV_LOG_ERROR, "Failed to write to cache file: %s\n",
                   av_err2str(ret));
            s->write_err = 1;
            /* Mark as NONE, not FAILED, since the block itself is fine -
             * just absent from the cache. */
            atomic_compare_exchange_strong_explicit(&block->state, &state,
                                                    BLOCK_NONE,
                                                    memory_order_relaxed,
                                                    memory_order_relaxed);
        } else {
            uint16_t crc = get_block_crc(tmp, bytes_read);
            av_log(h, AV_LOG_TRACE, "Cached %d bytes to block 0x%"PRIx64" at "
                   "offset 0x%"PRIx64", CRC 0x%04X\n", bytes_read, block_id,
                   block_pos, crc);
            atomic_store_explicit(&block->state, crc, memory_order_release);
        }
    } else {
        return AVERROR_EOF;
    }

    size = FFMIN(bytes_read - offset, size);
    if (size <= 0)
        return AVERROR_EOF;
    if (tmp != buf)
        memcpy(buf, &tmp[offset], size);
    s->pos += size;
    return size;
}

static int64_t shared_seek(URLContext *h, int64_t pos, int whence)
{
    SharedContext *s = h->priv_data;
    const int64_t filesize = get_filesize(h);
    int64_t res;

    switch (whence) {
    case AVSEEK_SIZE:
        if (filesize)
            return filesize;
        res = ffurl_seek(s->inner, pos, whence);
        if (res > 0) {
            if (set_filesize(h, res) < 0)
                return AVERROR(EINVAL);
        }
        return res;
    case SEEK_SET:
        break;
    case SEEK_CUR:
        pos += s->pos;
        break;
    case SEEK_END:
        if (filesize) {
            pos += filesize;
            break;
        }
        /* Defer to underlying protocol if filesize is unknown */
        res = ffurl_seek(s->inner, pos, whence);
        if (res < 0)
            return res;
        /* Opportunistically update known filesize */
        if (set_filesize(h, res - pos) < 0)
            return AVERROR(EINVAL);
        av_log(h, AV_LOG_DEBUG, "Inner seek to 0x%"PRIx64"\n", res);
        return s->pos = s->inner_pos = res;
    default:
        return AVERROR(EINVAL);
    }

    if (pos < 0)
        return AVERROR(EINVAL);

    av_log(h, AV_LOG_DEBUG, "Virtual seek to 0x%"PRIx64"\n", pos);
    return s->pos = pos;
}

static int shared_get_file_handle(URLContext *h)
{
    SharedContext *s = h->priv_data;
    return ffurl_get_file_handle(s->inner);
}

static int shared_get_short_seek(URLContext *h)
{
    SharedContext *s = h->priv_data;
    int ret = ffurl_get_short_seek(s->inner);
    return ret > 0 ? FFMAX(ret, s->block_size) : s->block_size;
}

#define OFFSET(x) offsetof(SharedContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "cache_dir",      "Directory path for shared file cache",             OFFSET(cache_dir),      AV_OPT_TYPE_STRING, {.str = NULL}, .flags = D },
    { "block_shift",    "Set the base 2 logarithm of the block size",       OFFSET(block_shift),    AV_OPT_TYPE_INT, {.i64 = 15}, 9, 30, .flags = D },
    { "read_only",      "Don't write data to the cache, only read from it", OFFSET(read_only),      AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, .flags = D },
    { "cache_verify",   "Verify correctness of the cache against the source",   OFFSET(verify),     AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, .flags = D },
    { "cache_timeout",  "Time in us to wait before re-fetching pending blocks", OFFSET(timeout),    AV_OPT_TYPE_INT64, {.i64 = 10000}, 0, INT64_MAX, .flags = D },
    { "retry_errors",   "Re-request blocks even if they previously failed", OFFSET(retry_errors),   AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, .flags = D },
    {0},
};

static const AVClass shared_context_class = {
    .class_name = "shared",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_shared_protocol = {
    .name                = "shared",
    .url_open2           = shared_open,
    .url_read            = shared_read,
    .url_seek            = shared_seek,
    .url_close           = shared_close,
    .url_get_file_handle = shared_get_file_handle,
    .url_get_short_seek  = shared_get_short_seek,
    .priv_data_size      = sizeof(SharedContext),
    .priv_data_class     = &shared_context_class,
};
