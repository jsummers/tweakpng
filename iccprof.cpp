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


int ImportICCProfileByFilename(Png *png, const TCHAR *fn)
{
#ifdef TWPNG_HAVE_ZLIB
	HANDLE fh;
	Chunk *c = NULL;
	int retval = 0;
	DWORD unc_prof_len, cmpr_prof_len;
	unsigned char *unc_prof_data = NULL;
	unsigned char *cmpr_prof_data = NULL;
	int pos;
	DWORD bytesread = 0;
	const char *prof_name = "ICC profile";
	int prof_name_len = 11;
	int ret;

	if(!png) goto done;

	fh=CreateFile(fn,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,NULL);
	if(fh==INVALID_HANDLE_VALUE) {
		mesg(MSG_E,_T("Can") SYM_RSQUO _T("t open %s"),fn);
		goto done;
	}

	unc_prof_len=GetFileSize(fh,NULL);
	if(unc_prof_len<128 || unc_prof_len>100000000) goto done;
	unc_prof_data = (unsigned char*)malloc(unc_prof_len);
	if(!unc_prof_data) goto done;

	ret = ReadFile(fh,(LPVOID)unc_prof_data,unc_prof_len,&bytesread,NULL);
	CloseHandle(fh);
	if(!ret || bytesread!=unc_prof_len) {
		goto done;
	}

	cmpr_prof_len = twpng_compress_data(&cmpr_prof_data, unc_prof_data, unc_prof_len);
	if(cmpr_prof_len==0 || !cmpr_prof_data) goto done;

	c=new Chunk;
	memcpy(c->m_chunktype_ascii,"iCCP",5);
	c->set_chunktype_tchar_from_ascii();

	c->length = prof_name_len + 1 + 1 + cmpr_prof_len;
	c->data=(unsigned char*)malloc(c->length);
	if(!c->data) goto done;

	memcpy(&c->data[0],prof_name,(size_t)prof_name_len+1); // Profile name
	c->data[prof_name_len+1] = 0; // Compression method
	memcpy(&c->data[prof_name_len+1+1],cmpr_prof_data,cmpr_prof_len);

	c->m_parentpng=png;
	c->after_init();
	c->chunkmodified();

	// Make the new iCCP chunk the second chunk in the file if possible.
	pos = (png->m_num_chunks>0) ? 1 : 0;
	png->insert_chunks(pos,1,0);
	png->chunk[pos]=c;
	c = NULL;

	png->fill_listbox(globals.hwndMainList);
	png->modified();
	twpng_SetLVSelection(globals.hwndMainList,pos,1);

	retval = 1;
done:
	if(unc_prof_data) free(unc_prof_data);
	if(cmpr_prof_data) free(cmpr_prof_data);
	if(c) delete c;
	return retval;
#else
	mesg(MSG_E,_T("Importing profiles not supported; requires zlib."));
	return 0;
#endif
}

// Ask the user for a filename, and import it as an ICC profile
int ImportICCProfile(Png *png)
{
#ifdef TWPNG_HAVE_ZLIB
	TCHAR fn[MAX_PATH];
	OPENFILENAME ofn;
	BOOL bRet;
	int retval = 0;

	if(!png) goto done;

	StringCchCopy(fn,MAX_PATH,_T(""));
	ZeroMemory(&ofn,sizeof(OPENFILENAME));

	ofn.lStructSize=sizeof(OPENFILENAME);
	ofn.hwndOwner=globals.hwndMain;
	ofn.hInstance=NULL;
	ofn.lpstrFilter=_T("*.icc\0*.icc\0*.*\0*.*\0\0");
	ofn.nFilterIndex=1;
	ofn.lpstrTitle=_T("Import ICC profile...");
	ofn.lpstrFile=fn;
	ofn.nMaxFile=MAX_PATH;
	ofn.Flags=OFN_FILEMUSTEXIST|OFN_HIDEREADONLY|OFN_NOCHANGEDIR;

	globals.dlgs_open++;
	bRet = GetOpenFileName(&ofn);
	globals.dlgs_open--;
	if(!bRet) goto done;

	if(!ImportICCProfileByFilename(png,fn)) goto done;

	retval = 1;
done:
	return retval;
#else
	mesg(MSG_E,_T("Importing profiles not supported; requires zlib."));
	return 0;
#endif
}

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

static double read_s15Fixed16Number(unsigned char *d)
{
	return ((double)(int)(unsigned int)read_int32(d))/65536.0;
}

