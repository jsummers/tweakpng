// iccprof.cpp
//
//
/*
    Copyright (C) 2012 Jason Summers

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

#include "twpng-config.h"

#include <windows.h>
#include <tchar.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "resource.h"
#include "tweakpng.h"
#include <strsafe.h>

extern struct globals_struct globals;

struct iccp_ctx_struct {
	unsigned char *cmpr_data;
	int cmpr_data_len;

	unsigned char *data; // Uncompressed profile data
	int data_len;

	HWND hwnd;
};

static void twpng_iccp_append_text(struct iccp_ctx_struct *ctx, const TCHAR *s)
{
	SendDlgItemMessage(ctx->hwnd,IDC_EDIT2,EM_REPLACESEL,(WPARAM)FALSE,
		(LPARAM)s);
}

static void twpng_iccp_append_textf(struct iccp_ctx_struct *ctx, const TCHAR *fmt, ...)
{
	va_list ap;
	TCHAR buf[2048];

	va_start(ap, fmt);
	StringCbVPrintf(buf,sizeof(buf),fmt,ap);
	va_end(ap);

	twpng_iccp_append_text(ctx,buf);
}

static void twpng_iccp_bytes_to_string(struct iccp_ctx_struct *ctx,
	unsigned char *data, int data_len, TCHAR *buf, int buf_len)
{
	int i;
	int buf_pos = 0;

	for(i=0;i<data_len;i++) {
		if(buf_pos > buf_len-10) break;
		if(data[i]>=32 && data[i]<=126) {
			buf[buf_pos++] = (TCHAR)data[i];
		}
		else if(data[i]!=0) {
			// TODO: Support unprintable characters.
			buf[buf_pos++] = '_';
		}
	}

	buf[buf_pos] = '\0';
}

static void twpng_iccp_bytes_to_hex(struct iccp_ctx_struct *ctx,
	unsigned char *data, int data_len, TCHAR *buf, int buf_len)
{
	int i;
	int buf_pos = 0;
	static const TCHAR hexchar[16] = {'0','1','2','3','4','5','6',
		'7','8','9','a','b','c','d','e','f' };

	buf[0]='\0';
	if(buf_len < data_len*2+1) return;

	for(i=0;i<data_len;i++) {
		buf[buf_pos++] = hexchar[data[i]/16];
		buf[buf_pos++] = hexchar[data[i]%16];
	}
	buf[buf_pos] = '\0';
}

static void twpng_iccp_bytes_to_datetime(struct iccp_ctx_struct *ctx,
	unsigned char *data, TCHAR *buf, int buf_len)
{
	int year, month, day, hour, minute, second;
	year = read_int16(&data[0]);
	month = read_int16(&data[2]);
	day = read_int16(&data[4]);
	hour = read_int16(&data[6]);
	minute = read_int16(&data[8]);
	second = read_int16(&data[10]);
	StringCchPrintf(buf,buf_len,_T("%d-%02d-%02d %02d:%02d:%02d"),
		year,month,day,hour,minute,second);
}

// Returns an extra pointer to buf.
static TCHAR *get_rendering_intent_descr(unsigned int u, TCHAR *buf, int buf_len)
{
	switch(u) {
	case 0:
		StringCchCopy(buf,buf_len,_T("Perceptual"));
		break;
	case 1:
		StringCchCopy(buf,buf_len,_T("Media-Relative Colorimetric"));
		break;
	case 2:
		StringCchCopy(buf,buf_len,_T("Saturation"));
		break;
	case 3:
		StringCchCopy(buf,buf_len,_T("ICC-Absolute Colorimetric"));
		break;
	default:
		StringCchCopy(buf,buf_len,_T("Unknown"));
	}
	return buf;
}

static void twpng_dump_iccp_header(struct iccp_ctx_struct *ctx)
{
	DWORD u;
	TCHAR buf[100];

	if(ctx->data_len<128) return;

	twpng_iccp_append_text(ctx,SYM_HORZBAR _T(" Header ")
		SYM_HORZBAR _T("\r\n"));

	u = read_int32(&ctx->data[0]);
	twpng_iccp_append_textf(ctx,_T("Profile size: %u\r\n"),u);

	twpng_iccp_bytes_to_string(ctx,&ctx->data[4],4,buf,100);
	twpng_iccp_append_textf(ctx,_T("Preferred CMM type: ") SYM_LDQUO
		_T("%s") SYM_RDQUO _T("\r\n"),buf);

	u = read_int32(&ctx->data[8]);
	twpng_iccp_append_textf(ctx,_T("Profile version: %u.%u.%u\r\n"),
		(u&0xff000000)>>24,
		(u&0x00f00000)>>20,
		(u&0x000f0000)>>16);

	twpng_iccp_bytes_to_string(ctx,&ctx->data[12],4,buf,100);
	twpng_iccp_append_textf(ctx,_T("Profile/device class: ") SYM_LDQUO
		_T("%s") SYM_RDQUO _T("\r\n"),buf);

	twpng_iccp_bytes_to_string(ctx,&ctx->data[16],4,buf,100);
	twpng_iccp_append_textf(ctx,_T("Data color space: ") SYM_LDQUO
		_T("%s") SYM_RDQUO _T("\r\n"),buf);

	twpng_iccp_bytes_to_string(ctx,&ctx->data[20],4,buf,100);
	twpng_iccp_append_textf(ctx,_T("Profile connection space: ") SYM_LDQUO
		_T("%s") SYM_RDQUO _T("\r\n"),buf);

	// Bytes 24-35: date/time
	twpng_iccp_bytes_to_datetime(ctx,&ctx->data[24],buf,100);
	twpng_iccp_append_textf(ctx,_T("Date and time: %s\r\n"),buf);

	twpng_iccp_bytes_to_string(ctx,&ctx->data[36],4,buf,100);
	twpng_iccp_append_textf(ctx,_T("Profile file signature: ") SYM_LDQUO
		_T("%s") SYM_RDQUO _T("\r\n"),buf);

	twpng_iccp_bytes_to_string(ctx,&ctx->data[40],4,buf,100);
	twpng_iccp_append_textf(ctx,_T("Primary platform: ") SYM_LDQUO
		_T("%s") SYM_RDQUO _T("\r\n"),buf);

	u = read_int32(&ctx->data[44]);
	twpng_iccp_append_textf(ctx,_T("Embedded profile flag: %u\r\n"),u&0x1);
	twpng_iccp_append_textf(ctx,_T("Dependent profile flag: %u\r\n"),(u&0x2)>>1);

	twpng_iccp_bytes_to_string(ctx,&ctx->data[48],4,buf,100);
	twpng_iccp_append_textf(ctx,_T("Device manufacturer: ") SYM_LDQUO
		_T("%s") SYM_RDQUO _T("\r\n"),buf);

	twpng_iccp_bytes_to_string(ctx,&ctx->data[52],4,buf,100);
	twpng_iccp_append_textf(ctx,_T("Device model: ") SYM_LDQUO
		_T("%s") SYM_RDQUO _T("\r\n"),buf);

	// TODO: Bytes 56-63: Device attributes

	u = read_int32(&ctx->data[64]);
	u = u & 0xffff;
	twpng_iccp_append_textf(ctx,_T("Rendering intent: %u (%s)\r\n"),u,
		get_rendering_intent_descr(u,buf,100));

	// TODO: Bytes 68-79: illuminant

	twpng_iccp_bytes_to_string(ctx,&ctx->data[80],4,buf,100);
	twpng_iccp_append_textf(ctx,_T("Profile creator: ") SYM_LDQUO
		_T("%s") SYM_RDQUO _T("\r\n"),buf);

	// Bytes 84-99: Profile ID
	twpng_iccp_bytes_to_hex(ctx,&ctx->data[84],16,buf,100);
	twpng_iccp_append_textf(ctx,_T("Profile ID: %s\r\n"),buf);

	// Bytes 100-127: Reserved
}

static void twpng_dump_iccp_textType(struct iccp_ctx_struct *ctx, unsigned char *d,
	unsigned int t_size)
{
	TCHAR buf[500];
	twpng_iccp_bytes_to_string(ctx,d,t_size,buf,500);
	twpng_iccp_append_textf(ctx,_T(" ") SYM_LDQUO _T("%s") SYM_RDQUO _T("\r\n"),buf);
}

static void twpng_dump_iccp_textDescriptionType(struct iccp_ctx_struct *ctx,
	unsigned char *d, unsigned int t_size)
{
	TCHAR buf[500];
	unsigned int ascii_count;

	ascii_count = read_int32(&d[0]);
	if(ascii_count+4>t_size) return;
	if(ascii_count>0) {
		twpng_iccp_bytes_to_string(ctx,&d[4],ascii_count-1,buf,500);
		twpng_iccp_append_textf(ctx,_T(" ") SYM_LDQUO _T("%s") SYM_RDQUO _T("\r\n"),buf);
	}
	// TODO?: Display the Unicode and Scriptcode versions of the string,
	// if present.
}

static void twpng_dump_iccp_tag(struct iccp_ctx_struct *ctx, unsigned int t_offset,
	unsigned int t_size)
{
	unsigned int type_code;

	if(t_size  > 100000000) return;
	if(t_size<8) return;
	if(t_offset>1000000000) return;
	if(t_offset+t_size>(unsigned int)ctx->data_len) return;

	type_code = read_int32(&ctx->data[t_offset]);

	switch(type_code) {
	case 0x74657874: // 'text'
		twpng_dump_iccp_textType(ctx,&ctx->data[t_offset+8],t_size-8);
		break;
	case 0x64657363: // 'desc'
		twpng_dump_iccp_textDescriptionType(ctx,&ctx->data[t_offset+8],t_size-8);
		break;
	}
}

static void twpng_dump_iccp_tags(struct iccp_ctx_struct *ctx)
{
	int i;
	unsigned int ntags;
	unsigned int t_offset, t_size;
	unsigned char *tagdata; // pointer to ctx->data[128]
	TCHAR buf[100];
	TCHAR buf2[100];

	if(ctx->data_len<132) return;
	tagdata = &ctx->data[128];

	twpng_iccp_append_text(ctx,_T("\r\n") SYM_HORZBAR _T(" Tags ")
		SYM_HORZBAR _T("\r\n"));

	ntags = read_int32(&tagdata[0]);
	twpng_iccp_append_textf(ctx,_T("Number of tags: %u\r\n"),ntags);

	if(ntags>10000) return;
	if((unsigned int)ctx->data_len<132+12*ntags) return;

	for(i=0;i<(int)ntags;i++) {
		twpng_iccp_bytes_to_string(ctx,&tagdata[4+12*i],4,buf,100);
		t_offset = read_int32(&tagdata[4+12*i+4]);
		t_size   = read_int32(&tagdata[4+12*i+8]);

		if(t_offset+4<=(unsigned int)ctx->data_len) {
			twpng_iccp_bytes_to_string(ctx,&ctx->data[t_offset],4,buf2,100);
		}
		else {
			buf2[0] = '\0';
		}

		twpng_iccp_append_textf(ctx,_T("Tag #%u signature=") SYM_LDQUO
			_T("%s") SYM_RDQUO _T(" offset=%u size=%u type=") SYM_LDQUO
			_T("%s") SYM_RDQUO _T("\r\n"),
			i+1,buf,t_offset,t_size,buf2);
		twpng_dump_iccp_tag(ctx,t_offset,t_size);
	}
}

static void twpng_dump_iccp(struct iccp_ctx_struct *ctx)
{
#ifdef TWPNG_HAVE_ZLIB
	twpng_iccp_append_textf(ctx,_T("Compressed size: %d\r\n"),ctx->cmpr_data_len);

	ctx->data_len = twpng_uncompress_data(&ctx->data, ctx->cmpr_data, ctx->cmpr_data_len);

	twpng_iccp_append_textf(ctx,_T("Uncompressed size: %d\r\n\r\n"),ctx->data_len);

	twpng_dump_iccp_header(ctx);

	twpng_dump_iccp_tags(ctx);

	if(ctx->data) {
		free(ctx->data);
		ctx->data = NULL;
	}
#endif
}

void Chunk::init_iCCP_dlg(HWND hwnd)
{
	struct keyword_info_struct kw;
	struct iccp_ctx_struct ctx;

	ZeroMemory(&ctx,sizeof(struct iccp_ctx_struct));
	get_keyword_info(&kw);

	SendDlgItemMessage(hwnd,IDC_EDIT1,EM_LIMITTEXT,79,0);
	SetDlgItemText(hwnd,IDC_EDIT1,kw.keyword);

	if((int)length < kw.keyword_len+2) return;

	if(data[kw.keyword_len+1] != 0) {
		twpng_iccp_append_text(&ctx,_T("Unsupported compression method\r\n"));
		return;
	}

	ctx.hwnd = hwnd;
	// +2: 1 for the NUL terminator, and 1 for the compression method.
	ctx.cmpr_data = &data[kw.keyword_len+2];
	ctx.cmpr_data_len = length - kw.keyword_len - 2;

#ifdef TWPNG_HAVE_ZLIB
	twpng_dump_iccp(&ctx);
#else
	twpng_iccp_append_text(&ctx,_T("Zlib support required\r\n"));
#endif

}

void Chunk::process_iCCP_dlg(HWND hwnd)
{
	struct keyword_info_struct kw;
	DWORD length_excluding_name;
	DWORD new_chunk_len;
	int ret;
	char *new_name_latin1 = NULL;
	int new_name_latin1_len;
	unsigned char *new_data = NULL;
	TCHAR keyword[80];

	// Save changes to profile name.

	// Get the length of the old name, so we know where the data after it begins.
	ret = get_keyword_info(&kw);
	if(!ret) goto done;

	// Read the new name from the Edit control.
	GetDlgItemText(hwnd,IDC_EDIT1,keyword,80);
	convert_tchar_to_latin1(keyword, lstrlen(keyword),
		&new_name_latin1,&new_name_latin1_len);

	length_excluding_name = length - kw.keyword_len; // includes 1 for NUL separator
	new_chunk_len = new_name_latin1_len + length_excluding_name;

	new_data = (unsigned char*)malloc(new_chunk_len);
	if(!new_data) goto done;

	// Copy the name into the new ICCP data block.
	memcpy(&new_data[0],new_name_latin1,new_name_latin1_len);
	// NUL separator and remaining data
	memcpy(&new_data[new_name_latin1_len],&data[kw.keyword_len],length_excluding_name);
	free(data);
	data = new_data;
	length = new_chunk_len;
	new_data = NULL;

done:
	if(new_data) free(new_data);
	if(new_name_latin1) free(new_name_latin1);
}
