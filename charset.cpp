// charset.cpp - part of TweakPNG
/*
    Copyright (C) 2008 Jason Summers

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    See the file tweakpng-src.txt for more information.
*/

// Note: I am aware of the WideCharToMultiByte and MultiByteToWideChar Windows
// API functions, but they don't work the same on all versions of Windows,
// and anyway I was interested in trying to write my own functions to do this.

#include "twpng-config.h"

#include <windows.h>
#include <tchar.h>
#include <malloc.h>

#include "tweakpng.h"

#ifdef UNICODE
int convert_tchar_to_latin1(const TCHAR *src, int srclen,
								   char **pdst, int *pdstlen)
{
	UINT16 c;
	int spos,dpos;
	unsigned char *dst;

	*pdst=NULL;
	*pdstlen=0;

	dst = (unsigned char*)malloc((size_t)srclen+1); // This memory won't all be used if there are surrogate pairs.
	if(!dst) return 0;
	dpos=0;
	for(spos=0;spos<srclen;spos++) {
		c=(UINT16)src[spos];
		if( (c & 0xfc00)==0xd800 ) {
			// first word of a surrogate pair
		}
		else if( (c & 0xfc00)==0xdc00 ) {
			// second word of a surrogate pair
			dst[dpos++] = '?';
		}
		else if(c>0xff) {
			// character not representable in latin-1
			dst[dpos++] = '?';
		}
		else {
			dst[dpos++] = (unsigned char)c;
		}
	}
	dst[dpos] = '\0';

	*pdst = (char*)dst;
	*pdstlen = dpos;
	return 1;
}
#else
int convert_tchar_to_latin1(const TCHAR *src, int srclen,
								   char **pdst, int *pdstlen)
{
	char *dst;
	*pdst=NULL;
	*pdstlen=0;
	dst = (char*)malloc(srclen+1);
	if(!dst) return 0;
	memcpy(dst,src,srclen);
	dst[srclen]='\0';
	*pdst=dst;
	*pdstlen=srclen;
	return 1;
}
#endif

int convert_latin1_to_tchar(const char *src, int srclen,
								   TCHAR **pdst, int *pdstlen)
{
	TCHAR *dst;
	int i;
	*pdst=NULL;
	*pdstlen=0;
	dst=(TCHAR*)malloc(sizeof(TCHAR)*((size_t)srclen+1));
	if(!dst) return 0;
	for(i=0;i<srclen;i++) {
		dst[i] = ((unsigned char)src[i]);
	}
	dst[srclen]='\0';
	*pdst=dst;
	*pdstlen=srclen;
	return 1;
}

#ifdef UNICODE

// Returns the number of bytes that the given utf8 string would
// require if converted to utf16. srclen is in bytes.
static int utf8_to_utf16_count_bytes(const void *src, int srclen)
{
	int count=0;
	int i;
	unsigned char c;

	for(i=0;i<srclen;i++) {
		c= ((const unsigned char*)src)[i];
		if(c<=0x7f) {
			// 1-byte utf8 character (=2 bytes in utf16)
			count+=2;
		}
		else if(c>=0x80 && c<=0xbf) {
			// non-initial byte of a multi-byte utf8 character
			;
		}
		else if(c>=0xc0 && c<=0xef) {
			// 1st byte of a 2-byte or 3-byte utf8 character (=2 bytes in utf16)
			count+=2;
		}
		else if(c>=0xf0 && c<=0xf7) {
			// 1st byte of a 4-byte utf8 character (=4 bytes in utf16)
			count+=4;
		}
		else {
			// invalid byte
			;
		}
	}
	return count;
}