static void twpng_dump_iccp_XYZData(struct iccp_ctx_struct *ctx,
	unsigned char *d, unsigned int t_size, const TCHAR *prefix)
{
	double x,y,z;
	if(t_size!=12) return;
	x = read_s15Fixed16Number(&d[0]);
	y = read_s15Fixed16Number(&d[4]);
	z = read_s15Fixed16Number(&d[8]);
	twpng_iccp_append_textf(ctx,_T("%sX=%.5f, Y=%.5f, Z=%.5f\r\n"),prefix,x,y,z);
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
	twpng_iccp_append_textf(ctx,_T("Profile creation time: %s UTC\r\n"),buf);

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

	u = read_int32(&ctx->data[60]);
	twpng_iccp_append_textf(ctx,_T("Attributes: %s, %s, %s, %s\r\n"),
		(u&0x01)?_T("Transparency"):_T("Reflective"),
		(u&0x02)?_T("Matte"):_T("Glossy"),
		(u&0x04)?_T("Negative"):_T("Positive"),
		(u&0x08)?_T("B&W"):_T("Color"));

	u = read_int32(&ctx->data[64]);
	u = u & 0xffff;
	twpng_iccp_append_textf(ctx,_T("Rendering intent: %u (%s)\r\n"),u,
		get_rendering_intent_descr(u,buf,100));

	// Bytes 68-79: illuminant
	twpng_dump_iccp_XYZData(ctx,&ctx->data[68],12,_T("Illuminant: "));

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
	twpng_iccp_append_textf(ctx,_T("\t") SYM_LDQUO _T("%s") SYM_RDQUO _T("\r\n"),buf);
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
		twpng_iccp_append_textf(ctx,_T("\t") SYM_LDQUO _T("%s") SYM_RDQUO _T("\r\n"),buf);
	}
	// TODO?: Display the Unicode and Scriptcode versions of the string,
	// if present.
}

static void twpng_dump_iccp_std_observer(struct iccp_ctx_struct *ctx,
	unsigned char *d)
{
	unsigned int u;
	const TCHAR *itypes[3] = { _T("unknown"), _T("1931 2 degree observer"),
		_T("1964 10 degree observer") };
	u = read_int32(d);
	twpng_iccp_append_textf(ctx,_T("\tstandard observer: %u (%s)\r\n"),u,
		u<=2 ? itypes[u] : _T("unrecognized"));
}

static void twpng_dump_iccp_meas_geometry(struct iccp_ctx_struct *ctx,
	unsigned char *d)
{
	unsigned int u;
	const TCHAR *itypes[3] = { _T("unknown"), _T("0/45 or 45/0"),
		_T("0/d or d/0") };
	u = read_int32(d);
	twpng_iccp_append_textf(ctx,_T("\tmeasurement geometry: %u (%s)\r\n"),u,
		u<=2 ? itypes[u] : _T("unrecognized"));
}

static void twpng_dump_iccp_meas_flare(struct iccp_ctx_struct *ctx,
	unsigned char *d)
{
	unsigned int u;
	const TCHAR *s;
	u = read_int32(d);
	if(u==0) s=_T("0 (0%)");
	else if(u==0x10000) s=_T("1.0 (or 100%)");
	else s=_T("unrecognized");
	twpng_iccp_append_textf(ctx,_T("\tmeasurement flare: %u (%s)\r\n"),u,s);
}

static void twpng_dump_iccp_illuminant_type(struct iccp_ctx_struct *ctx,
	unsigned char *d)
{
	unsigned int u;
	const TCHAR *itypes[9] = { _T("unknown"), _T("D50"), _T("D65"), _T("D93"),
		_T("F2"), _T("D55"), _T("A"), _T("Equi-Power (E)"), _T("F8") };
	u = read_int32(d);
	twpng_iccp_append_textf(ctx,_T("\tilluminant type: %u (%s)\r\n"),u,
		u<=8 ? itypes[u] : _T("unrecognized"));
}

static void twpng_dump_iccp_viewingConditions(struct iccp_ctx_struct *ctx,
	unsigned char *d, unsigned int t_size)
{
	if(t_size!=28) return;
	twpng_dump_iccp_XYZData(ctx,&d[0],12,_T("\tilluminant: "));
	twpng_dump_iccp_XYZData(ctx,&d[12],12,_T("\tsurround: "));
	twpng_dump_iccp_illuminant_type(ctx,&d[24]);
}

