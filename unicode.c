#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "unicode.h"

#ifdef HEADER
#define UTF8_MAX_LENGTH 4
#define UNICODE_BAD_INPUT -1
#define UNICODE_SURROGATE_PAIR -2
#define UNICODE_NOT_SURROGATE_PAIR -3
#define UNICODE_BAD_UTF8 -4
#define UNICODE_EMPTY_INPUT -5
#define UNICODE_NON_SHORTEST -6
#define UNICODE_TOO_BIG -7
#define UNICODE_NOT_CHARACTER -8

/* For routines which don't need a return value. */
#define UNICODE_OK 0
#endif /* def HEADER */

/* Convert a UTF-8 encoded character in "input" into a number. This
   function returns the unicode value of the UTF-8 character if
   successful, and -1 if not successful. "end_ptr" is set to the next
   character after the read character on success. "end_ptr" is set to
   the start of input on failure. "end_ptr" may not be null. */

int utf8_to_ucs2 (const unsigned char * input, const unsigned char ** end_ptr)
{
    *end_ptr = input;
    unsigned char c;
    c = input[0];
    if (c == 0) {
        return UNICODE_EMPTY_INPUT;
    }
    if (c < 0x80) {
	/* One byte (ASCII) case. */
        * end_ptr = input + 1;
        return c;
    }
    if ((c & 0xF0) == 0xF0) {
	/* Four byte case. */
	unsigned char d, e, f;
	uint32_t v;
	d = input[1];
	e = input[2];
	f = input[3];

	/* https://metacpan.org/source/CHANSEN/Unicode-UTF8-0.60/UTF8.xs#L117 */
	v = ((c & 0x07) << 18)
	    | ((d & 0x3F) << 12)
	    | ((e & 0x3F) <<  6)
	    | ((f & 0x3F));
	if ((v & 0xF8C0C0C0) != 0xF0808080) {
	    return UNICODE_BAD_UTF8;
	}
	/* Non-shortest form */
	if (v < 0xF0908080) {
	    return UNICODE_NON_SHORTEST;
	}
	/* Greater than U+10FFFF */
	if (v > 0xF48FBFBF) {
	    return UNICODE_TOO_BIG;
	}
	/* Non-characters U+nFFFE..U+nFFFF on plane 1-16 */
	if ((v & 0x000FBFBE) == 0x000FBFBE) {
	    return UNICODE_NOT_CHARACTER;
	}
	return v;
    }
    if ((c & 0xE0) == 0xE0) {
	/* Three byte case. */
        if (input[1] < 0x80 || input[1] > 0xBF ||
	    input[2] < 0x80 || input[2] > 0xBF) {
            return UNICODE_BAD_UTF8;
	}
        * end_ptr = input + 3;
        return
            (c & 0x0F)<<12 |
            (input[1] & 0x3F)<<6  |
            (input[2] & 0x3F);
    }
    if ((c & 0xC0) == 0xC0) {
	/* Two byte case. */
        if (input[1] < 0x80 || input[1] > 0xBF) {
            return UNICODE_BAD_UTF8;
	}
        * end_ptr = input + 2;
        return
            (c & 0x1F)<<6  |
            (input[1] & 0x3F);
    }
    return UNICODE_BAD_INPUT;
}

/* Input: a Unicode code point, "ucs2". 

   Output: UTF-8 characters in buffer "utf8". 

   Return value: the number of bytes written into "utf8", or a
   negative number if there was an error.

   This adds a zero byte to the end of the string. It assumes that the
   buffer "utf8" has at least four bytes of space to write to. */

int ucs2_to_utf8 (int ucs2, unsigned char * utf8)
{
    if (ucs2 < 0x80) {
        utf8[0] = ucs2;
        utf8[1] = '\0';
        return 1;
    }
    if (ucs2 >= 0x80  && ucs2 < 0x800) {
        utf8[0] = (ucs2 >> 6)   | 0xC0;
        utf8[1] = (ucs2 & 0x3F) | 0x80;
        utf8[2] = '\0';
        return 2;
    }
    if (ucs2 >= 0x800 && ucs2 < 0xFFFF) {
	if (ucs2 >= 0xD800 && ucs2 <= 0xDFFF) {
	    /* Ill-formed. */
	    return UNICODE_SURROGATE_PAIR;
	}
        utf8[0] = ((ucs2 >> 12)       ) | 0xE0;
        utf8[1] = ((ucs2 >> 6 ) & 0x3F) | 0x80;
        utf8[2] = ((ucs2      ) & 0x3F) | 0x80;
        utf8[3] = '\0';
        return 3;
    }
    if (ucs2 >= 0x10000 && ucs2 < 0x10FFFF) {
	/* http://tidy.sourceforge.net/cgi-bin/lxr/source/src/utf8.c#L380 */
	utf8[0] = 0xF0 | (ucs2 >> 18);
	utf8[1] = 0x80 | ((ucs2 >> 12) & 0x3F);
	utf8[2] = 0x80 | ((ucs2 >> 6) & 0x3F);
	utf8[3] = 0x80 | ((ucs2 & 0x3F));
        utf8[4] = '\0';
        return 4;
    }
    return UNICODE_BAD_INPUT;
}