int convert_utf8_to_utf16(const void *src, int srclen,
								 WCHAR **pdst, int *pdstlen)
{
	WCHAR *dst;
	int pending_char;
	int utf8_more_bytes_expected = 0;
	unsigned char c;
	int dstpos;
	int i;
	int memneeded;

	memneeded = utf8_to_utf16_count_bytes(src,srclen);
	dst = (WCHAR*)malloc((size_t)memneeded+10);
	if(!dst) return 0;
	dstpos=0;

	pending_char=0;
	for(i=0;i<srclen;i++) {
		c= ((const unsigned char*)src)[i];
		if(c<=0x7f) {
			// 1-byte utf8 character
			pending_char=0;
			utf8_more_bytes_expected = 0;
			dst[dstpos++] = c;
		}
		else if((c>=0x80 && c<=0xbf) && utf8_more_bytes_expected>0) {
			// non-initial byte of a multi-byte utf8 character
			pending_char = (pending_char<<6)|(c&0x3f);
			utf8_more_bytes_expected--;
			if(utf8_more_bytes_expected==0) {
				if(pending_char>=0xd800 && pending_char<=0xdfff) {
					// unrepresentable character in the surrogate-pair range
					dst[dstpos++] = 0xfffd;
				}
				else if(pending_char<=0xffff) {
					// normal single-word character
					dst[dstpos++] = pending_char;
				}
				else {
					// Character representable using a surrogate pair
					// TODO: make sure this is correct.
					dst[dstpos++] = 0xd800 | ((pending_char-0x10000)>>10);
					dst[dstpos++] = 0xdc00 | ((pending_char-0x10000)&0x03ff);
				}
			}

		}
		else if(c>=0xc0 && c<=0xdf) {
			// 1st byte of a 2-byte utf8 character
			pending_char = c&0x1f;
			utf8_more_bytes_expected = 1;
		}
		else if(c>=0xe0 && c<=0xef) {
			// 1st byte of a 3-byte utf8 character
			pending_char = c&0x0f;
			utf8_more_bytes_expected = 2;
		}
		else if(c>=0xf0 && c<=0xf7) {
			// 1st byte of a 4-byte utf8 character
			pending_char = c&0x07;
			utf8_more_bytes_expected = 3;
		}
		else {
			// invalid byte
			pending_char=0;
			utf8_more_bytes_expected = 0;
		}
	}

	dst[dstpos]= '\0';

	*pdst = dst;
	*pdstlen = dstpos;
	return 1;
}
#endif


#ifdef UNICODE

// Returns the number of bytes that the given utf16 string would
// require if converted to utf8. srclen is in WCHARs.
static int utf16_to_utf8_count_bytes(const WCHAR *src, int srclen)
{
	int i, c;
	int count = 0;

	for(i=0;i<srclen;i++) {
		c = (int)src[i];
		if(c<=0x7f) { // 1-byte utf8 character
			count+=1;
		}
		else if(c<=0x07ff) { // 2-byte utf8 character
			count+=2;
		}
		else if(c>=0xd800 && c<=0xdbff) {
			// first word of a surrogate pair
			;
		}
		else if(c>=0xdc00 && c<=0xdfff) {
			// second word of a surrogate pair
			// the surrogate pair as a whole => 4-byte utf8 character
			count+=4;
		}
		else { // 3-byte utf8 character
			count+=3;
		}
	}
	return count;
}

int convert_utf16_to_utf8(const WCHAR *src, int srclen,
								 char **pdst, int *pdstlen)
{
	unsigned char *dst;
	int dpos;
	int i;
	int c;
	int pending_char = 0;
	int codept;
	int memneeded;

	*pdst = NULL;
	*pdstlen=0;

	memneeded = utf16_to_utf8_count_bytes(src,srclen);
	dst = (unsigned char*)malloc((size_t)memneeded+10);
	
	if(!dst) return 0;

	dpos=0;
	for(i=0;i<srclen;i++) {
		c = (int)src[i];
		if(c<=0x7f) { // 1-byte utf8 character
			dst[dpos++] = (unsigned char)c;
		}
		else if(c<=0x07ff) { // 2-byte utf8 character
			dst[dpos++] = 0xc0 | (c>>6);
			dst[dpos++] = 0x80 | (c&0x3f);
		}
		else if(c>=0xd800 && c<=0xdbff) {
			// first word of a surrogate pair
			pending_char = c;
		}
		else if(c>=0xdc00 && c<=0xdfff) {
			// second word of a surrogate pair  => 4-byte utf8 character
			// TODO: make sure this is correct.
			codept = (pending_char & 0x03ff) << 10;
			codept |= (c & 0x03ff);
			codept += 0x10000;
			dst[dpos++] = 0xf0 | (codept>>18);
			dst[dpos++] = 0x80 | ((codept>>12)&0x3f);
			dst[dpos++] = 0x80 | ((codept>>6)&0x3f);
			dst[dpos++] = 0x80 | (codept&0x3f);
		}
		else { // 3-byte utf8 character
			dst[dpos++] = 0xe0 | (c>>12);
			dst[dpos++] = 0x80 | ((c>>6)&0x3f);
			dst[dpos++] = 0x80 | (c&0x3f);
		}
	}
	dst[dpos]='\0';
	*pdstlen = dpos;
	*pdst = (char*)dst;
	return 1;
}
#endif
