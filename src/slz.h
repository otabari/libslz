#ifndef _SLZ_H
#define _SLZ_H

#include <stdint.h>

/* We have two macros UNALIGNED_LE_OK and UNALIGNED_FASTER. The latter indicates
 * that using unaligned data is faster than a simple shift. On x86 32-bit at
 * least it is not the case as the per-byte access is 30% faster. A core2-duo on
 * x86_64 is 7% faster to read one byte + shifting by 8 than to read one word,
 * but a core i5 is 7% faster doing the unaligned read, so we privilege more
 * recent implementations here.
 */
#if defined(__x86_64__)
#define UNALIGNED_LE_OK
#define UNALIGNED_FASTER
#elif defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__) || (defined(__ARMEL__) && defined(__ARM_ARCH_7A__))
#define UNALIGNED_LE_OK
//#define UNALIGNED_FASTER
#endif

/* Log2 of the size of the hash table used for the references table. */
#define HASH_BITS 13

enum slz_state {
	SLZ_ST_INIT,  /* stream initialized */
	SLZ_ST_EOB,   /* header or end of block already sent */
	SLZ_ST_FIXED, /* inside a fixed huffman sequence */
	SLZ_ST_LAST,  /* last block, BFINAL sent */
	SLZ_ST_DONE,  /* BFINAL+EOB sent BFINAL */
	SLZ_ST_END    /* end sent (BFINAL, EOB, CRC + len) */
};

enum {
	SLZ_FMT_GZIP,    /* RFC1952: gzip envelope and crc32 for CRC */
	SLZ_FMT_ZLIB,    /* RFC1950: zlib envelope and adler-32 for CRC */
	SLZ_FMT_DEFLATE, /* RFC1951: raw deflate, and no crc */
};

struct slz_stream {
	uint32_t queue; /* last pending bits, LSB first */
	uint32_t qbits; /* number of bits in queue, < 8 */
	unsigned char *outbuf; /* set by encode() */
	uint16_t state; /* one of slz_state */
	uint8_t level:1; /* 0 = no compression, 1 = compression */
	uint8_t format:2; /* SLZ_FMT_* */
	uint8_t unused1; /* unused for now */
	uint32_t crc32;
	uint32_t ilen;
};

/* Functions specific to rfc1951 (deflate) */
void slz_prepare_dist_table();
long slz_rfc1951_encode(struct slz_stream *strm, unsigned char *out, const unsigned char *in, long ilen, int more);
int slz_rfc1951_init(struct slz_stream *strm);
int slz_rfc1951_finish(struct slz_stream *strm, unsigned char *buf);

/* Functions specific to rfc1952 (gzip) */
void slz_make_crc_table(void);
uint32_t slz_crc32_by1(uint32_t crc, const unsigned char *buf, int len);
uint32_t slz_crc32_by4(uint32_t crc, const unsigned char *buf, int len);
long slz_rfc1952_encode(struct slz_stream *strm, unsigned char *out, const unsigned char *in, long ilen, int more);
int slz_rfc1952_send_header(struct slz_stream *strm, unsigned char *buf);
int slz_rfc1952_init(struct slz_stream *strm);
int slz_rfc1952_finish(struct slz_stream *strm, unsigned char *buf);

/* Functions specific to rfc1950 (zlib) */
uint32_t slz_adler32_by1(uint32_t crc, const unsigned char *buf, int len);
uint32_t slz_adler32_block(uint32_t crc, const unsigned char *buf, long len);
long slz_rfc1950_encode(struct slz_stream *strm, unsigned char *out, const unsigned char *in, long ilen, int more);
int slz_rfc1950_send_header(struct slz_stream *strm, unsigned char *buf);
int slz_rfc1950_init(struct slz_stream *strm);
int slz_rfc1950_finish(struct slz_stream *strm, unsigned char *buf);

/* generic functions */

/* Initializes stream <strm>. It will configure the stream to use format
 * <format> for the data, which must be one of SLZ_FMT_*. The compression level
 * passed in <level> is set. This value can only be 0 (no compression) or 1
 * (compression) and other values will lead to unpredictable behaviour. The
 * function should always return 0.
 */
static inline int slz_init(struct slz_stream *strm, int level, int format)
{
	int ret;

	if (format == SLZ_FMT_GZIP)
		ret = slz_rfc1952_init(strm);
	else if (format == SLZ_FMT_ZLIB)
		ret = slz_rfc1950_init(strm);
	else { /* deflate for anything else */
		ret = slz_rfc1951_init(strm);
		strm->format = format;
	}
	strm->level = level;
	return ret;
}

/* Encodes the block according to the format used by the stream. This means
 * that the CRC of the input block may be computed according to the CRC32 or
 * adler-32 algorithms. The number of output bytes is returned.
 */
static inline long slz_encode(struct slz_stream *strm, unsigned char *out,
                              const unsigned char *in, long ilen, int more)
{
	long ret;

	if (strm->format == SLZ_FMT_GZIP)
		ret = slz_rfc1952_encode(strm, out, in, ilen, more);
	else if (strm->format == SLZ_FMT_ZLIB)
		ret = slz_rfc1950_encode(strm, out, in, ilen, more);
	else /* deflate for other ones */
		ret = slz_rfc1951_encode(strm, out, in, ilen, more);

	return ret;
}

/* Flushes pending bits and sends the trailer for stream <strm> into buffer
 * <buf> if needed. When it's done, the stream state is updated to SLZ_ST_END.
 * It returns the number of bytes emitted. The trailer consists in flushing the
 * possibly pending bits from the queue (up to 24 bits), rounding to the next
 * byte, then 4 bytes for the CRC when doing zlib/gzip, then another 4 bytes
 * for the input length for gzip. That may abount to 4+4+4 = 12 bytes, that the
 * caller must ensure are available before calling the function.
 */
static inline int slz_finish(struct slz_stream *strm, unsigned char *buf)
{
	int ret;

	if (strm->format == SLZ_FMT_GZIP)
		ret = slz_rfc1952_finish(strm, buf);
	else if (strm->format == SLZ_FMT_ZLIB)
		ret = slz_rfc1950_finish(strm, buf);
	else /* deflate for other ones */
		ret = slz_rfc1951_finish(strm, buf);

	return ret;
}

#endif