/* Convert surrogate pairs to UTF-8. */

int surrogate_to_utf8 (int hi, int lo, unsigned char * utf8)
{
    int X, W, U, C;
    if (hi < 0xD800 || hi > 0xDFFF) {
	/* Not surrogate pair. */
	return UNICODE_NOT_SURROGATE_PAIR;
    }
    if (lo < 0xD800 || lo > 0xDFFF) {
	/* Not surrogate pair. */
	return UNICODE_NOT_SURROGATE_PAIR;
    }
    /* http://www.unicode.org/faq/utf_bom.html#utf16-3 */
    X = ((hi & ((1 << 6) -1)) << 10) | (lo & ((1 << 10) -1));
    W = (hi >> 6) & ((1 << 5) - 1);
    U = W + 1;
    C = U << 16 | X;

    return ucs2_to_utf8 (C, utf8);
}

int
unicode_to_surrogates (unsigned unicode, unsigned * hi_ptr, unsigned * lo_ptr)
{
    unsigned hi = 0xD800;
    unsigned lo = 0xDC00;
    if (unicode < 0x10000) {
	/* Doesn't need to be a surrogate pair, let's recycle this
	   constant here. */
	return UNICODE_NOT_SURROGATE_PAIR;
    }
    unicode -= 0x10000;
    hi |= ((unicode >>10) & 0x3ff);
    lo |= ((unicode) & 0x3ff);
    * hi_ptr = hi;
    * lo_ptr = lo;
    return UNICODE_OK;
}

/* Given a count of Unicode characters "n_chars", return the number of
   bytes. A negative value indicates some kind of error. */

int unicode_chars_to_bytes (const unsigned char * utf8, int n_chars)
{
    int i;
    const unsigned char * p = utf8;
    int len = strlen ((const char *) utf8);
    if (len == 0 && n_chars != 0) {
	return UNICODE_EMPTY_INPUT;
    }
    for (i = 0; i < n_chars; i++) {
        int ucs2 = utf8_to_ucs2 (p, & p);
        if (ucs2 < 0) {
	    return ucs2;
        }
    }
    return p - utf8;
}

int unicode_count_chars (const unsigned char * utf8)
{
    int chars = 0;
    const unsigned char * p = utf8;
    int len = strlen ((const char *) utf8);
    if (len == 0) {
        return 0;
    }
    while (p - utf8 < len) {
        int ucs2;
        ucs2 = utf8_to_ucs2 (p, & p);
        if (ucs2 == -1) {
            return -1;
        }
        chars++;
        if (*p == '\0') {
            return chars;
        }
    }
    return UNICODE_BAD_INPUT;
}

/* From the Json3 project. */

#ifdef HEADER
#define VALID_UTF8 1
#define INVALID_UTF8 0
#endif /* def HEADER */

#define BYTE_80_8F \
      0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86:\
 case 0x87: case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D:\
 case 0x8E: case 0x8F
#define BYTE_80_9F \
      0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86:\
 case 0x87: case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D:\
 case 0x8E: case 0x8F: case 0x90: case 0x91: case 0x92: case 0x93: case 0x94:\
 case 0x95: case 0x96: case 0x97: case 0x98: case 0x99: case 0x9A: case 0x9B:\
 case 0x9C: case 0x9D: case 0x9E: case 0x9F
#define BYTE_80_BF \
      0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86:\
 case 0x87: case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D:\
 case 0x8E: case 0x8F: case 0x90: case 0x91: case 0x92: case 0x93: case 0x94:\
 case 0x95: case 0x96: case 0x97: case 0x98: case 0x99: case 0x9A: case 0x9B:\
 case 0x9C: case 0x9D: case 0x9E: case 0x9F: case 0xA0: case 0xA1: case 0xA2:\
 case 0xA3: case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xA8: case 0xA9:\
 case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF: case 0xB0:\
 case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:\
 case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE:\
 case 0xBF