static void twpng_dump_iccp_measurementType(struct iccp_ctx_struct *ctx,
	unsigned char *d, unsigned int t_size)
{
	if(t_size!=28) return;
	twpng_dump_iccp_std_observer(ctx,&d[0]);
	twpng_dump_iccp_XYZData(ctx,&d[4],12,_T("\tmeasurement backing: "));
	twpng_dump_iccp_meas_geometry(ctx,&d[16]);
	twpng_dump_iccp_meas_flare(ctx,&d[20]);
	twpng_dump_iccp_illuminant_type(ctx,&d[24]);
}

static void twpng_dump_iccp_signatureType(struct iccp_ctx_struct *ctx,
	unsigned char *d, unsigned int t_size)
{
	TCHAR buf[20];
	if(t_size!=4) return;
	twpng_iccp_bytes_to_hex(ctx,d,4,buf,20);
	twpng_iccp_append_textf(ctx,_T("\t%s\r\n"),buf);
}

static void twpng_dump_iccp_tag(struct iccp_ctx_struct *ctx, unsigned int t_offset,
	unsigned int t_size)
{
	unsigned int type_code;
	unsigned char *dataptr;
	unsigned int datasize;

	if(t_size  > 100000000) return;
	if(t_size<8) return;
	if(t_offset>1000000000) return;
	if(t_offset+t_size>(unsigned int)ctx->data_len) return;

	type_code = read_int32(&ctx->data[t_offset]);

	dataptr = &ctx->data[t_offset+8];
	datasize = t_size-8;

	switch(type_code) {
	case 0x74657874: // 'text'
		twpng_dump_iccp_textType(ctx,dataptr,datasize);
		break;
	case 0x64657363: // 'desc'
		twpng_dump_iccp_textDescriptionType(ctx,dataptr,datasize);
		break;
	case 0x58595a20: // 'XYZ '
		twpng_dump_iccp_XYZData(ctx,dataptr,datasize,_T("\t"));
		break;
	case 0x76696577: // 'view'
		twpng_dump_iccp_viewingConditions(ctx,dataptr,datasize);
		break;
	case 0x6d656173: // 'meas'
		twpng_dump_iccp_measurementType(ctx,dataptr,datasize);
		break;
	case 0x73696720: // 'sig '
		twpng_dump_iccp_signatureType(ctx,dataptr,datasize);
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

void Chunk::init_iCCP_dlg(struct edit_chunk_ctx *ecctx, HWND hwnd)
{
	struct keyword_info_struct kw;
	struct iccp_ctx_struct ctx;
	RECT rd,r1;

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

	GetClientRect(hwnd,&rd);
	GetPosInParent(GetDlgItem(hwnd,IDOK),&r1);

	ecctx->tdm.border_buttonoffset = (rd.right-rd.left)-r1.left;
	ecctx->tdm.border_btn1y = r1.top;

	GetPosInParent(GetDlgItem(hwnd,IDCANCEL),&r1);
	ecctx->tdm.border_btn2y = r1.top;

	GetPosInParent(GetDlgItem(hwnd,IDC_EDIT2),&r1);
	ecctx->tdm.border_editx = (rd.right-rd.left)-r1.right;
	ecctx->tdm.border_edity = (rd.bottom-rd.top)-r1.bottom;
}

void Chunk::size_iCCP_dlg(struct edit_chunk_ctx *ecctx, HWND hwnd)
{
	RECT rd,r1;
	HWND h;

	GetClientRect(hwnd,&rd);

	SetWindowPos(GetDlgItem(hwnd,IDOK),NULL,rd.right-ecctx->tdm.border_buttonoffset,ecctx->tdm.border_btn1y,0,0,SWP_NOSIZE|SWP_NOZORDER);
	SetWindowPos(GetDlgItem(hwnd,IDCANCEL),NULL,rd.right-ecctx->tdm.border_buttonoffset,ecctx->tdm.border_btn2y,0,0,SWP_NOSIZE|SWP_NOZORDER);

	h=GetDlgItem(hwnd,IDC_EDIT2);
	GetPosInParent(h,&r1);
	SetWindowPos(h,NULL,0,0,
		(rd.right-rd.left)-r1.left-ecctx->tdm.border_editx,
		(rd.bottom-rd.top)-r1.top-ecctx->tdm.border_edity,
		SWP_NOMOVE|SWP_NOZORDER);
}

void Chunk::process_iCCP_dlg(struct edit_chunk_ctx *ecctx, HWND hwnd)
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