#define BYTE_90_BF \
      0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96:\
 case 0x97: case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D:\
 case 0x9E: case 0x9F: case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4:\
 case 0xA5: case 0xA6: case 0xA7: case 0xA8: case 0xA9: case 0xAA: case 0xAB:\
 case 0xAC: case 0xAD: case 0xAE: case 0xAF: case 0xB0: case 0xB1: case 0xB2:\
 case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7: case 0xB8: case 0xB9:\
 case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF
#define BYTE_A0_BF \
      0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: case 0xA6:\
 case 0xA7: case 0xA8: case 0xA9: case 0xAA: case 0xAB: case 0xAC: case 0xAD:\
 case 0xAE: case 0xAF: case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4:\
 case 0xB5: case 0xB6: case 0xB7: case 0xB8: case 0xB9: case 0xBA: case 0xBB:\
 case 0xBC: case 0xBD: case 0xBE: case 0xBF
#define BYTE_C2_DF \
      0xC2: case 0xC3: case 0xC4: case 0xC5: case 0xC6: case 0xC7: case 0xC8:\
 case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF:\
 case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6:\
 case 0xD7: case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD:\
 case 0xDE: case 0xDF
#define BYTE_E1_EC \
      0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: case 0xE6: case 0xE7:\
 case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0xEC
#define BYTE_EE_EF \
      0xEE: case 0xEF
#define BYTE_F1_F3 \
      0xF1: case 0xF2: case 0xF3

#define UNICODEADDBYTE i++

#define UNICODEFAILUTF8(want) return INVALID_UTF8

#define UNICODENEXTBYTE c=input[i]

int
valid_utf8 (const unsigned char * input, int input_length)
{
    int i;
    unsigned char c;

    i = 0;

 string_start:

    i++;
    if (i >= input_length) {
	return VALID_UTF8;
    }
    /* Set c separately here since we use a range comparison before
       the switch statement. */
    c = input[i];

    /* Admit all bytes <= 0x80. */
    if (c <= 0x80) {
	goto string_start;
    }

    switch (c) {
    case BYTE_C2_DF:
	UNICODEADDBYTE;
	goto byte_last_80_bf;
	    
    case 0xE0:
	UNICODEADDBYTE;
	goto byte23_a0_bf;
	    
    case BYTE_E1_EC:
	UNICODEADDBYTE;
	goto byte_penultimate_80_bf;
	    
    case 0xED:
	UNICODEADDBYTE;
	goto byte23_80_9f;
	    
    case BYTE_EE_EF:
	UNICODEADDBYTE;
	goto byte_penultimate_80_bf;
	    
    case 0xF0:
	UNICODEADDBYTE;
	goto byte24_90_bf;
	    
    case BYTE_F1_F3:
	UNICODEADDBYTE;
	goto byte24_80_bf;
	    
    case 0xF4:
	UNICODEADDBYTE;
	goto byte24_80_8f;

    }

 byte_last_80_bf:

    switch (UNICODENEXTBYTE) {

    case BYTE_80_BF:
	UNICODEADDBYTE;
	goto string_start;
    default:
	UNICODEFAILUTF8 (XBYTES_80_BF);
    }

 byte_penultimate_80_bf:

    switch (UNICODENEXTBYTE) {

    case BYTE_80_BF:
	UNICODEADDBYTE;
	goto byte_last_80_bf;
    default:
	UNICODEFAILUTF8 (XBYTES_80_BF);
    }

 byte24_90_bf:

    switch (UNICODENEXTBYTE) {

    case BYTE_90_BF:
	UNICODEADDBYTE;
	goto byte_penultimate_80_bf;
    default:
	UNICODEFAILUTF8 (XBYTES_90_BF);
    }

 byte23_80_9f:

    switch (UNICODENEXTBYTE) {

    case BYTE_80_9F:
	UNICODEADDBYTE;
	goto byte_last_80_bf;
    default:
	UNICODEFAILUTF8 (XBYTES_80_9F);
    }

 byte23_a0_bf:

    switch (UNICODENEXTBYTE) {

    case BYTE_A0_BF:
	UNICODEADDBYTE;
	goto byte_last_80_bf;
    default:
	UNICODEFAILUTF8 (XBYTES_A0_BF);
    }

 byte24_80_bf:

    switch (UNICODENEXTBYTE) {

    case BYTE_80_BF:
	UNICODEADDBYTE;
	goto byte_penultimate_80_bf;
    default:
	UNICODEFAILUTF8 (XBYTES_80_BF);
    }

 byte24_80_8f:

    switch (UNICODENEXTBYTE) {

    case BYTE_80_8F:
	UNICODEADDBYTE;
	goto byte_penultimate_80_bf;
    default:
	UNICODEFAILUTF8 (XBYTES_80_8F);
    }
}


#ifdef TEST

void print_bytes (const unsigned char * bytes)
{
    int i;
    for (i = 0; i < strlen ((const char *) bytes); i++) {
        fprintf (stderr, "%02X", bytes[i]);
    }
    fprintf (stderr, "\n");
}

#define OK(test, counter, message, ...) {	\
	counter++;				\
	if (test) {				\
	    printf ("ok %d - ", counter);	\
	}					\
	else {					\
	    printf ("not ok %d - ", counter);	\
	}					\
	printf (message, ## __VA_ARGS__);	\
	printf (".\n");				\
    }


void test_ucs2_to_utf8 (const unsigned char * input, int * count)
{
    /* Buffer to print utf8 out into. */
    unsigned char buffer[0x100];
    /* Offset into buffer. */
    unsigned char * offset;
    const unsigned char * start = input;

    offset = buffer;
    while (1) {
        int unicode;
        int bytes;
        const unsigned char * end;
        unicode = utf8_to_ucs2 (start, & end);
        if (unicode == -1) {
            break;
	}
        bytes = ucs2_to_utf8 (unicode, offset);
	OK (bytes > 0, (*count), "no bad conversion");
	OK (strncmp ((const char *) offset,
		     (const char *) start, bytes) == 0, (*count),
	    "round trip OK for %X (%d bytes)", unicode, bytes);
        start = end;
        offset += bytes;
#if 0
        printf ("%X %d\n", unicode, bytes);
#endif
    }
    * offset = '\0';
    OK (strcmp ((const char *) buffer, (const char *) input) == 0,
	(*count),
	"input %s resulted in identical output %s",
	input, buffer);
}

static void
test_invalid_utf8 (int * count)
{
    unsigned char invalid_utf8[8];
    int unicode;
    const unsigned char * end;
    snprintf ((char *) invalid_utf8, 7, "%c%c%c", 0xe8, 0xe4, 0xe5);
    unicode = utf8_to_ucs2 (invalid_utf8, & end);
    OK (unicode == UNICODE_BAD_UTF8, (*count),
	"invalid UTF-8 gives incorrect result");
}

static void
test_surrogate_pairs (int * count)
{
    unsigned nogood = 0x3000;
    /* Test against examples from
       https://en.wikipedia.org/wiki/UTF-16#Examples. */
    unsigned wikipedia_1 = 0x10437;
    unsigned wikipedia_2 = 0x24b62;
    unsigned json_spec = 0x1D11E;
    int status;
    unsigned hi;
    unsigned lo;

    status = unicode_to_surrogates (nogood, & hi, & lo);

    OK (status == UNICODE_NOT_SURROGATE_PAIR, (*count),
	"low value to surrogate pair breaker returns error");

    status = unicode_to_surrogates (wikipedia_1, & hi, & lo);
    OK (status == UNICODE_OK, (*count), "Ok with %X", wikipedia_1);
    OK (hi == 0xD801, (*count), "Got expected %X == 0xD801", hi);
    OK (lo == 0xDC37, (*count), "Got expected %X == 0xDC37", lo);

    status = unicode_to_surrogates (wikipedia_2, & hi, & lo);
    OK (status == UNICODE_OK, (*count), "Ok with %X", wikipedia_1);
    OK (hi == 0xD852, (*count), "Got expected %X == 0xD852", hi);
    OK (lo == 0xDF62, (*count), "Got expected %X == 0xDF62", lo);

    status = unicode_to_surrogates (json_spec, & hi, & lo);
    OK (status == UNICODE_OK, (*count), "Ok with %X", json_spec);
    OK (hi == 0xD834, (*count), "Got expected %X == 0xD834", hi);
    OK (lo == 0xDd1e, (*count), "Got expected %X == 0xDD1e", lo);
}

int main ()
{
    /* Test counter for TAP. */
    int count;
    count = 0;
    const unsigned char * utf8 = (unsigned char *) "漢数字ÔÕÖＸ";
    const unsigned char * start = utf8;
    while (*start) {
        int unicode;
        const unsigned char * end;
        unicode = utf8_to_ucs2 (start, & end);
	OK (unicode > 0, count, "no bad value at %s", start);
        printf ("# %s is %04X, length is %d\n", start, unicode, end - start);
        start = end;
    }
    test_ucs2_to_utf8 (utf8, & count);
    int cc = unicode_count_chars (utf8);
    OK (cc == 7, count, "get seven characters for utf8");
    test_invalid_utf8 (& count);
    test_surrogate_pairs (& count);
    printf ("1..%d\n", count);
}

#endif /* def TEST */
