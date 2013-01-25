// chunk.cpp
//
//
/*
    Copyright (C) 1999-2008 Jason Summers

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
#include <stdarg.h>
#include <assert.h>

#include "resource.h"
#include "tweakpng.h"
#ifdef TWPNG_HAVE_ZLIB
#include <zlib.h>
#endif
#include <strsafe.h>

extern struct globals_struct globals;

static const TCHAR *known_text_keys[] = { _T("Title"),_T("Author"),_T("Description"),
	_T("Copyright"),_T("Creation Time"),_T("Software"),_T("Disclaimer"),
	_T("Warning"),_T("Source"),_T("Comment"),NULL };


static INT_PTR CALLBACK DlgProcEdit_tEXt(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcEdit_PLTE(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcGetInt(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct get_int_ctx {
	int value;   // initial value and final value
	TCHAR *label;
	TCHAR *title;  // window title
};


typedef struct {
	unsigned char red;
	unsigned char green;
	unsigned char blue;
	unsigned char alpha;
} palent_t;

struct edit_plte_ctx {
	int numplte;
	int minplte;
	int maxplte;
	int numtrns;
	int mintrns;  // 1 if a trns chunk exists, otherwise 0
	int numbkgd;   // 1 if a bkgd chunk exists, otherwise 0
	palent_t  plte[256];
	unsigned char     bkgd;
	unsigned char     trnscolor; // only used for grayscale images

	HWND hwnd;
	int bkgdbuttonstate;
	int alphabuttonstate;

	int caneditcolors; // can palette colors be edited? (not if grayscale)

	HWND hwndEditPal;
	int border_buttonoffset;
	int border_editx;
	int border_edity;
	int border_btn1y;
	int border_btn2y;
	int color_left;
	int border_width;
	int border_height;
	int color_top;

	int item_w, item_h;  // size in pixels of each box
	int curr_i;
};

typedef struct {
	int id;
	char *name;
} chunk_id_struct_t;

static const chunk_id_struct_t chunk_id_list[] = {
	{CHUNK_IHDR,"IHDR"},
	{CHUNK_IEND,"IEND"},
	{CHUNK_IDAT,"IDAT"},
	{CHUNK_PLTE,"PLTE"},
	{CHUNK_bKGD,"bKGD"},
	{CHUNK_cHRM,"cHRM"},
	{CHUNK_gAMA,"gAMA"},
	{CHUNK_hIST,"hIST"},
	{CHUNK_pHYs,"pHYs"},
	{CHUNK_sBIT,"sBIT"},
	{CHUNK_tEXt,"tEXt"},
	{CHUNK_tIME,"tIME"},
	{CHUNK_tRNS,"tRNS"},
	{CHUNK_zTXt,"zTXt"},
	{CHUNK_sRGB,"sRGB"},
	{CHUNK_iCCP,"iCCP"},
	{CHUNK_iTXt,"iTXt"},
	{CHUNK_sPLT,"sPLT"},
              
	{CHUNK_oFFs,"oFFs"},
	{CHUNK_pCAL,"pCAL"},
	{CHUNK_sCAL,"sCAL"},
	{CHUNK_gIFg,"gIFg"},
	{CHUNK_gIFx,"gIFx"},
	{CHUNK_gIFt,"gIFt"},
	{CHUNK_fRAc,"fRAc"},

	{CHUNK_sTER,"sTER"},
	{CHUNK_dSIG,"dSIG"},
	{CHUNK_acTL,"acTL"},
	{CHUNK_fcTL,"fcTL"},
	{CHUNK_fdAT,"fdAT"},
	{CHUNK_CgBI,"CgBI"},
	{CHUNK_vpAg,"vpAg"},

	{CHUNK_MHDR,"MHDR"},
	{CHUNK_MEND,"MEND"},
          
	{CHUNK_LOOP,"LOOP"},
	{CHUNK_ENDL,"ENDL"},
	{CHUNK_DEFI,"DEFI"},
	{CHUNK_JHDR,"JHDR"},
	{CHUNK_BASI,"BASI"},
	{CHUNK_CLON,"CLON"},
	{CHUNK_DHDR,"DHDR"},
	{CHUNK_PAST,"PAST"},
	{CHUNK_DISC,"DISC"},
	{CHUNK_BACK,"BACK"},
	{CHUNK_FRAM,"FRAM"},
	{CHUNK_MOVE,"MOVE"},
	{CHUNK_CLIP,"CLIP"},
	{CHUNK_SHOW,"SHOW"},
	{CHUNK_TERM,"TERM"},
	{CHUNK_SAVE,"SAVE"},
	{CHUNK_SEEK,"SEEK"},
	{CHUNK_eXPI,"eXPI"},
	{CHUNK_fPRI,"fPRI"},
	{CHUNK_nEED,"nEED"},
	{CHUNK_pHYg,"pHYg"},
	{CHUNK_JDAT,"JDAT"},
	{CHUNK_JSEP,"JSEP"},
	{CHUNK_PROM,"PROM"},
	{CHUNK_IPNG,"IPNG"},
	{CHUNK_PPLT,"PPLT"},
	{CHUNK_IJNG,"IJNG"},
	{CHUNK_DROP,"DROP"},
	{CHUNK_DBYK,"DBYK"},
	{CHUNK_ORDR,"ORDR"},
	{0,NULL}};

int Chunk::is_critical()      { return (m_chunktype_ascii[0]&0x20)?0:1; }
int Chunk::is_public()        { return (m_chunktype_ascii[1]&0x20)?0:1; }
int Chunk::is_safe_to_copy()  { return (m_chunktype_ascii[3]&0x20)?1:0; }


#ifdef TWPNG_HAVE_ZLIB

static DWORD get_uncompressed_size(unsigned char *datain, int inlen,
	int *perrflag, TCHAR *errmsg, int errmsglen)
{
	unsigned char dbuf[16384];
	int data_read=0;
	int err;

	z_stream z;

	*perrflag = 0;
	ZeroMemory((void*)&z,sizeof(z_stream));

	z.opaque=0;
	z.next_in = datain;
	z.avail_in = inlen;

	inflateInit(&z);

	while(1) {
		z.next_out = dbuf;
		z.avail_out = sizeof(dbuf);
		err = inflate(&z, Z_NO_FLUSH);
		if(err == Z_STREAM_END) break;
		if(err != Z_OK) {
			*perrflag = 1;
			if(errmsg) {
#ifdef UNICODE
				StringCchPrintf(errmsg,errmsglen,_T("%S"),z.msg);
#else
				StringCchPrintf(errmsg,errmsglen,"%s",z.msg);
#endif
			}
			inflateEnd(&z);
			return 0;
		}
	}
	inflateEnd(&z);
	return z.total_out;
}


// returns length of compressed data
// allocs a new buffer for the data
int twpng_compress_data(unsigned char **dataoutp, unsigned char*datain, int inlen)
{
	z_stream z;
	unsigned char *dataout;
	int alloced;
	int err;

	alloced=  (int)((double)inlen*1.02) + 50;
	dataout= (unsigned char*)malloc(alloced);
	if(!dataout) {
		mesg(MSG_E,_T("can") SYM_RSQUO _T("t alloc memory for compress"));
		return 0;
	}

	ZeroMemory((void*)&z,sizeof(z_stream));

	z.opaque=0;
	z.next_in = datain;
	z.avail_in = inlen;
	z.next_out =  dataout;
	z.avail_out = alloced;

	err = deflateInit(&z, globals.compression_level);   //Z_DEFAULT_COMPRESSION

	err = deflate(&z, Z_FINISH);
	if(err != Z_STREAM_END) {
		mesg(MSG_E,_T("error compressing data"));
		err = deflateEnd(&z);
		(*dataoutp)=NULL;
		return 0;
	}

	err = deflateEnd(&z);
	(*dataoutp)=dataout;
	return z.total_out;
}

// On success, sets *dataoutp to an alloc'd memory block, and returns its length.
// On failure, sets *dataoutp to NULL, and returns 0.
int twpng_uncompress_data(unsigned char **dataoutp, unsigned char *datain, int inlen)
{
	int alloced=0;
	unsigned char *dataout;
	z_stream z;
	int data_written;
	int ret;
	DWORD unc_size;
	int errflag;
	TCHAR errmsg[200];

	*dataoutp = NULL;

	// We uncompress it once just to figure out how big a buffer we
	// need to allocate. It's wasteful, but no big deal
	// in this program, I think.
	unc_size=get_uncompressed_size(datain,inlen,&errflag,errmsg,200);
	if(errflag) {
		mesg(MSG_E,_T("inflate error: %s"),errmsg);
		return 0;
	}

	alloced=unc_size+50;       // just unc_size should work

	dataout= (unsigned char*)malloc(alloced);
	if(!dataout) {
		mesg(MSG_E,_T("can") SYM_RSQUO _T("t alloc memory for uncompress"));
		return 0;
	}

	data_written=0;

	ZeroMemory((void*)&z,sizeof(z_stream));

	z.opaque=0;
	z.next_in = datain;
	z.avail_in = inlen;
	z.next_out =  dataout;         // &dataout[data_written];
	z.avail_out = alloced;

	inflateInit(&z);

	ret = inflate(&z,Z_FINISH);
	inflateEnd(&z);

	if(ret != Z_STREAM_END) {
		mesg(MSG_E,_T("decompression error"));
		free(dataout);
		return 0;
	}

	(*dataoutp)=dataout;
	return z.total_out;
}
#endif

// returns 0 if canceled
int choose_color_dialog(HWND hwnd, unsigned char *redp,
						unsigned char *greenp, unsigned char *bluep)
{
	CHOOSECOLOR cc;

	cc.lStructSize=sizeof(CHOOSECOLOR);
	cc.hwndOwner=hwnd;
	cc.hInstance=NULL;
	cc.rgbResult= RGB(*redp,*greenp,*bluep);  // set initial value
	cc.lpCustColors=globals.custcolors;
	cc.Flags=CC_RGBINIT|CC_FULLOPEN;
	cc.lCustData=0;
	cc.lpfnHook=NULL;
	cc.lpTemplateName=NULL;

	if(ChooseColor(&cc)) {
		(*redp)   = GetRValue(cc.rgbResult);
		(*greenp) = GetGValue(cc.rgbResult);
		(*bluep)  = GetBValue(cc.rgbResult);
		return 1;
	}
	return 0;
}


// indata is not null terminated,
// outdata is null terminated, and must be freed by caller
static void lf2crlf(TCHAR **outdatap, TCHAR *indata, int len)
{
	int lfcount=0;
	TCHAR *outdata;
	int i,p;

	(*outdatap)=NULL;

	for(i=0;i<len;i++) {
		if(indata[i]=='\n') lfcount++;
	}

	outdata=(TCHAR*)malloc(sizeof(TCHAR)*(len+lfcount+1));
	if(!outdata) return;

	p=0;
	for(i=0;i<len;i++) {
		if(indata[i]=='\n') {
			outdata[p++]='\r';
			outdata[p++]='\n';
		}
		else {
			outdata[p++]=indata[i];
		}
	}
	assert(p==len+lfcount);
	outdata[p]='\0';

	(*outdatap)=outdata;
}

// input must be null-terminated
// output will be null-terminated
// this conversion is in-place
static void crlf2lf(TCHAR *outdata)
{
	int d,i,len;

	len=lstrlen(outdata);

	d=0;
	for(i=0;i<len;i++) {
		if(outdata[i]=='\r' && outdata[i+1]=='\n') {
			outdata[i-d]='\n';
			d++;
		}
		else {
			outdata[i-d]=outdata[i];
		}
	}
	outdata[len-d]='\0';
}

int Chunk::copy_to_memory(unsigned char *m)
{
#if 0
	write_int32(&m[0],length);
	memcpy(&m[4],m_chunktype_ascii,4);
	memcpy(&m[8],data,length);
	write_int32(&m[8+length],m_crc);
#endif
	copy_segment_to_memory(m,0,length+12);
	return 1;
}

// We maintain both an ASCII and a TCHAR (optionally Unicode)
// version of the 4-character "chunktype" string.
// This function copies/converts the ascii version to the
// TCHAR version.
void Chunk::set_chunktype_tchar_from_ascii()
{
	int i;
	for(i=0;i<=4;i++) { m_chunktype_tchar[i] = (TCHAR)m_chunktype_ascii[i]; }
}

// returns bytes consumed; 0 if error
int Chunk::init_from_memory(unsigned char *m, int msize)
{
	if(msize<12) return 0;

	length= read_int32(&m[0]);
	if((int)length+12>msize) return 0;

	memcpy(m_chunktype_ascii,&m[4],4);
	m_chunktype_ascii[4]='\0';
	set_chunktype_tchar_from_ascii();

	data=(unsigned char*)malloc(length);
	if(!data) {
		mesg(MSG_S,_T("can") SYM_RSQUO _T("t alloc memory for new chunk"));
		return 0;
	}
	memcpy(data,&m[8],length);
	// crc is next, but we'll ignore it

	after_init();
	chunkmodified();

	return length+12;
}

int Chunk::edit_plte_info()
{
	Chunk *ch_plte;
	Chunk *ch_trns;
	Chunk *ch_bkgd;
	struct edit_plte_ctx pal_info;
	int maxpal;
	int i;
	INT_PTR changed;
	int grayscale;

	// set defaults

	ch_plte=ch_trns=ch_bkgd=NULL;
	grayscale= (m_parentpng->m_colortype==0 || m_parentpng->m_colortype==4);

	ZeroMemory((void*)&pal_info,sizeof(struct edit_plte_ctx));
	for(i=0;i<256;i++) {
		pal_info.plte[i].red   = 0;
		pal_info.plte[i].green = 0;
		pal_info.plte[i].blue  = 0;
		pal_info.plte[i].alpha = 255;
	}
	pal_info.numbkgd=0;
	pal_info.bkgd=0;
	pal_info.numtrns=0;
	pal_info.mintrns=0;
	pal_info.minplte=1;
	pal_info.bkgdbuttonstate=0;
	pal_info.alphabuttonstate=0;
	pal_info.caneditcolors=1;
	pal_info.trnscolor=0;

	pal_info.item_w=1;
	pal_info.item_h=1;
	pal_info.curr_i= -1;

	maxpal= 1<<m_parentpng->m_bitdepth;

	switch(m_chunktype_id) {
	case CHUNK_PLTE:
		ch_plte=this;
		break;
	case CHUNK_tRNS:
		ch_trns=this;
		pal_info.alphabuttonstate=1;
		break;
	case CHUNK_bKGD:
		ch_bkgd=this;
		pal_info.bkgdbuttonstate=1;
		break;
	}

	if(!ch_plte && !grayscale) ch_plte= m_parentpng->find_first_chunk(CHUNK_PLTE,NULL);
	if(!ch_trns && m_parentpng->m_colortype==3) ch_trns= m_parentpng->find_first_chunk(CHUNK_tRNS,NULL);
	if(!ch_bkgd && m_parentpng->m_colortype==3) ch_bkgd= m_parentpng->find_first_chunk(CHUNK_bKGD,NULL);

	if(!ch_plte && !grayscale) {
		mesg(MSG_E,_T("Need a PLTE chunk to edit this"));
		return 0;
	}

	if(ch_trns) {
		pal_info.mintrns= 1;
		if(grayscale) {
			pal_info.numtrns=1;
			if(ch_trns->length==2) {
				pal_info.trnscolor = read_int16(&ch_trns->data[0]);
			}
		}
		else {
			pal_info.numtrns= ch_trns->length;
			if(pal_info.numtrns > maxpal) pal_info.numtrns= maxpal;

			for(i=0;i<pal_info.numtrns;i++) {
				pal_info.plte[i].alpha= ch_trns->data[i];
			}
		}
	}

	if(ch_bkgd) {
		if(grayscale) {
			if(ch_bkgd->length == 2) {
				pal_info.numbkgd= 1;
				pal_info.bkgd = read_int16(&ch_bkgd->data[0]);
			}
		}
		else {
			if(ch_bkgd->length == 1) {
				pal_info.numbkgd= 1;
				pal_info.bkgd= ch_bkgd->data[0];
			}
		}
	}

	if(grayscale) {
		pal_info.maxplte= maxpal;
		pal_info.numplte= maxpal;
		for(i=0;i<maxpal;i++) {
			pal_info.plte[i].red = i*(255/(maxpal-1));
			pal_info.plte[i].green = pal_info.plte[i].red;
			pal_info.plte[i].blue  = pal_info.plte[i].red;
		}
		pal_info.caneditcolors=0;
	}
	else {
		pal_info.maxplte= maxpal;
		pal_info.numplte= (ch_plte->length)/3;
		if(pal_info.numplte > maxpal) pal_info.numplte= maxpal;
		for(i=0;i<pal_info.numplte;i++) {
			pal_info.plte[i].red   = ch_plte->data[3*i];
			pal_info.plte[i].green = ch_plte->data[3*i+1];
			pal_info.plte[i].blue  = ch_plte->data[3*i+2];
		}
	}
	globals.dlgs_open++;
	changed=DialogBoxParam(globals.hInst,_T("DLG_PLTE"),globals.hwndMain,
		DlgProcEdit_PLTE, (LPARAM)(&pal_info));
	globals.dlgs_open--;
	if(changed>0) {
		if(ch_bkgd) {
			if(grayscale) {
				write_int16(&ch_bkgd->data[0],pal_info.bkgd);
			}
			else {
				ch_bkgd->data[0]= pal_info.bkgd;
			}
			ch_bkgd->chunkmodified();
		}
		if(ch_trns) {
			if(grayscale) {
				if(ch_trns->length==2) {
					write_int16(&ch_trns->data[0],pal_info.trnscolor);
				}
			}
			else {
				if(ch_trns->length != (unsigned int)pal_info.numtrns) {
					// size of alpha palette changed, need to reallocate
					free(ch_trns->data);
					ch_trns->data=(unsigned char*)malloc(pal_info.numtrns);
					ch_trns->length=(unsigned int)pal_info.numtrns;
				}
				for(i=0;i<pal_info.numtrns;i++) {
					ch_trns->data[i]=pal_info.plte[i].alpha;
				}
			}
			ch_trns->chunkmodified();
		}
		if(ch_plte) {
			if(ch_plte->length != (unsigned int)(3*pal_info.numplte) ) {
				// size of palette changed, need to reallocate
				free(ch_plte->data);
				ch_plte->data=(unsigned char*)malloc(3*pal_info.numplte);
				ch_plte->length=(unsigned int)(3*pal_info.numplte);
			}
			for(i=0;i<pal_info.numplte;i++) {
				ch_plte->data[3*i  ]=pal_info.plte[i].red;
				ch_plte->data[3*i+1]=pal_info.plte[i].green;
				ch_plte->data[3*i+2]=pal_info.plte[i].blue;
			}
			ch_plte->chunkmodified();
		}
	}
	if(changed==1) changed=2;
	return (int)changed;
}

int can_edit_chunk_type(int ct)
{
	switch(ct) {
	case CHUNK_IHDR: case CHUNK_PLTE: case CHUNK_bKGD:
	case CHUNK_gAMA: case CHUNK_cHRM: case CHUNK_tRNS:
	case CHUNK_sBIT: case CHUNK_sRGB: case CHUNK_tEXt:
	case CHUNK_pHYs: case CHUNK_tIME: case CHUNK_sTER:
	case CHUNK_acTL: case CHUNK_fcTL: case CHUNK_fdAT:
	case CHUNK_oFFs: case CHUNK_sCAL: case CHUNK_iCCP:
	case CHUNK_vpAg:
		return 1;
	case CHUNK_zTXt:
		return globals.zlib_available;
	case CHUNK_iTXt:
		return globals.unicode_supported;
	}
	return 0;
}

int Chunk::can_edit()
{
	return can_edit_chunk_type(m_chunktype_id);
}

// return 1 if changed, 2 if changed multiple chunks
int Chunk::edit()
{
	struct edit_chunk_ctx ecctx;
	INT_PTR changed=0;

	// The editor for most chunks can't handle invalid chunks very well.
	if(!has_valid_length()) return 0;
	ZeroMemory((void*)&ecctx,sizeof(struct edit_chunk_ctx));
	ecctx.ch = this;

	globals.dlgs_open++;

	switch(m_chunktype_id) {
	case CHUNK_tEXt:
#ifdef TWPNG_HAVE_ZLIB
	case CHUNK_zTXt:
#endif
#ifdef UNICODE
	case CHUNK_iTXt:
#endif
		changed=  DialogBoxParam(globals.hInst,_T("DLG_TEXT"),globals.hwndMain,
		    DlgProcEdit_tEXt, (LPARAM)&ecctx);
		break;

	case CHUNK_IHDR:
		changed=  DialogBoxParam(globals.hInst,_T("DLG_IHDR"),globals.hwndMain,
			Chunk::DlgProcEditChunk, (LPARAM)&ecctx);
		if(changed>0) { after_init(); changed=2; }
		break;
	case CHUNK_gAMA:
		changed=  DialogBoxParam(globals.hInst,_T("DLG_GAMA"),globals.hwndMain,
		    DlgProcEditChunk, (LPARAM)&ecctx);
		break;
	case CHUNK_sRGB:
		changed=  DialogBoxParam(globals.hInst,_T("DLG_SRGB"),globals.hwndMain,
		    DlgProcEditChunk, (LPARAM)&ecctx);
		break;
	case CHUNK_pHYs:
		changed=  DialogBoxParam(globals.hInst,_T("DLG_PHYS"),globals.hwndMain,
		    DlgProcEditChunk, (LPARAM)&ecctx);
		break;
	case CHUNK_cHRM:
		changed=  DialogBoxParam(globals.hInst,_T("DLG_CHRM"),globals.hwndMain,
		    DlgProcEditChunk, (LPARAM)&ecctx);
		break;
	case CHUNK_bKGD:
		switch(m_parentpng->m_colortype) {
		case 3:         // color palette
			goto plte_start;

		case 0: case 4:  // grayscale
			if(m_parentpng->m_bitdepth<=8) goto plte_start;
			changed=  DialogBoxParam(globals.hInst,_T("DLG_NUMBER1"),globals.hwndMain,
				DlgProcEditChunk, (LPARAM)&ecctx);
			break;
		case 2: case 6:  // rgb
			if(m_parentpng->m_bitdepth==8) {
				unsigned char cr,cg,cb;
				cr=data[1];
				cg=data[3];
				cb=data[5];

				if(choose_color_dialog(globals.hwndMain, &cr,&cg,&cb) ) {
					data[0]=0; data[1]= cr;
					data[2]=0; data[3]= cg;
					data[4]=0; data[5]= cb;
					changed=1;
				}
			}
			else {
				changed=  DialogBoxParam(globals.hInst,_T("DLG_COLOR3"),globals.hwndMain,
					DlgProcEditChunk, (LPARAM)&ecctx);
			}
			break;
		}
		break;

	case CHUNK_sBIT:
		switch(m_parentpng->m_colortype) {
		case 0:   // grayscale
			changed=  DialogBoxParam(globals.hInst,_T("DLG_NUMBER1"),globals.hwndMain,
				DlgProcEditChunk, (LPARAM)&ecctx);
			break;
		case 2: case 3:  // rbg or paletted
			changed=  DialogBoxParam(globals.hInst,_T("DLG_COLOR3"),globals.hwndMain,
				DlgProcEditChunk, (LPARAM)&ecctx);
			break;
		case 4:  // grayscale+alpha
			changed=  DialogBoxParam(globals.hInst,_T("DLG_COLOR2"),globals.hwndMain,
				DlgProcEditChunk, (LPARAM)&ecctx);
			break;
		case 6:  // rgba
			changed=  DialogBoxParam(globals.hInst,_T("DLG_COLOR4"),globals.hwndMain,
				DlgProcEditChunk, (LPARAM)&ecctx);
			break;
		}
		break;

	case CHUNK_tRNS:
		switch(m_parentpng->m_colortype) {
		case 3:
			goto plte_start;
		case 0:  // grayscale
			if(m_parentpng->m_colortype==0 && m_parentpng->m_bitdepth<=8) goto plte_start;
			changed=  DialogBoxParam(globals.hInst,_T("DLG_NUMBER1"),globals.hwndMain,
				DlgProcEditChunk, (LPARAM)&ecctx);
			break;
		case 2:  // rgb
			changed=  DialogBoxParam(globals.hInst,_T("DLG_COLOR3"),globals.hwndMain,
				DlgProcEditChunk, (LPARAM)&ecctx);
			break;
		}
		break;

	case CHUNK_PLTE:
plte_start:
		changed = edit_plte_info();
		break;

	case CHUNK_tIME:
		changed=  DialogBoxParam(globals.hInst,_T("DLG_TIME"),globals.hwndMain,
		    DlgProcEditChunk, (LPARAM)&ecctx);
		break;

	case CHUNK_sTER:
		changed=  DialogBoxParam(globals.hInst,_T("DLG_STER"),globals.hwndMain,
		    DlgProcEditChunk, (LPARAM)&ecctx);
		break;

	case CHUNK_acTL:
		changed=  DialogBoxParam(globals.hInst,_T("DLG_ACTL"),globals.hwndMain,
		    DlgProcEditChunk, (LPARAM)&ecctx);
		break;

	case CHUNK_fcTL:
		changed=  DialogBoxParam(globals.hInst,_T("DLG_FCTL"),globals.hwndMain,
		    DlgProcEditChunk, (LPARAM)&ecctx);
		break;

	case CHUNK_fdAT:
		changed=  DialogBoxParam(globals.hInst,_T("DLG_NUMBER1"),globals.hwndMain,
		    DlgProcEditChunk, (LPARAM)&ecctx);
		break;

	case CHUNK_oFFs:
		changed=  DialogBoxParam(globals.hInst,_T("DLG_OFFS"),globals.hwndMain,
		    DlgProcEditChunk, (LPARAM)&ecctx);
		break;

	case CHUNK_sCAL:
		changed=  DialogBoxParam(globals.hInst,_T("DLG_SCAL"),globals.hwndMain,
		    DlgProcEditChunk, (LPARAM)&ecctx);
		break;

	case CHUNK_iCCP:
		changed=  DialogBoxParam(globals.hInst,_T("DLG_ICCPROFILE"),globals.hwndMain,
		    DlgProcEditChunk, (LPARAM)&ecctx);
		break;

	case CHUNK_vpAg:
		changed=  DialogBoxParam(globals.hInst,_T("DLG_VPAG"),globals.hwndMain,
		    DlgProcEditChunk, (LPARAM)&ecctx);
		break;
	}

	if(changed>0) chunkmodified();    // update crc
	globals.dlgs_open--;
	return (int)changed;
}

// call after you've changed a chunk
void Chunk::chunkmodified()
{
	m_crc=calc_crc();
}

DWORD Chunk::calc_crc()
{
	DWORD ccrc;  // calculated crc

	ccrc=update_crc(CRCINIT,(unsigned char*)m_chunktype_ascii,4);
	ccrc=update_crc(ccrc,data,length);
	ccrc=CRCCOMPL(ccrc);
	return ccrc;
}

int Chunk::get_chunk_type_id()
{
	int i;
	for(i=0; chunk_id_list[i].id; i++) {
		if(!strcmp(m_chunktype_ascii,chunk_id_list[i].name))
			return chunk_id_list[i].id;
	}

	return CHUNK_UNKNOWN;
}

// returns 1 if found, 0 if not found; caller must provide name[5]
int get_name_from_id(char *name, int x)
{
	int i;
	for(i=0; chunk_id_list[i].id; i++) {
		if(chunk_id_list[i].id==x) {
			StringCchCopyA(name,5,chunk_id_list[i].name);
			return 1;
		}
	}
	StringCchCopyA(name,5,"????");
	return 0;
}

// Returns 0 if the chunk is known to be invalid because it has the wrong length.
// This is only useful for simple chunks with predictable sizes.
int Chunk::has_valid_length()
{
	int ct = m_parentpng->m_colortype;

	switch(m_chunktype_id) {
	case CHUNK_IHDR:
		return (length==13);
	case CHUNK_IEND:
		return (length==0);
	case CHUNK_bKGD:
		if(ct==3) return (length==1);
		else if(ct==0 || ct==4) return (length==2);
		else if(ct==2 || ct==6) return (length==6);
		break;
	case CHUNK_tRNS:
		if(ct==0) return (length==2);
		else if(ct==2) return (length==6);
		break;
	case CHUNK_sRGB:
		return (length==1);
	case CHUNK_sTER:
		return (length==1);
	case CHUNK_gAMA:
		return (length==4);
	case CHUNK_sBIT:
		if(ct==0) return (length==1);
		else if(ct==2 || ct==3) return (length==3);
		else if(ct==4) return (length==2);
		else if(ct==6) return (length==4);
		break;
	case CHUNK_cHRM:
		return (length==32);
	case CHUNK_pHYs:
		return (length==9);
	case CHUNK_tIME:
		return (length==7);
	case CHUNK_JHDR:
		return (length==16);
	case CHUNK_MHDR:
		return (length==28);
	case CHUNK_oFFs:
		return (length==9);
	case CHUNK_sCAL:
		return (length>=4);
	case CHUNK_acTL:
		return (length==8);
	case CHUNK_fcTL:
		return (length==26);
	case CHUNK_fdAT:
		return (length>=4);
	case CHUNK_vpAg:
		return (length==9);
	}

	return 1;
}

void Chunk::msg_invalid_length(TCHAR *buf, int buflen, const TCHAR *name)
{
	StringCchPrintf(buf,buflen,_T("%s; incorrect chunk length"),name);
}

// If the chunk is known to be invalid because it has the wrong length,
// put an error message in 'buf' and return 1.
int Chunk::msg_if_invalid_length(TCHAR *buf, int buflen, const TCHAR *name)
{
	if(has_valid_length()) return 0;
	msg_invalid_length(buf,buflen,name);
	return 1;
}

void Chunk::describe_IHDR(TCHAR *buf, int buflen)
{
	TCHAR buf2[500];
	if(msg_if_invalid_length(buf,buflen,_T("PNG image header"))) return;

	StringCchPrintf(buf,buflen,_T("PNG image header: %u%s%u"),read_int32(&data[0]),SYM_TIMES,read_int32(&data[4]));
	StringCchPrintf(buf2,500,_T(", %u bit%s/%s"),(DWORD)(data[8]),(data[8]==1)?_T(""):_T("s"),
		(data[9]==3)?_T("pixel"):_T("sample"));
	StringCchCat(buf,buflen,buf2);

	switch(data[9]) {
	case 0: StringCchCopy(buf2,500,_T(", grayscale")); break;
	case 2: StringCchCopy(buf2,500,_T(", truecolor")); break;
	case 3: StringCchCopy(buf2,500,_T(", paletted")); break;
	case 4: StringCchCopy(buf2,500,_T(", grayscale+alpha")); break;
	case 6: StringCchCopy(buf2,500,_T(", truecolor+alpha")); break;
	default: StringCchPrintf(buf2,500,_T(", illegal color type (%u)"),(DWORD)(data[9]));
	}
	StringCchCat(buf,buflen,buf2);

	switch(data[12]) {
	case 0: StringCchCopy(buf2,500,_T(", noninterlaced")); break;
	case 1: StringCchCopy(buf2,500,_T(", interlaced")); break;
	default:
		StringCchPrintf(buf2,500,_T(", unrecognized interlace method (%u)"),(DWORD)(data[12]));
	}
	StringCchCat(buf,buflen,buf2);
}

void Chunk::describe_JHDR(TCHAR *buf, int buflen)
{
	TCHAR buf2[500];
	int x,ct;

	if(msg_if_invalid_length(buf,buflen,_T("JNG file header"))) return;

	StringCchPrintf(buf,buflen,_T("JNG file header: %u") SYM_TIMES _T("%u"),
		(unsigned int)read_int32(&data[0]),
		(unsigned int)read_int32(&data[4]) );

	ct = (int)data[8];
	switch(ct) {
	case 8: StringCbPrintf(buf2,sizeof(buf2),_T(", grayscale")); break;
	case 10: StringCbPrintf(buf2,sizeof(buf2),_T(", color")); break;
	case 12: StringCbPrintf(buf2,sizeof(buf2),_T(", grayscale+alpha")); break;
	case 14: StringCbPrintf(buf2,sizeof(buf2),_T(", color+alpha")); break;
	default: StringCbPrintf(buf2,sizeof(buf2),_T(", color type=%d"),ct);
	}
	StringCchCat(buf,buflen,buf2);

	x = (int)data[9]; // Image_sample_depth
	if(x==20)
		StringCbPrintf(buf2,sizeof(buf2),_T(", 8 and 12-bit"));
	else
		StringCbPrintf(buf2,sizeof(buf2),_T(", %d-bit"),x);
	StringCchCat(buf,buflen,buf2);

	if(data[11]==8) { // Image_interlace_method
		StringCchCat(buf,buflen,_T(", progressive"));
	}

	if(ct==12 || ct==14) { // If an alpha channel is present...
		x = (int)data[12]; // Alpha_sample_depth
		StringCbPrintf(buf2,sizeof(buf2),_T(", alpha: %d-bit"),x);
		StringCchCat(buf,buflen,buf2);

		x = (int)data[13]; // Alpha_compression_method
		switch(x) {
		case 0: StringCbPrintf(buf2,sizeof(buf2),_T(", alpha cmpr.: PNG")); break;
		case 8: StringCbPrintf(buf2,sizeof(buf2),_T(", alpha cmpr.: JDAA")); break;
		default: StringCbPrintf(buf2,sizeof(buf2),_T(""));
		}
		StringCchCat(buf,buflen,buf2);
	}
}

void Chunk::describe_MHDR(TCHAR *buf, int buflen)
{
	TCHAR buf2[500];
	unsigned int u;

	if(msg_if_invalid_length(buf,buflen,_T("MNG file header"))) return;

	StringCchPrintf(buf,buflen,_T("MNG file header: %u") SYM_TIMES _T("%u"),
		(unsigned int)read_int32(&data[0]),
		(unsigned int)read_int32(&data[4]) );

	u=read_int32(&data[8]); // Ticks_per_second
	StringCbPrintf(buf2,sizeof(buf2),_T(", %u ticks/sec."),u);
	StringCchCat(buf,buflen,buf2);

	u=read_int32(&data[12]); // Nominal_layer_count
	if(u) {
		StringCbPrintf(buf2,sizeof(buf2),_T(", %u layers"),u);
		StringCchCat(buf,buflen,buf2);
	}

	u=read_int32(&data[16]); // Nominal_frame_count
	if(u) {
		StringCbPrintf(buf2,sizeof(buf2),_T(", %u frames"),u);
		StringCchCat(buf,buflen,buf2);
	}

	u=read_int32(&data[20]); // Nominal_play_time
	if(u) {
		StringCbPrintf(buf2,sizeof(buf2),_T(", %u ticks"),u);
		StringCchCat(buf,buflen,buf2);
	}

	u=read_int32(&data[24]); // Simplicity_profile
	StringCbPrintf(buf2,sizeof(buf2),_T(", profile: 0x%08x"),u);
	StringCchCat(buf,buflen,buf2);
}

void Chunk::describe_IEND(TCHAR *buf, int buflen)
{
	if(msg_if_invalid_length(buf,buflen,_T("end-of-image marker"))) return;
	StringCchCopy(buf,buflen,_T("end-of-image marker"));
}

void Chunk::describe_bKGD(TCHAR *buf, int buflen)
{
	if(msg_if_invalid_length(buf,buflen,_T("background color"))) return;

	if(m_parentpng->m_imgtype != IMG_PNG) {
		StringCchCopy(buf,buflen,_T("background color"));
		return;
	}

	if(m_parentpng->m_colortype==3) {   // paletted
		if(length==1) {
			StringCchPrintf(buf,buflen,_T("background color = palette entry %u"),(DWORD)data[0]);
			return;
		}
	}
	else if(m_parentpng->m_colortype==0 || m_parentpng->m_colortype==4) { // grayscale
		if(length==2) {
			StringCchPrintf(buf,buflen,_T("background color = (%u)"),
				(DWORD)read_int16(&data[0]));
			return;
		}
	}
	else if(m_parentpng->m_colortype==2 || m_parentpng->m_colortype==6) {  // truecolor
		if(length==6) {
			StringCchPrintf(buf,buflen,_T("background color = (%u,%u,%u)"),
				(DWORD)read_int16(&data[0]),
				(DWORD)read_int16(&data[2]),
				(DWORD)read_int16(&data[4]));
			return;
		}
	}

	StringCchCopy(buf,buflen,_T("invalid background color chunk"));

}

void Chunk::describe_hIST(TCHAR *buf, int buflen)
{
	if(length<2 || length>512 || (length%2)) {
		StringCchCopy(buf,buflen,_T("invalid histogram chunk"));
		return;
	}
	StringCchPrintf(buf,buflen,_T("histogram for palette colors, %u entr%s"),length/2,(length==2)?_T("y"):_T("ies"));
}

void Chunk::describe_PLTE(TCHAR *buf, int buflen)
{
	TCHAR buf2[80];

	switch(m_parentpng->m_colortype) {
	case 2: case 6:
		StringCchCopy(buf,buflen,_T("palette (suggested)")); break;
	case 0: case 4:
		StringCchCopy(buf,buflen,_T("illegal palette chunk")); break;
	default:	
		StringCchCopy(buf,buflen,_T("palette"));
	}

	if(length<3 || length > 768 || (length%3 !=0)) {
		StringCchCat(buf,buflen,_T(", incorrect palette size"));
		return;
	}
	StringCchPrintf(buf2,80,_T(", %u entr%s"),(DWORD)(length/3),(length==3)?_T("y"):_T("ies"));
	StringCchCat(buf,buflen,buf2);
}

void Chunk::describe_tRNS(TCHAR *buf, int buflen)
{
	if(msg_if_invalid_length(buf,buflen,_T("transparency chunk"))) return;

	switch(m_parentpng->m_colortype) {
	case 3:
		if(length<=256) {
			StringCchPrintf(buf,buflen,_T("alpha values for palette colors, %u entr%s"),length,(length==1)?_T("y"):_T("ies"));
			return;
		}
	case 0:
		if(length==2) {
			StringCchPrintf(buf,buflen,_T("transparent color = (%u)"),(DWORD)read_int16(&data[0]));
			return;
		}
		break;
	case 2:
		if(length==6) {
			StringCchPrintf(buf,buflen,_T("transparent color = (%u,%u,%u)"),
				(DWORD)read_int16(&data[0]),
				(DWORD)read_int16(&data[2]),
				(DWORD)read_int16(&data[4]));
			return;
		}
		break;
	default:
		StringCchCopy(buf,buflen,_T("illegal transparency chunk, alpha channel is present"));
		return;
	}
	StringCchCopy(buf,buflen,_T("invalid transparency chunk"));
}

void Chunk::describe_sRGB(TCHAR *buf, int buflen)
{
	TCHAR s[40];

	if(msg_if_invalid_length(buf,buflen,_T("sRGB color space"))) return;

	switch(data[0]) {
	case 0: StringCchCopy(s,40,_T("Perceptual")); break;
	case 1: StringCchCopy(s,40,_T("Relative colorimetric")); break;
	case 2: StringCchCopy(s,40,_T("Saturation")); break;
	case 3: StringCchCopy(s,40,_T("Absolute colorimetric")); break;
	default: StringCchPrintf(s,40,_T("[unknown: %d]"),(int)data[0]);
	}
	StringCchPrintf(buf,buflen,_T("sRGB color space, rendering intent: %s"),s);
}

void Chunk::describe_sTER(TCHAR *buf, int buflen)
{
	TCHAR s[40];

	if(msg_if_invalid_length(buf,buflen,_T("stereo image"))) return;

	switch(data[0]) {
	case 0: StringCchCopy(s,40,_T("Cross-fuse layout")); break;
	case 1: StringCchCopy(s,40,_T("Diverging-fuse layout")); break;
	default: StringCchPrintf(s,40,_T("[unknown: %d]"),(int)data[0]);
	}
	StringCchPrintf(buf,buflen,_T("stereo image, mode: %s"),s);
}

void Chunk::describe_gAMA(TCHAR *buf, int buflen)
{
	DWORD g;
	double g2;

	if(msg_if_invalid_length(buf,buflen,_T("file gamma"))) return;

	g=read_int32(&data[0]);
	g2=((double)(g))/100000.0;
	StringCchPrintf(buf,buflen,_T("file gamma = %.5f"),g2);

}

void Chunk::describe_sBIT(TCHAR *buf, int buflen)
{
	if(msg_if_invalid_length(buf,buflen,_T("significant bits/sample"))) return;

	switch(m_parentpng->m_colortype) {
	case 0:
		if(length==1) {
			StringCchPrintf(buf,buflen,_T("significant bits/sample: %u"),(DWORD)data[0]);
			return;
		}
		break;
	case 2: case 3:
		if(length==3) {
			StringCchPrintf(buf,buflen,_T("significant bits/sample: R:%u,G:%u,B:%u"),
				(DWORD)data[0],(DWORD)data[1],(DWORD)data[2]);
			return;
		}
		break;
	case 4:
		if(length==2) {
			StringCchPrintf(buf,buflen,_T("significant bits/sample: %u,A:%u"),
				(DWORD)data[0],(DWORD)data[1]);
			return;
		}
		break;
	case 6:
		if(length==4) {
			StringCchPrintf(buf,buflen,_T("significant bits/sample: R:%u,G:%u,B:%u,A:%u"),
				(DWORD)data[0],(DWORD)data[1],(DWORD)data[2],(DWORD)data[3]);
			return;
		}
	}

	StringCchCopy(buf,buflen,_T("invalid significant bits/sample chunk"));
}

void Chunk::describe_cHRM(TCHAR *buf, int buflen)
{
	if(msg_if_invalid_length(buf,buflen,_T("chromaticities"))) return;

	StringCchPrintf(buf,buflen,_T("chromaticities: WP(%.5f,%.5f),R(%.5f,%.5f),G(%.5f,%.5f),B(%.5f,%.5f)"),
		((double)read_int32(&data[0]))/100000.0,
		((double)read_int32(&data[4]))/100000.0,
		((double)read_int32(&data[8]))/100000.0,
		((double)read_int32(&data[12]))/100000.0,
		((double)read_int32(&data[16]))/100000.0,
		((double)read_int32(&data[20]))/100000.0,
		((double)read_int32(&data[24]))/100000.0,
		((double)read_int32(&data[28]))/100000.0);
}

void Chunk::describe_pHYs(TCHAR *buf, int buflen)
{
	TCHAR s[80];
	int x,y;

	if(msg_if_invalid_length(buf,buflen,_T("pixel size"))) return;

	x=read_int32(&data[0]);
	y=read_int32(&data[4]);

	StringCchPrintf(buf,buflen,_T("pixel size = %u%s%u pixels"),x,SYM_TIMES,y);

	switch(data[8]) {
	case 0: StringCchCat(buf,buflen,_T(" (per unspecified unit)")); break;
	case 1:
		StringCchPrintf(s,80,_T(" per meter (%.1f%s%.1f dpi)"),(double)x * 0.0254,SYM_TIMES,(double)y * 0.0254);
		StringCchCat(buf,buflen,s);
		break;
	default: StringCchCat(buf,buflen,_T(" per (unrecognized unit)"));
	}
}

void Chunk::describe_acTL(TCHAR *buf, int buflen)
{
	TCHAR s[80];
	int nframes,nplays;

	if(msg_if_invalid_length(buf,buflen,_T("APNG animation control"))) return;

	nframes=read_int32(&data[0]);
	nplays=read_int32(&data[4]);

	StringCchPrintf(buf,buflen,_T("APNG animation control: %d frame%s, play "),nframes,(nframes==1)?_T(""):_T("s"));
	if(nplays==0)
		StringCbCopy(s,sizeof(s),_T("indefinitely"));
	else
		StringCbPrintf(s,sizeof(s),_T("%d time%s"),nplays,(nplays==1)?_T(""):_T("s"));
	StringCchCat(buf,buflen,s);
}

void Chunk::describe_fcTL(TCHAR *buf, int buflen)
{
	int seq, width, height, xoffs, yoffs, d_num, d_den;
	double delay;
	const TCHAR *dispose_op;
	const TCHAR *blend_op;

	if(msg_if_invalid_length(buf,buflen,_T("APNG frame control"))) return;

	seq=read_int32(&data[0]);
	width=read_int32(&data[4]);
	height=read_int32(&data[8]);
	xoffs=read_int32(&data[12]);
	yoffs=read_int32(&data[16]);
	d_num=read_int16(&data[20]);
	d_den=read_int16(&data[22]);
	if(d_den==0) delay = ((double)d_num)/100.0;
	else delay = ((double)d_num)/(double)d_den;

	switch(data[24]) {
	case 0: dispose_op = _T("none"); break;
	case 1: dispose_op = _T("bkgd"); break;
	case 2: dispose_op = _T("prev"); break;
	default: dispose_op = _T("?"); break;
	}
	
	switch(data[25]) {
	case 0: blend_op = _T("source"); break;
	case 1: blend_op = _T("over"); break;
	default: blend_op = _T("?"); break;
	}

	StringCchPrintf(buf,buflen,_T("APNG frame control, seq#=%d, %d%s%d+%d+%d, delay=%.3fs, dispose=%s, blend=%s"),
		seq,width,SYM_TIMES,height,xoffs,yoffs,delay,dispose_op,blend_op);
}

void Chunk::describe_fdAT(TCHAR *buf, int buflen)
{
	int seq;

	if(length<4) {
		msg_invalid_length(buf,buflen,_T("APNG frame data"));
		return;
	}

	seq=read_int32(&data[0]);
	StringCchPrintf(buf,buflen,_T("APNG frame data, seq#=%d"),seq);
}

void Chunk::describe_oFFs(TCHAR *buf, int buflen)
{
	StringCchPrintf(buf,buflen,_T("oFFs"));
	int xpos,ypos;

	if(msg_if_invalid_length(buf,buflen,_T("image offset"))) return;

	xpos=(int)read_int32(&data[0]);
	ypos=(int)read_int32(&data[4]);

	StringCchPrintf(buf,buflen,_T("image offset = (%d,%d) "),xpos,ypos);

	switch(data[8]) {
	case 0: StringCchCat(buf,buflen,_T("pixels")); break;
	case 1: StringCchCat(buf,buflen,SYM_MICROMETERS); break;
	default: StringCchCat(buf,buflen,_T("(invalid units)"));
	}
}

void Chunk::describe_vpAg(TCHAR *buf, int buflen)
{
	StringCchPrintf(buf,buflen,_T("vpAg"));
	int xpos,ypos;

	if(msg_if_invalid_length(buf,buflen,_T("virtual page"))) return;

	xpos=(int)read_int32(&data[0]);
	ypos=(int)read_int32(&data[4]);

	StringCchPrintf(buf,buflen,_T("virtual page = %d") SYM_TIMES _T("%d "),xpos,ypos);

	switch(data[8]) {
	case 0: StringCchCat(buf,buflen,_T("pixels")); break;
		// I can't find documentation about any unit codes other than 0, though
		// I'd guess it's the same as the oFFs chunk.
	default: StringCchCat(buf,buflen,_T("(unknown units)"));
	}
}

// Read a text field from 'data' beginning at the specified offset, and
// ending either at a NUL byte or the end of the chunk's data.
// 'buf' is always left NUL-terminated.
// Returns the number of bytes read.
int Chunk::read_text_field(int offset, TCHAR *dst, int dstlen, unsigned int flags)
{
	unsigned char *src;
	int srclen;
	int srcpos;
	int dstpos=0;
	unsigned char c;

	src = &data[offset];
	srclen = length-offset;

	for(srcpos=0; srcpos<srclen; srcpos++) {
		if(dstpos>=dstlen-1) {
			// exceeded dst buf
			goto done;
		}
		if(srcpos>=srclen) {
			// read all source data
			goto done;
		}
		c = src[srcpos];

		if(!c) {
			// reached NUL byte in input
			goto done;
		}

		if(flags&TWPNG_FLAG_ASCIIFLOATINGPOINT) {
			if( (c>='0' && c<='9') || c=='.' || c=='+' || c=='-' || c=='e' || c=='E' ) {
				;
			}
			else {
				c='_';
			}
		}

		dst[dstpos++] = (TCHAR)c;
	}

done:
	dst[dstpos]='\0';
	return srcpos;
}

struct sCAL_data {
	int units;
	TCHAR x[100];
	TCHAR y[100];
};

void Chunk::get_sCAL_data(struct sCAL_data *d)
{
	int pos=0;
	int ret;

	d->units=1;
	d->x[0]='\0';
	d->y[0]='\0';
	if(!has_valid_length()) return;

	d->units=(int)data[0];
	pos+=1;

	ret=read_text_field(pos,d->x,100,TWPNG_FLAG_ASCIIFLOATINGPOINT);
	pos+=ret+1;
	read_text_field(pos,d->y,100,TWPNG_FLAG_ASCIIFLOATINGPOINT);
}

// Create 'data' based on the contents of the sCAL_data struct.
void Chunk::set_sCAL_data(const struct sCAL_data *d)
{
	int xlen_tchar,ylen_tchar;
	int xlen_latin1,ylen_latin1;
	int tot_len;

	char *x_latin1=NULL;
	char *y_latin1=NULL;

	xlen_tchar = lstrlen(d->x);
	ylen_tchar = lstrlen(d->y);
	convert_tchar_to_latin1(d->x,xlen_tchar,&x_latin1,&xlen_latin1);
	convert_tchar_to_latin1(d->y,ylen_tchar,&y_latin1,&ylen_latin1);
	if(!x_latin1 || !y_latin1) goto done;

	tot_len = 1 + xlen_latin1 + 1 + ylen_latin1;
	if(data) { free(data); }
	length=0;
	data = (unsigned char*)malloc(tot_len);
	if(!data) return;
	//memset(data,'x',tot_len);
	length=tot_len;
	data[0] = (unsigned char)d->units;
	memcpy(&data[1],x_latin1,xlen_latin1);
	data[1+xlen_latin1]='\0';
	memcpy(&data[1+xlen_latin1+1],y_latin1,ylen_latin1);

done:
	if(x_latin1) free(x_latin1);
	if(y_latin1) free(y_latin1);
}

void Chunk::describe_sCAL(TCHAR *buf, int buflen)
{
	TCHAR *unitsstring;
	struct sCAL_data d;

	if(msg_if_invalid_length(buf,buflen,_T("physical scale of image subject"))) return;
	get_sCAL_data(&d);

	if(d.units==1) unitsstring=_T("meters");
	else if(d.units==2) unitsstring=_T("radians");
	else unitsstring=_T("unknown units");

	StringCchPrintf(buf,buflen,_T("physical scale of image subject: %s%s%s %s"),
		d.x,SYM_TIMES,d.y,unitsstring);
}

// keyword and indata must be null-terminated
// Take text from from the text edit dialog (supplied in parameters),
// and use it to update m_text_info.* and 'data'.
int Chunk::set_text_info(const TCHAR *keyword,
						 const TCHAR *language, const TCHAR *translated_keyword,
						 const TCHAR *indata, int is_compressed, int is_international)
{
	int ct;
	int retval=0;
	int pos;
	char *kw_latin1=NULL;  int kwlen=0;
	char *text_mbcs=NULL;  int text_len=0;
	char *lang_latin1=NULL; int lang_len=0;
	char *trns_kw_utf8=NULL;  int trns_kw_len=0;
	unsigned char *cmpr_text=NULL;  int cmpr_text_len;

	// Just clear the text_info struct.
	// It will be recreated by the get_text_info call at the end of this function.
	free_text_info();

	if(data) { free(data); data=NULL; } // lose the old data

	if(is_international) {
		ct=CHUNK_iTXt;
		StringCchCopyA(m_chunktype_ascii,5,"iTXt");
	}
	else if(is_compressed) {
		ct=CHUNK_zTXt;
		StringCchCopyA(m_chunktype_ascii,5,"zTXt");
	}
	else {
		ct=CHUNK_tEXt;
		StringCchCopyA(m_chunktype_ascii,5,"tEXt");
	}
	m_chunktype_id = ct;
	set_chunktype_tchar_from_ascii();

	convert_tchar_to_latin1(keyword,lstrlen(keyword),&kw_latin1,&kwlen);

	if(is_international) {
#ifdef UNICODE
		convert_utf16_to_utf8(indata,lstrlen(indata),&text_mbcs,&text_len);
		convert_utf16_to_utf8(translated_keyword,lstrlen(translated_keyword),&trns_kw_utf8,&trns_kw_len);
		convert_tchar_to_latin1(language,lstrlen(language),&lang_latin1,&lang_len);
#endif
	}
	else {
		convert_tchar_to_latin1(indata,lstrlen(indata),&text_mbcs,&text_len);
	}

	m_text_info.is_compressed = is_compressed;
	length=0;

	if(is_compressed) {
#ifdef TWPNG_HAVE_ZLIB
		cmpr_text_len=twpng_compress_data(&cmpr_text,(unsigned char*)text_mbcs,text_len);
#else
		cmpr_text_len=0;
#endif
		if(cmpr_text_len<1 || !cmpr_text) { goto done; }
	}

	// calculate total length of text chunk
	if(ct==CHUNK_iTXt) {
		length = kwlen+1 + 2 + lang_len+1 + trns_kw_len+1;
		if(is_compressed) length += cmpr_text_len;
		else length += text_len;
	}
	else if(ct==CHUNK_zTXt) {
		length = kwlen+1 + 1 + cmpr_text_len;
	}
	else {
		length = kwlen+1 + text_len;
	}

	data=(unsigned char*)malloc(length);
	if(!data) goto done;

	pos=0;

	// Keyword
	memcpy((void*)&data[pos],(void*)kw_latin1,kwlen);
	pos+=kwlen;
	data[pos++] = 0;

	// compression flag
	if(ct==CHUNK_iTXt) {
		data[pos++] = (unsigned char)is_compressed;
	}

	// compression method
	if(ct==CHUNK_zTXt || ct==CHUNK_iTXt) {
		data[pos++] = 0;
	}

	if(ct==CHUNK_iTXt) {
		// language tag
		memcpy((void*)&data[pos],(void*)lang_latin1,lang_len);
		pos+=lang_len;
		data[pos++] = 0;

		// translated keyword
		memcpy((void*)&data[pos],(void*)trns_kw_utf8,trns_kw_len);
		pos+=trns_kw_len;
		data[pos++] = 0;
	}

	if(is_compressed) {
		memcpy((void*)&data[pos],(void*)cmpr_text,cmpr_text_len);
		pos+=cmpr_text_len;
	}
	else {
		memcpy((void*)&data[pos],(void*)text_mbcs,text_len);
		pos+=text_len;
	}
	// assert(pos==length);

	retval=1;
done:
	if(kw_latin1) free(kw_latin1);
	if(text_mbcs) free(text_mbcs);
	if(lang_latin1) free(lang_latin1);
	if(trns_kw_utf8) free(trns_kw_utf8);
	if(cmpr_text) free(cmpr_text);

	get_text_info();

	return retval;
}

// Returns the number of bytes until a NUL char is found.
// Return value doesn't include the NUL.
static int find_word(const char *s, int startpos, int s_len)
{
	int x;
	for(x=startpos;x<s_len;x++) {
		if(s[x]=='\0') return x-startpos;
	}
	return s_len-startpos;
}

static int uncompress_text(struct text_info_struct *ti, unsigned char *cmpr_text,
						   int cmpr_text_len, int is_utf8)
{
#ifdef TWPNG_HAVE_ZLIB
	unsigned char *unc_text;
	int unc_text_size;
	int i;

	unc_text=NULL;
	ti->text_size_in_tchars=0;
	unc_text_size=twpng_uncompress_data(&unc_text, cmpr_text, cmpr_text_len);
	if(unc_text==NULL) return 0;

	if(is_utf8) {
#ifdef UNICODE
		convert_utf8_to_utf16(unc_text,unc_text_size,&ti->text,&ti->text_size_in_tchars);
#endif
	}
	else {

		ti->text = (TCHAR*)malloc(sizeof(TCHAR)*(unc_text_size?unc_text_size:1));
		if(!ti->text) return 0;
		ti->text_size_in_tchars = unc_text_size;
		for(i=0;i<ti->text_size_in_tchars;i++) {
			ti->text[i] = (TCHAR)unc_text[i];
		}
	}
	free(unc_text);
	return 1;
#else
	return 0;
#endif
}


// Parses data from a tEX or zTXt ir iTXt chunk, and
// stores it in the text_info member variable.
// The text will be in uncompressed_data. It is not NULL-terminated.
int Chunk::get_text_info()
{
	int p,i;
	unsigned char cmpr_method=0;
	unsigned char cmpr_flag=0;
	int cvt_len;
	int ret;
	int text_is_utf8=0;
	int len;

	if(m_text_info.processed) return 1;
	m_text_info.processed=1;

	m_text_info.text=NULL;
	m_text_info.text_size_in_tchars=0;
	m_text_info.is_compressed=0;

	p=0;
	len = find_word((const char*)data,p,(int)length);

	// p is now the length of the keyword
	if(len<1 || len>79) {
		return 0;
	}

	convert_latin1_to_tchar((char*)&data[p],len,&m_text_info.keyword,&cvt_len);

	p += len + 1;

	if(m_chunktype_id==CHUNK_zTXt) {
		if(p>(int)length-1) return 0;
		cmpr_method= data[p++];
		m_text_info.is_compressed=1;
	}
	else if(m_chunktype_id==CHUNK_iTXt) {
		text_is_utf8 = 1;
		if(p>(int)length-4) return 0;
		cmpr_flag = data[p++];
		if(cmpr_flag!=0) m_text_info.is_compressed=1;
		cmpr_method = data[p++];

		// language tag
		len = find_word((const char*)data,p,(int)length);
		convert_latin1_to_tchar((char*)&data[p],len,&m_text_info.language,&cvt_len);
		p += len+1;

		// translated keyword
		if(p>(int)length-1) return 0;
		len = find_word((const char*)data,p,(int)length);
#ifdef UNICODE
		ret=convert_utf8_to_utf16(&data[p],len,&m_text_info.translated_keyword,&cvt_len);
		if(!ret) return 0;
#endif
		p += len+1;
	}

	if(p>(int)length) return 0;

	if(m_text_info.is_compressed) {
		if(cmpr_method!=0) {   // unknown compression method: force failure
			return 0;
		}

		ret = uncompress_text(&m_text_info, &data[p], length-p, text_is_utf8);
		if(!ret) {
			// decompression failed, or zlib not available
			m_text_info.text=NULL;
			m_text_info.text_size_in_tchars=0;
			return 1;
		}
	}
	else { // uncompressed text
		if(text_is_utf8) {
#ifdef UNICODE
			convert_utf8_to_utf16(&data[p],length-p,&m_text_info.text,&m_text_info.text_size_in_tchars);
#endif
		}
		else {
			m_text_info.text= (TCHAR*)malloc( sizeof(TCHAR)*(length-p+1) );
			if(!m_text_info.text) {
				mesg(MSG_S,_T("can") SYM_RSQUO _T("t alloc memory"));
				return 0;
			}
			m_text_info.text_size_in_tchars=length-p;

			for(i=0;i<m_text_info.text_size_in_tchars;i++) {
				m_text_info.text[i] = (TCHAR)data[p+i];
			}
		}
	}
	return 1;
}

// how much text will we display in the listbox
#define MAX_TEXT_DISPLAY 150


void Chunk::describe_text(TCHAR *buf, int buflen, int ct)
{
	TCHAR text[MAX_TEXT_DISPLAY+10];
	unsigned int i;
	int flag;
	TCHAR tmpbuf[200];

	if(!get_text_info()) {
		StringCchCopy(buf,buflen,_T("can") SYM_RSQUO _T("t read text chunk"));
		return;
	}

	if(!m_text_info.text) {       // failed

		// Figure out the probable reason that we don't have text available.
		if(ct==CHUNK_iTXt && !globals.unicode_supported) {
			StringCchCopy(text,MAX_TEXT_DISPLAY,_T(" --international text not supported--"));
		}
		else if(m_text_info.is_compressed) {
			if(globals.zlib_available) {
				StringCchCopy(text,MAX_TEXT_DISPLAY,_T(" --decompression failed!--"));
			}
			else {
				StringCchCopy(text,MAX_TEXT_DISPLAY,_T(" --decompression not supported--"));
			}
		}
		else {         // uncompressed text chunk
			StringCchCopy(text,MAX_TEXT_DISPLAY,_T(" --can") SYM_RSQUO _T("t read text--"));
		}
	}
	else {
		if(m_text_info.text_size_in_tchars<=MAX_TEXT_DISPLAY) {
			memcpy(text,m_text_info.text,sizeof(TCHAR)*m_text_info.text_size_in_tchars);
			text[m_text_info.text_size_in_tchars]='\0';
		}
		else {
			memcpy(text,m_text_info.text,sizeof(TCHAR)*MAX_TEXT_DISPLAY);
			text[MAX_TEXT_DISPLAY+0]='.';
			text[MAX_TEXT_DISPLAY+1]='.';
			text[MAX_TEXT_DISPLAY+2]='.';
			text[MAX_TEXT_DISPLAY+3]='\0';
		}
	}

	// clobber bad characters
	// fixme: this should be done in get_text_info, not here.
	for(i=0;m_text_info.keyword[i];i++) {
		if(m_text_info.keyword[i]<32 || (m_text_info.keyword[i]>126 && m_text_info.keyword[i]<161))
			m_text_info.keyword[i]='|';
	}

	for(i=0;text[i];i++) {
		if(text[i]<32 || (text[i]>126 && text[i]<161))
			text[i]='|';
	}

	flag=0;
	for(i=0; known_text_keys[i]; i++) {
		if(!lstrcmp(m_text_info.keyword,known_text_keys[i])) flag=1;
	}

	// text (compressed, international), KW=%s (nonstandard), LANG=%s: ""
	StringCchPrintf(buf,buflen,_T("text"));
	if(m_text_info.is_compressed && ct==CHUNK_iTXt) StringCchCat(buf,buflen,_T(" (international, compressed)"));
	else if(m_text_info.is_compressed) StringCchCat(buf,buflen,_T(" (compressed)"));
	else if(ct==CHUNK_iTXt) StringCchCat(buf,buflen,_T(" (international)"));
	StringCchPrintf(tmpbuf,200,_T(", key=") SYM_LDQUO _T("%s") SYM_RDQUO _T("%s"),m_text_info.keyword,flag?_T(""):_T(" (nonstandard)"));
	StringCchCat(buf,buflen,tmpbuf);
	if(ct==CHUNK_iTXt && m_text_info.language && m_text_info.language[0]) {
		StringCchPrintf(tmpbuf,200,_T(", lang=%s"),m_text_info.language);
		StringCchCat(buf,buflen,tmpbuf);
	}
	StringCchCat(buf,buflen,_T(": ") SYM_LDQUO );
	StringCchCat(buf,buflen,text);
	StringCchCat(buf,buflen,SYM_RDQUO);
}

static const TCHAR *monthname[] = { _T("INVALID-MONTH"),_T("Jan"),_T("Feb"),_T("Mar"),_T("Apr"),_T("May"),
		_T("Jun"),_T("Jul"),_T("Aug"),_T("Sep"),_T("Oct"),_T("Nov"),_T("Dec") };

void Chunk::describe_tIME(TCHAR *buf, int buflen)
{
	int year, month, day, hour, minute, second;

	if(msg_if_invalid_length(buf,buflen,_T("time of last modification"))) return;

	year=   read_int16(&data[0]);
	month=  (int)data[2];
	day=    (int)data[3];
	hour=   (int)data[4];
	minute= (int)data[5];
	second= (int)data[6];

	if(month<1 || month>12) month=0;

	StringCchPrintf(buf,buflen,_T("time of last modification = %d %s %d, %02d:%02d:%02d UTC"),
		day,monthname[month],year,hour,minute,second);
}

void Chunk::get_text_descr(TCHAR *buf, int buflen)
{
	StringCchCopy(buf,buflen,_T(""));

	switch(m_chunktype_id) {
	case CHUNK_IHDR: describe_IHDR(buf,buflen); break;
	case CHUNK_IEND: describe_IEND(buf,buflen); break;
	case CHUNK_bKGD: describe_bKGD(buf,buflen); break;
	case CHUNK_PLTE: describe_PLTE(buf,buflen); break;
	case CHUNK_tRNS: describe_tRNS(buf,buflen); break;
	case CHUNK_sBIT: describe_sBIT(buf,buflen); break;
	case CHUNK_gAMA: describe_gAMA(buf,buflen); break;
	case CHUNK_cHRM: describe_cHRM(buf,buflen); break;
	case CHUNK_pHYs: describe_pHYs(buf,buflen); break;
	case CHUNK_tIME: describe_tIME(buf,buflen); break;
	case CHUNK_hIST: describe_hIST(buf,buflen); break;
	case CHUNK_tEXt: describe_text(buf,buflen,CHUNK_tEXt); break;
	case CHUNK_zTXt: describe_text(buf,buflen,CHUNK_zTXt); break;
	case CHUNK_iTXt: describe_text(buf,buflen,CHUNK_iTXt); break;

	case CHUNK_IDAT: StringCchCopy(buf,buflen,_T("PNG image data")); break;

	case CHUNK_sRGB: describe_sRGB(buf,buflen); break;
	case CHUNK_iCCP: describe_keyword_chunk(buf,buflen,_T("embedded ICC profile")); break; 
	case CHUNK_sPLT: describe_keyword_chunk(buf,buflen,_T("suggested palette")); break;
	case CHUNK_sTER: describe_sTER(buf,buflen); break;

	case CHUNK_oFFs: describe_oFFs(buf,buflen); break;
	case CHUNK_pCAL: describe_keyword_chunk(buf,buflen,_T("calibration of pixel values")); break;
	case CHUNK_sCAL: describe_sCAL(buf,buflen); break;
	case CHUNK_gIFg: StringCchCopy(buf,buflen,_T("GIF graphic control extension")); break;
	case CHUNK_gIFx: StringCchCopy(buf,buflen,_T("GIF application extension")); break;
	case CHUNK_gIFt: StringCchCopy(buf,buflen,_T("GIF plain text extension [DEPRECATED]")); break;
	case CHUNK_fRAc: StringCchCopy(buf,buflen,_T("parameters for fractal image")); break;
	case CHUNK_dSIG: StringCchCopy(buf,buflen,_T("digital signature data")); break;

	case CHUNK_MHDR: describe_MHDR(buf,buflen); break;
	case CHUNK_MEND: StringCchCopy(buf,buflen,_T("end-of-file marker")); break;
	case CHUNK_LOOP: StringCchCopy(buf,buflen,_T("start of loop")); break;
	case CHUNK_ENDL: StringCchCopy(buf,buflen,_T("end of loop")); break;
	case CHUNK_DEFI: StringCchCopy(buf,buflen,_T("define an object")); break;
	case CHUNK_JHDR: describe_JHDR(buf,buflen); break;
	case CHUNK_BASI: StringCchCopy(buf,buflen,_T("PNG chunks")); break;
	case CHUNK_CLON: StringCchCopy(buf,buflen,_T("clone an object")); break;
	case CHUNK_DHDR: StringCchCopy(buf,buflen,_T("Delta-PNG datastream header")); break;
	case CHUNK_PAST: StringCchCopy(buf,buflen,_T("paste an image into another")); break;
	case CHUNK_DISC: StringCchCopy(buf,buflen,_T("discard objects")); break;
	case CHUNK_BACK: StringCchCopy(buf,buflen,_T("background")); break;
	case CHUNK_FRAM: StringCchCopy(buf,buflen,_T("frame definition")); break;
	case CHUNK_MOVE: StringCchCopy(buf,buflen,_T("new image location")); break;
	case CHUNK_CLIP: StringCchCopy(buf,buflen,_T("object clipping boundaries")); break;
	case CHUNK_SHOW: StringCchCopy(buf,buflen,_T("show images")); break;
	case CHUNK_TERM: StringCchCopy(buf,buflen,_T("termination action")); break;
	case CHUNK_SAVE: StringCchCopy(buf,buflen,_T("save information")); break;
	case CHUNK_SEEK: StringCchCopy(buf,buflen,_T("seek point")); break;
	case CHUNK_eXPI: StringCchCopy(buf,buflen,_T("export image")); break;
	case CHUNK_fPRI: StringCchCopy(buf,buflen,_T("frame priority")); break;
	case CHUNK_nEED: StringCchCopy(buf,buflen,_T("resources needed")); break;
	case CHUNK_pHYg: StringCchCopy(buf,buflen,_T("physical pixel size (global)")); break;
	case CHUNK_JDAT: StringCchCopy(buf,buflen,_T("JNG image data")); break;
	case CHUNK_JSEP: StringCchCopy(buf,buflen,_T("8-bit/12-bit image separator")); break;
	case CHUNK_PROM: StringCchCopy(buf,buflen,_T("promotion of parent object")); break;
	case CHUNK_IPNG: StringCchCopy(buf,buflen,_T("incomplete PNG")); break;
	case CHUNK_PPLT: StringCchCopy(buf,buflen,_T("partial palette")); break;
	case CHUNK_IJNG: StringCchCopy(buf,buflen,_T("incomplete JNG")); break;
	case CHUNK_DROP: StringCchCopy(buf,buflen,_T("drop chunks")); break;
	case CHUNK_DBYK: StringCchCopy(buf,buflen,_T("drop chunks by keyword")); break;
	case CHUNK_ORDR: StringCchCopy(buf,buflen,_T("ordering restrictions")); break;

	case CHUNK_acTL: describe_acTL(buf,buflen); break;
	case CHUNK_fcTL: describe_fcTL(buf,buflen); break;
	case CHUNK_fdAT: describe_fdAT(buf,buflen); break;
	case CHUNK_CgBI: StringCchCopy(buf,buflen,_T("iPhone PNG-like file header")); break;
	case CHUNK_vpAg: describe_vpAg(buf,buflen); break;

	default:
		if(!is_public()) {
			StringCchCopy(buf,buflen,_T("private chunk"));
			break;
		}

		StringCchCopy(buf,buflen,_T("unrecognized chunk type"));
		break;
	}
}


void Chunk::get_text_descr_generic(TCHAR *buf, int buflen)
{
	StringCchCopy(buf,buflen,_T(""));
	if(is_critical()) StringCchCat(buf,buflen,_T("critical"));
	else StringCchCat(buf,buflen,_T("ancillary"));

	if(!is_public()) {
		StringCchCat(buf,buflen,_T(", private"));
	}

	if(!is_critical()) {  // safe-to-copy is not used for critical chunks
		if(is_safe_to_copy()) StringCchCat(buf,buflen,_T(", safe to copy"));
		else StringCchCat(buf,buflen,_T(", unsafe to copy"));
	}
}

int Chunk::get_keyword_info(struct keyword_info_struct *kw)
{
	int i;

	i=0;
	kw->keyword[0] = '\0';
	kw->keyword_len = 0;

	while(1) {
		if(i>=(int)length) {
			// No NUL separator exists
			kw->keyword[0] = '\0';
			kw->keyword_len = 0;
			return 0;
		}

		if(data[i]==0) break; // NUL separator found

		// keyword_len is the actual length in the chunk, not necessarily
		// the length in the ->keyword buffer.
		kw->keyword_len = i+1;

		if(i<=78) {
			kw->keyword[i] = (TCHAR)data[i];
			kw->keyword[i+1] = '\0';
		}

		i++;
	}
	return 1;
}

// Several chunk types start with a null-terminated keyword. In lieu of
// fully handling them, we'll at least display that keyword.
void Chunk::describe_keyword_chunk(TCHAR *buf, int buflen, const TCHAR *prefix)
{
	struct keyword_info_struct kw;

	get_keyword_info(&kw);

	if(kw.keyword_len>=1 && kw.keyword_len<=79) {
		StringCchPrintf(buf,buflen,_T("%s: [%s]"),prefix,kw.keyword);
	}
	else {
		StringCchPrintf(buf,buflen,_T("%s, invalid name length"),prefix);
	}
}

// if exp is nonzero, does not write the length or crc
int Chunk::write_to_file(HANDLE fh, int exp)
{
	unsigned char buf[8];
	DWORD written;

	if(!exp) {
		// write length
		write_int32(&buf[0],length);
		WriteFile(fh,(LPVOID)buf,4,&written,NULL);
		if(written!=4) return 0;
	}

	// write type
	memcpy(&buf[0],m_chunktype_ascii,4);
	WriteFile(fh,(LPVOID)buf,4,&written,NULL);
	if(written!=4) return 0;

	// write data
	if(length>0) {
		WriteFile(fh,(LPVOID)data,length,&written,NULL);
		if(written!=length) return 0;
	}

	if(!exp) {
		// write crc
		write_int32(&buf[0],m_crc);
		WriteFile(fh,(LPVOID)buf,4,&written,NULL);
		if(written!=4) return 0;
	}

	return 1;
}

// Copy part or all of the chunk into a memory block.
// This includes the length/type/crc.
// Copies up to buflen bytes. Returns the number of bytes copied into buf.
DWORD Chunk::copy_segment_to_memory(unsigned char *buf, DWORD offset, DWORD buflen)
{
	DWORD pos_in_buf;
	DWORD pos_in_chunk;

	for(pos_in_buf=0;pos_in_buf<buflen;pos_in_buf++) {
		pos_in_chunk = offset+pos_in_buf;
		if(pos_in_chunk>=length+12) {
			return pos_in_buf; // reached end of chunk
		}

		if(pos_in_chunk<8) {
			// writing the length or type
			switch(pos_in_chunk) {
			case 0: buf[pos_in_buf]= (unsigned char) ((length & 0xff000000)>>24); break;
			case 1: buf[pos_in_buf]= (unsigned char) ((length & 0x00ff0000)>>16); break;
			case 2: buf[pos_in_buf]= (unsigned char) ((length & 0x0000ff00)>> 8); break;
			case 3: buf[pos_in_buf]= (unsigned char) ( length & 0x000000ff     ); break;
			case 4: buf[pos_in_buf] = m_chunktype_ascii[0]; break;
			case 5: buf[pos_in_buf] = m_chunktype_ascii[1]; break;
			case 6: buf[pos_in_buf] = m_chunktype_ascii[2]; break;
			case 7: buf[pos_in_buf] = m_chunktype_ascii[3]; break;
			}
		}
		else if(pos_in_chunk >= (length+8)) {
			// writing the crc
			if(     pos_in_chunk==length+8 ) buf[pos_in_buf]= (unsigned char) ((m_crc & 0xff000000)>>24);
			else if(pos_in_chunk==length+9 ) buf[pos_in_buf]= (unsigned char) ((m_crc & 0x00ff0000)>>16);
			else if(pos_in_chunk==length+10) buf[pos_in_buf]= (unsigned char) ((m_crc & 0x0000ff00)>> 8); 
			else if(pos_in_chunk==length+11) buf[pos_in_buf]= (unsigned char) ( m_crc & 0x000000ff     );
		}
		else {
			// writing the data
			buf[pos_in_buf] = data[pos_in_chunk-8];
		}
	}
	return pos_in_buf;
}

Chunk::Chunk()
{
	data=NULL;

	m_text_info.processed=0;
	m_text_info.is_compressed=0;
	m_text_info.text=NULL;
	m_text_info.keyword=NULL;
	m_text_info.language=NULL;
	m_text_info.translated_keyword=NULL;
}

Chunk::~Chunk()
{
	if(data) free(data);
	free_text_info();
}

void Chunk::free_text_info()
{
	if(m_text_info.text) free(m_text_info.text);
	if(m_text_info.keyword) free(m_text_info.keyword);
	if(m_text_info.language) free(m_text_info.language);
	if(m_text_info.translated_keyword) free(m_text_info.translated_keyword);
	m_text_info.processed=0;
	m_text_info.is_compressed=0;
	m_text_info.text=NULL;
	m_text_info.text_size_in_tchars=0;
	m_text_info.keyword=NULL;
	m_text_info.language=NULL;
	m_text_info.translated_keyword=NULL;
}

// call this after setting the data for the chunk
// it may do some other initialization here..
void Chunk::after_init()
{
	// set type id for convenience when testing chunk types
	m_chunktype_id=get_chunk_type_id();

	if(m_chunktype_id == CHUNK_IHDR) {
		if(length>=13) {
			m_parentpng->m_width=read_int32(&data[0]);
			m_parentpng->m_height=read_int32(&data[4]);
			m_parentpng->m_bitdepth=data[8];
			m_parentpng->m_colortype=data[9];
			//m_parentpng->m_compression=data[10];
			//m_parentpng->m_compressionfilter=data[11];
			//m_parentpng->m_interlace=data[12];
		}
	}
}


// a useful function that Microsoft seems to have left out of the API
void GetPosInParent(HWND hwnd,RECT *rc)
{
	POINT p;

	p.x= p.y= 0;
	ClientToScreen(GetParent(hwnd),&p);
	GetWindowRect(hwnd,rc);
	rc->left -= p.x;
	rc->right -= p.x;
	rc->top -= p.y;
	rc->bottom -= p.y;
}

static int Dlg_tEXt_InitDialog(HWND hwnd, Chunk *ch, struct textdlgmetrics *tdm)
{
	int i;
	int rv;
	TCHAR *cooked_data;
	RECT rd,r1;

	SendMessage(hwnd,WM_SETICON,ICON_BIG,(LPARAM)LoadIcon(globals.hInst,_T("ICONMAIN")));

#ifndef TWPNG_HAVE_ZLIB
	EnableWindow(GetDlgItem(hwnd,IDC_TEXTCOMPRESSED),FALSE);
#endif
#ifndef UNICODE
	EnableWindow(GetDlgItem(hwnd,IDC_TEXTUNICODE),FALSE);
#endif

	for(i=0;known_text_keys[i];i++) {
		SendDlgItemMessage(hwnd,IDC_TEXTKEYWORD,CB_ADDSTRING,0,
		     (LPARAM)known_text_keys[i]);
	}
	SendDlgItemMessage(hwnd,IDC_TEXTKEYWORD,CB_LIMITTEXT,79,0);

	rv=ch->get_text_info();
	if(rv) {
		SetDlgItemText(hwnd,IDC_TEXTKEYWORD,ch->m_text_info.keyword);
		if(ch->m_text_info.language)
			SetDlgItemText(hwnd,IDC_TEXTLANGUAGE,ch->m_text_info.language);
		if(ch->m_text_info.translated_keyword)
			SetDlgItemText(hwnd,IDC_TEXTTRNSLTKEYWORD,ch->m_text_info.translated_keyword);

		if(ch->m_text_info.text) {
			lf2crlf(&cooked_data,ch->m_text_info.text,ch->m_text_info.text_size_in_tchars);
			if(cooked_data) {
				SetDlgItemText(hwnd,IDC_TEXTTEXT,(TCHAR*)cooked_data);
				free(cooked_data);
			}
		}

	}
	CheckDlgButton(hwnd,IDC_TEXTCOMPRESSED,
		(ch->m_text_info.is_compressed)?BST_CHECKED:BST_UNCHECKED);

	if(ch->m_chunktype_id==CHUNK_iTXt) {
		CheckDlgButton(hwnd,IDC_TEXTUNICODE,BST_CHECKED);
	}
	else {
		ShowWindow(GetDlgItem(hwnd,IDC_TEXTLABELLANG),SW_HIDE);
		ShowWindow(GetDlgItem(hwnd,IDC_TEXTLABELTKW),SW_HIDE);
		ShowWindow(GetDlgItem(hwnd,IDC_TEXTLANGUAGE),SW_HIDE);
		ShowWindow(GetDlgItem(hwnd,IDC_TEXTTRNSLTKEYWORD),SW_HIDE);
	}

	// save some control position metrics for later resizing
	GetClientRect(hwnd,&rd);
	GetPosInParent(GetDlgItem(hwnd,IDOK),&r1);

	tdm->border_buttonoffset = (rd.right-rd.left)-r1.left;
	tdm->border_btn1y = r1.top;

	GetPosInParent(GetDlgItem(hwnd,IDCANCEL),&r1);
	tdm->border_btn2y = r1.top;

	GetPosInParent(GetDlgItem(hwnd,IDC_TEXTTEXT),&r1);
	tdm->border_editx = (rd.right-rd.left)-r1.right;
	tdm->border_edity = (rd.bottom-rd.top)-r1.bottom;

	twpng_SetWindowPos(hwnd,&globals.window_prefs.text);
	if(globals.window_prefs.text.max) ShowWindow(hwnd,SW_SHOWMAXIMIZED);

	return 1;
}

// Take the data in the dialog items, and create a PNG text chunk from it.
static void Dlg_tEXt_OK(HWND hwnd, Chunk *ch)
{
	HWND h;
	int len;
	TCHAR *tmptext;
	TCHAR keyword[80];
	TCHAR language[80];
	TCHAR trnsl_keyword[80]; // fixme: how long does this need to be?
	int is_international;
	int is_cmpr;

	is_cmpr = (IsDlgButtonChecked(hwnd,IDC_TEXTCOMPRESSED)==BST_CHECKED);

	is_international = (IsDlgButtonChecked(hwnd,IDC_TEXTUNICODE)==BST_CHECKED);

	h=GetDlgItem(hwnd,IDC_TEXTTEXT);
	len=GetWindowTextLength(h);
	tmptext=(TCHAR*)malloc((sizeof(TCHAR))*(len+1));
	GetWindowText(h,tmptext,len+1);

	crlf2lf(tmptext);

	GetDlgItemText(hwnd,IDC_TEXTKEYWORD,keyword,80);
	keyword[79]='\0';

	language[0]='\0';
	trnsl_keyword[0]='\0';
	if(is_international) {
		// fixme: set a max length for these items
		GetDlgItemText(hwnd,IDC_TEXTLANGUAGE,language,80);
		language[79]='\0';
		GetDlgItemText(hwnd,IDC_TEXTTRNSLTKEYWORD,trnsl_keyword,80);
		trnsl_keyword[79]='\0';
	}

	if(is_international) {
		ch->set_text_info(keyword,language,trnsl_keyword,tmptext,is_cmpr,1);
	}
	else {
		ch->set_text_info(keyword,NULL,NULL,tmptext,is_cmpr,0);
	}
	free(tmptext);
}

// The text editor is so different from the other chunk editors that I'll
// keep it in a separate function from the rest.
static INT_PTR CALLBACK DlgProcEdit_tEXt(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	Chunk *ch = NULL;

	RECT rd,r1;
	struct edit_chunk_ctx *ecctx = NULL;

	id=LOWORD(wParam);

	if(msg==WM_INITDIALOG) {
		ecctx = (struct edit_chunk_ctx*)lParam;
		if(!ecctx) return 1;
		SetWindowLongPtr(hwnd,DWLP_USER,lParam);
		return Dlg_tEXt_InitDialog(hwnd,ecctx->ch,&ecctx->tdm);
	}
	else {
		ecctx = (struct edit_chunk_ctx*)GetWindowLongPtr(hwnd,DWLP_USER);
		if(!ecctx) return 0;
		ch = ecctx->ch;
	}

	switch (msg) {
	case WM_GETMINMAXINFO:
		{
			LPMINMAXINFO mm;
			mm=(LPMINMAXINFO)lParam;
			mm->ptMinTrackSize.x=370;
			mm->ptMinTrackSize.y=190;
			return 1;
		}

	case WM_SIZE:
		{
			HWND h;

			GetClientRect(hwnd,&rd);

			SetWindowPos(GetDlgItem(hwnd,IDOK),NULL,rd.right-ecctx->tdm.border_buttonoffset,ecctx->tdm.border_btn1y,0,0,SWP_NOSIZE|SWP_NOZORDER);
			SetWindowPos(GetDlgItem(hwnd,IDCANCEL),NULL,rd.right-ecctx->tdm.border_buttonoffset,ecctx->tdm.border_btn2y,0,0,SWP_NOSIZE|SWP_NOZORDER);

			h=GetDlgItem(hwnd,IDC_TEXTTEXT);
			GetPosInParent(h,&r1);
			SetWindowPos(h,NULL,0,0,
				(rd.right-rd.left)-r1.left-ecctx->tdm.border_editx,
				(rd.bottom-rd.top)-r1.top-ecctx->tdm.border_edity,
				SWP_NOMOVE|SWP_NOZORDER);

		}
		return 1;

	case WM_DESTROY:
		twpng_StoreWindowPos(hwnd,&globals.window_prefs.text);
		break;

	case WM_COMMAND:
		switch(id) {
		case IDC_TEXTUNICODE:
			if(HIWORD(wParam)==BN_CLICKED) {
				int n;
				n= (BST_CHECKED==IsDlgButtonChecked(hwnd,IDC_TEXTUNICODE));
				ShowWindow(GetDlgItem(hwnd,IDC_TEXTLABELLANG),n?SW_SHOW:SW_HIDE);
				ShowWindow(GetDlgItem(hwnd,IDC_TEXTLABELTKW),n?SW_SHOW:SW_HIDE);
				ShowWindow(GetDlgItem(hwnd,IDC_TEXTLANGUAGE),n?SW_SHOW:SW_HIDE);
				ShowWindow(GetDlgItem(hwnd,IDC_TEXTTRNSLTKEYWORD),n?SW_SHOW:SW_HIDE);
			}
			return 1;

		case IDOK:
			Dlg_tEXt_OK(hwnd,ch);
			EndDialog(hwnd,1);
			return 1;

		case IDCANCEL:
			EndDialog(hwnd, 0);
			return 1;
		}
	}
	return 0;
}

// sets tIME dialog fields to the current system time
static void set_to_current_time(HWND hwnd)
{
	SYSTEMTIME st;
	GetSystemTime(&st);
	TCHAR buf[20];

	SetDlgItemInt(hwnd,IDC_YEAR  ,(int)st.wYear,FALSE);
	SetDlgItemInt(hwnd,IDC_DAY   ,(int)st.wDay,FALSE);
	StringCchPrintf(buf,20,_T("%02d"),(int)st.wHour);   SetDlgItemText(hwnd,IDC_HOUR  ,buf);
	StringCchPrintf(buf,20,_T("%02d"),(int)st.wMinute); SetDlgItemText(hwnd,IDC_MINUTE,buf);
	StringCchPrintf(buf,20,_T("%02d"),(int)st.wSecond); SetDlgItemText(hwnd,IDC_SECOND,buf);
	SendDlgItemMessage(hwnd,IDC_MONTH,CB_SETCURSEL,(WPARAM)((int)st.wMonth-1),0);
}

static void phys_calc_inches(HWND hwnd)
{
	int x, y;
	double xinches, yinches;
	TCHAR buf[200];
	//radio1=meters. If radio1 is checked, show in inches

	x=(IsDlgButtonChecked(hwnd,IDC_RADIO1)==BST_CHECKED)?1:0;
	if(!x) {
		SetDlgItemText(hwnd,IDC_ININCHES,_T(""));
		return;
	}

	x=GetDlgItemInt(hwnd,IDC_EDIT1,NULL,FALSE);
	y=GetDlgItemInt(hwnd,IDC_EDIT2,NULL,FALSE);

	xinches = ((double)x)*0.0254;
	yinches = ((double)y)*0.0254;

	StringCchPrintf(buf,200,_T("= %.2f %s %.2f pixels/inch"),xinches,SYM_TIMES,yinches);
	SetDlgItemText(hwnd,IDC_ININCHES,buf);
}

// Disable the "num plays" editbox if "play indefinitely" is checked.
static void acTL_EnableDisable(HWND hwnd)
{
	int n;
	n=(IsDlgButtonChecked(hwnd,IDC_CHECK1)==BST_CHECKED)?1:0;
	EnableWindow(GetDlgItem(hwnd,IDC_EDIT2),n?0:1);
}

void Chunk::init_sCAL_dlg(HWND hwnd)
{
	struct sCAL_data d;
	get_sCAL_data(&d);
	CheckDlgButton(hwnd,IDC_RADIO0,(d.units==1)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(hwnd,IDC_RADIO1,(d.units==2)?BST_CHECKED:BST_UNCHECKED);
	SetDlgItemText(hwnd,IDC_EDIT1,d.x);
	SetDlgItemText(hwnd,IDC_EDIT2,d.y);
}

void Chunk::process_sCAL_dlg(HWND hwnd)
{
	struct sCAL_data d;
	d.units=0;
	if(IsDlgButtonChecked(hwnd,IDC_RADIO0)==BST_CHECKED) d.units=1;
	if(IsDlgButtonChecked(hwnd,IDC_RADIO1)==BST_CHECKED) d.units=2;
	GetDlgItemText(hwnd,IDC_EDIT1,d.x,100);
	GetDlgItemText(hwnd,IDC_EDIT2,d.y,100);
	set_sCAL_data(&d);
}

// A big ugly function to handle messages for nearly all chunk edit
// dialog boxes. But the alternative of separate functions for each chunk
// type is even uglier IMHO.
INT_PTR CALLBACK Chunk::DlgProcEditChunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	struct edit_chunk_ctx *p = NULL;
	Chunk *ch=NULL;
	WORD id,code;
	double tmpdbl;
	int tmpint1,tmpint2,i;
	TCHAR buf[500];

	if(msg==WM_INITDIALOG) {
		p = (struct edit_chunk_ctx*)lParam;
		if(!p) return 1;
		SetWindowLongPtr(hwnd,DWLP_USER,lParam);
		ch = p->ch;

		switch(ch->m_chunktype_id) {
		case CHUNK_IHDR:
			SendDlgItemMessage(hwnd,IDC_COLORTYPE,CB_ADDSTRING,0,(LPARAM)_T("0 = grayscale"));
			SendDlgItemMessage(hwnd,IDC_COLORTYPE,CB_ADDSTRING,0,(LPARAM)_T("2 = RGB"));
			SendDlgItemMessage(hwnd,IDC_COLORTYPE,CB_ADDSTRING,0,(LPARAM)_T("3 = paletted"));
			SendDlgItemMessage(hwnd,IDC_COLORTYPE,CB_ADDSTRING,0,(LPARAM)_T("4 = grayscale+alpha"));
			SendDlgItemMessage(hwnd,IDC_COLORTYPE,CB_ADDSTRING,0,(LPARAM)_T("6 = RGB+alpha"));
			SetDlgItemInt(hwnd,IDC_EDIT1,read_int32(&ch->data[0]),FALSE);
			SetDlgItemInt(hwnd,IDC_EDIT2,read_int32(&ch->data[4]),FALSE);
			SetDlgItemInt(hwnd,IDC_EDIT3,(int)(ch->data[8]),FALSE); // bit depth
			CheckDlgButton(hwnd,IDC_CHECK1,(ch->data[12]>0)?BST_CHECKED:BST_UNCHECKED); // interlaced
			tmpint1= (int)(ch->data[9]);
			switch(tmpint1) {
			case 2: tmpint2=1; break;
			case 3: tmpint2=2; break;
			case 4: tmpint2=3; break;
			case 6: tmpint2=4; break;
			default: tmpint2=0;
			}
			SendDlgItemMessage(hwnd,IDC_COLORTYPE,CB_SETCURSEL,(WPARAM)tmpint2,0);
			break;

		case CHUNK_gAMA:
			SendDlgItemMessage(hwnd,IDC_GAMMA,CB_ADDSTRING,0,(LPARAM)_T("0.40000"));
			SendDlgItemMessage(hwnd,IDC_GAMMA,CB_ADDSTRING,0,(LPARAM)_T("0.45000"));
			SendDlgItemMessage(hwnd,IDC_GAMMA,CB_ADDSTRING,0,(LPARAM)_T("0.45455"));
			SendDlgItemMessage(hwnd,IDC_GAMMA,CB_ADDSTRING,0,(LPARAM)_T("0.50000"));
			SendDlgItemMessage(hwnd,IDC_GAMMA,CB_ADDSTRING,0,(LPARAM)_T("0.55000"));
			SendDlgItemMessage(hwnd,IDC_GAMMA,CB_ADDSTRING,0,(LPARAM)_T("0.60000"));
			SendDlgItemMessage(hwnd,IDC_GAMMA,CB_ADDSTRING,0,(LPARAM)_T("0.66667"));
			SendDlgItemMessage(hwnd,IDC_GAMMA,CB_ADDSTRING,0,(LPARAM)_T("0.70000"));
			SendDlgItemMessage(hwnd,IDC_GAMMA,CB_ADDSTRING,0,(LPARAM)_T("1.00000"));

			StringCchPrintf(buf,500,_T("%0.5f"),((double)read_int32(ch->data))/100000.0);
			SetDlgItemText(hwnd,IDC_GAMMA,buf);
			break;
		case CHUNK_sRGB:
			CheckDlgButton(hwnd,IDC_SRGB_0,(ch->data[0]==0)?BST_CHECKED:BST_UNCHECKED);
			CheckDlgButton(hwnd,IDC_SRGB_1,(ch->data[0]==1)?BST_CHECKED:BST_UNCHECKED);
			CheckDlgButton(hwnd,IDC_SRGB_2,(ch->data[0]==2)?BST_CHECKED:BST_UNCHECKED);
			CheckDlgButton(hwnd,IDC_SRGB_3,(ch->data[0]==3)?BST_CHECKED:BST_UNCHECKED);
			break;

		case CHUNK_sTER:
			CheckDlgButton(hwnd,IDC_STER_0,(ch->data[0]==0)?BST_CHECKED:BST_UNCHECKED);
			CheckDlgButton(hwnd,IDC_STER_1,(ch->data[0]==1)?BST_CHECKED:BST_UNCHECKED);
			break;

		case CHUNK_pHYs:
			SetDlgItemInt(hwnd,IDC_EDIT1,read_int32(&ch->data[0]),FALSE);
			SetDlgItemInt(hwnd,IDC_EDIT2,read_int32(&ch->data[4]),FALSE);
			CheckDlgButton(hwnd,IDC_RADIO0,(ch->data[8]==0)?BST_CHECKED:BST_UNCHECKED);
			CheckDlgButton(hwnd,IDC_RADIO1,(ch->data[8]==1)?BST_CHECKED:BST_UNCHECKED);
			phys_calc_inches(hwnd);
			break;

		case CHUNK_cHRM:
			StringCchPrintf(buf,500,_T("%0.5f"),((double)read_int32(&ch->data[ 0]))/100000.0); SetDlgItemText(hwnd,IDC_EDIT1,buf);
			StringCchPrintf(buf,500,_T("%0.5f"),((double)read_int32(&ch->data[ 4]))/100000.0); SetDlgItemText(hwnd,IDC_EDIT2,buf);
			StringCchPrintf(buf,500,_T("%0.5f"),((double)read_int32(&ch->data[ 8]))/100000.0); SetDlgItemText(hwnd,IDC_EDIT3,buf);
			StringCchPrintf(buf,500,_T("%0.5f"),((double)read_int32(&ch->data[12]))/100000.0); SetDlgItemText(hwnd,IDC_EDIT4,buf);
			StringCchPrintf(buf,500,_T("%0.5f"),((double)read_int32(&ch->data[16]))/100000.0); SetDlgItemText(hwnd,IDC_EDIT5,buf);
			StringCchPrintf(buf,500,_T("%0.5f"),((double)read_int32(&ch->data[20]))/100000.0); SetDlgItemText(hwnd,IDC_EDIT6,buf);
			StringCchPrintf(buf,500,_T("%0.5f"),((double)read_int32(&ch->data[24]))/100000.0); SetDlgItemText(hwnd,IDC_EDIT7,buf);
			StringCchPrintf(buf,500,_T("%0.5f"),((double)read_int32(&ch->data[28]))/100000.0); SetDlgItemText(hwnd,IDC_EDIT8,buf);
			break;

		case CHUNK_bKGD:
			SetWindowText(hwnd,_T("Background color"));
			tmpint1= (int)ch->m_parentpng->m_bitdepth;

			switch(ch->m_parentpng->m_colortype) {
			case 3:
				StringCchPrintf(buf,500,_T("Enter palette index (0") SYM_ENDASH _T("%d)"),(1<<tmpint1)-1);
				SetDlgItemText(hwnd,IDC_LABEL1,buf);
				SetDlgItemInt(hwnd,IDC_EDIT1,(int)(ch->data[0]),FALSE);
				break;
			case 0: case 4:
				StringCchPrintf(buf,500,_T("Enter background gray shade (0") SYM_ENDASH _T("%d)"),(1<<tmpint1)-1);
				SetDlgItemText(hwnd,IDC_LABEL1,buf);
				SetDlgItemInt(hwnd,IDC_EDIT1,read_int16(&ch->data[0]),FALSE);
				break;
			case 2: case 6:
				StringCchPrintf(buf,500,_T("Enter background RGB values (0") SYM_ENDASH _T("%d)"),(1<<tmpint1)-1);
				SetDlgItemText(hwnd,IDC_LABEL1,buf);
				SetDlgItemInt(hwnd,IDC_EDIT1,read_int16(&ch->data[0]),FALSE);
				SetDlgItemInt(hwnd,IDC_EDIT2,read_int16(&ch->data[2]),FALSE);
				SetDlgItemInt(hwnd,IDC_EDIT3,read_int16(&ch->data[4]),FALSE);
				break;
			}
			break;

		case CHUNK_sBIT:
			SetWindowText(hwnd,_T("Significant bits"));
			tmpint1= (ch->m_parentpng->m_colortype==3)?8:(int)ch->m_parentpng->m_bitdepth;
			StringCchPrintf(buf,500,_T("Enter significant bits (1") SYM_ENDASH _T("%d)"),tmpint1);
			SetDlgItemText(hwnd,IDC_LABEL1,buf);

			if(ch->length>=1)
				SetDlgItemInt(hwnd,IDC_EDIT1,(int)(ch->data[0]),FALSE);
			if(ch->length>=2)
				SetDlgItemInt(hwnd,IDC_EDIT2,(int)(ch->data[1]),FALSE);
			if(ch->length>=3)
				SetDlgItemInt(hwnd,IDC_EDIT3,(int)(ch->data[2]),FALSE);
			if(ch->length>=4)
				SetDlgItemInt(hwnd,IDC_EDIT4,(int)(ch->data[3]),FALSE);

			break;

		case CHUNK_tRNS:
			SetWindowText(hwnd,_T("Transparent color"));
			tmpint1= (int)ch->m_parentpng->m_bitdepth;

			switch(ch->m_parentpng->m_colortype) {
			case 0: case 4:
				StringCchPrintf(buf,500,_T("Enter transparent gray shade (0") SYM_ENDASH _T("%d)"),(1<<tmpint1)-1);
				SetDlgItemText(hwnd,IDC_LABEL1,buf);
				SetDlgItemInt(hwnd,IDC_EDIT1,read_int16(&ch->data[0]),FALSE);
				break;
			case 2: case 6:
				StringCchPrintf(buf,500,_T("Enter transparent RGB color (0") SYM_ENDASH _T("%d)"),(1<<tmpint1)-1);
				SetDlgItemText(hwnd,IDC_LABEL1,buf);
				SetDlgItemInt(hwnd,IDC_EDIT1,read_int16(&ch->data[0]),FALSE);
				SetDlgItemInt(hwnd,IDC_EDIT2,read_int16(&ch->data[2]),FALSE);
				SetDlgItemInt(hwnd,IDC_EDIT3,read_int16(&ch->data[4]),FALSE);
				break;
			}
			break;


		case CHUNK_tIME:
			SetDlgItemInt(hwnd,IDC_YEAR,read_int16(&ch->data[0]),FALSE);
			SetDlgItemInt(hwnd,IDC_DAY   ,(int)ch->data[3],FALSE);
			StringCchPrintf(buf,500,_T("%02d"),(int)ch->data[4]); SetDlgItemText(hwnd,IDC_HOUR  ,buf);
			StringCchPrintf(buf,500,_T("%02d"),(int)ch->data[5]); SetDlgItemText(hwnd,IDC_MINUTE,buf);
			StringCchPrintf(buf,500,_T("%02d"),(int)ch->data[6]); SetDlgItemText(hwnd,IDC_SECOND,buf);

			for(i=1;i<=12;i++) {
				SendDlgItemMessage(hwnd,IDC_MONTH,CB_ADDSTRING,0,(LPARAM)monthname[i]);
			}
			SendDlgItemMessage(hwnd,IDC_MONTH,CB_SETCURSEL,(WPARAM)((int)ch->data[2]-1),0);
			//set_to_current_time(hwnd);

			break;

		case CHUNK_acTL:
			SetDlgItemInt(hwnd,IDC_EDIT1,read_int32(&ch->data[0]),FALSE); // # frames
			tmpint1 = read_int32(&ch->data[4]);
			SetDlgItemInt(hwnd,IDC_EDIT2,tmpint1,FALSE); // # plays
			CheckDlgButton(hwnd,IDC_CHECK1,(tmpint1==0)?BST_CHECKED:BST_UNCHECKED);
			acTL_EnableDisable(hwnd);
			break;

		case CHUNK_fcTL:
			SendDlgItemMessage(hwnd,IDC_DISPOSE,CB_ADDSTRING,0,(LPARAM)_T("0 = None"));
			SendDlgItemMessage(hwnd,IDC_DISPOSE,CB_ADDSTRING,0,(LPARAM)_T("1 = Background"));
			SendDlgItemMessage(hwnd,IDC_DISPOSE,CB_ADDSTRING,0,(LPARAM)_T("2 = Previous"));
			SendDlgItemMessage(hwnd,IDC_BLEND,CB_ADDSTRING,0,(LPARAM)_T("0 = Source"));
			SendDlgItemMessage(hwnd,IDC_BLEND,CB_ADDSTRING,0,(LPARAM)_T("1 = Over"));

			tmpint1 = (int)ch->data[24];
			if(tmpint1<0 || tmpint1>2) tmpint1=0;
			SendDlgItemMessage(hwnd,IDC_DISPOSE,CB_SETCURSEL,(WPARAM)tmpint1,0);
			tmpint1 = (int)ch->data[25];
			if(tmpint1<0 || tmpint1>1) tmpint1=0;
			SendDlgItemMessage(hwnd,IDC_BLEND,CB_SETCURSEL,(WPARAM)tmpint1,0);

			SetDlgItemInt(hwnd,IDC_EDIT1,read_int32(&ch->data[0]),FALSE); // sequence #
			SetDlgItemInt(hwnd,IDC_EDIT2,read_int32(&ch->data[4]),FALSE); // width
			SetDlgItemInt(hwnd,IDC_EDIT3,read_int32(&ch->data[8]),FALSE); // height
			SetDlgItemInt(hwnd,IDC_EDIT4,read_int32(&ch->data[12]),FALSE); // x offset
			SetDlgItemInt(hwnd,IDC_EDIT5,read_int32(&ch->data[16]),FALSE); // y offset
			SetDlgItemInt(hwnd,IDC_EDIT6,read_int16(&ch->data[20]),FALSE); // delay numerator
			SetDlgItemInt(hwnd,IDC_EDIT7,read_int16(&ch->data[22]),FALSE); // delay denominator
			// dispose op
			// blend op
			break;

		case CHUNK_fdAT:
			SetWindowText(hwnd,_T("APNG frame data"));
			SetDlgItemText(hwnd,IDC_LABEL1,_T("Sequence number"));
			SetDlgItemInt(hwnd,IDC_EDIT1,read_int32(&ch->data[0]),FALSE);
			break;

		case CHUNK_oFFs:
			SetDlgItemInt(hwnd,IDC_EDIT1,read_int32(&ch->data[0]),TRUE);
			SetDlgItemInt(hwnd,IDC_EDIT2,read_int32(&ch->data[4]),TRUE);
			CheckDlgButton(hwnd,IDC_RADIO0,(ch->data[8]==0)?BST_CHECKED:BST_UNCHECKED);
			CheckDlgButton(hwnd,IDC_RADIO1,(ch->data[8]==1)?BST_CHECKED:BST_UNCHECKED);
			break;

		case CHUNK_sCAL:
			ch->init_sCAL_dlg(hwnd);
			break;

		case CHUNK_iCCP:
			ch->init_iCCP_dlg(p,hwnd);
			break;

		case CHUNK_vpAg:
			SetDlgItemInt(hwnd,IDC_EDIT1,read_int32(&ch->data[0]),TRUE);
			SetDlgItemInt(hwnd,IDC_EDIT2,read_int32(&ch->data[4]),TRUE);
			SetDlgItemInt(hwnd,IDC_EDIT3,(UINT)ch->data[8],FALSE);
			break;
		}
		return 1;
	}
	else {
		p = (struct edit_chunk_ctx*)GetWindowLongPtr(hwnd,DWLP_USER);
		if(!p) return FALSE;
		ch = p->ch;
	}

	id=LOWORD(wParam);
	code=HIWORD(wParam);

	switch (msg) {
	case WM_DESTROY:
		ch=NULL;
		break;
		
	case WM_SIZE:
		if(ch->m_chunktype_id==CHUNK_iCCP) {
			ch->size_iCCP_dlg(p,hwnd);
			return 1;
		}
		return 0;

	case WM_GETMINMAXINFO:
		if(ch->m_chunktype_id==CHUNK_iCCP) {
			LPMINMAXINFO mm;
			mm=(LPMINMAXINFO)lParam;
			mm->ptMinTrackSize.x=440;
			mm->ptMinTrackSize.y=190;
			return 1;
		}
		return 0;

	case WM_COMMAND:
		if(!ch) return 1;

		switch(id) {

		case IDC_EDIT1:
		case IDC_EDIT2:
			if(code==EN_CHANGE && ch->m_chunktype_id==CHUNK_pHYs) {
				phys_calc_inches(hwnd);
			}
			return 1;

		case IDC_RADIO0:
		case IDC_RADIO1:
			if(code==BN_CLICKED && ch->m_chunktype_id==CHUNK_pHYs) {
				phys_calc_inches(hwnd);
			}
			return 1;

		case IDC_BUTTON1:
			switch(ch->m_chunktype_id) {
			case CHUNK_tIME:
				set_to_current_time(hwnd);
				break;
			}
			return 1;

		case IDC_CHECK1:
			switch(ch->m_chunktype_id) {
			case CHUNK_acTL:
				acTL_EnableDisable(hwnd);
				break;
			}
			return 1;

		case IDOK:
			switch(ch->m_chunktype_id) {

			case CHUNK_IHDR:
				tmpint1=GetDlgItemInt(hwnd,IDC_EDIT1,NULL,FALSE);
				tmpint2=GetDlgItemInt(hwnd,IDC_EDIT2,NULL,FALSE);
				write_int32(&ch->data[0],tmpint1); // width
				write_int32(&ch->data[4],tmpint2); // height
				ch->data[8]=(unsigned char)GetDlgItemInt(hwnd,IDC_EDIT3,NULL,FALSE); // bit depth
				ch->data[12]=(IsDlgButtonChecked(hwnd,IDC_CHECK1)==BST_CHECKED)?1:0; // interlace
				tmpint2=(int)SendDlgItemMessage(hwnd,IDC_COLORTYPE,CB_GETCURSEL,0,0);
				switch(tmpint2) {
				case 1: tmpint1=2; break;
				case 2: tmpint1=3; break;
				case 3: tmpint1=4; break;
				case 4: tmpint1=6; break;
				default: tmpint1=0;
				}
				ch->data[9]=(unsigned char)tmpint1;
				break;

			case CHUNK_gAMA:
				GetDlgItemText(hwnd,IDC_GAMMA,buf,100);
				tmpdbl=_tcstod(buf,NULL)*100000.0+0.000001;
				write_int32(ch->data,(DWORD)tmpdbl);
				break;

			case CHUNK_cHRM:
				GetDlgItemText(hwnd,IDC_EDIT1,buf,100); tmpdbl=_tcstod(buf,NULL)*100000.0+0.000001; write_int32(&ch->data[ 0],(DWORD)tmpdbl);
				GetDlgItemText(hwnd,IDC_EDIT2,buf,100); tmpdbl=_tcstod(buf,NULL)*100000.0+0.000001; write_int32(&ch->data[ 4],(DWORD)tmpdbl);
				GetDlgItemText(hwnd,IDC_EDIT3,buf,100); tmpdbl=_tcstod(buf,NULL)*100000.0+0.000001; write_int32(&ch->data[ 8],(DWORD)tmpdbl);
				GetDlgItemText(hwnd,IDC_EDIT4,buf,100); tmpdbl=_tcstod(buf,NULL)*100000.0+0.000001; write_int32(&ch->data[12],(DWORD)tmpdbl);
				GetDlgItemText(hwnd,IDC_EDIT5,buf,100); tmpdbl=_tcstod(buf,NULL)*100000.0+0.000001; write_int32(&ch->data[16],(DWORD)tmpdbl);
				GetDlgItemText(hwnd,IDC_EDIT6,buf,100); tmpdbl=_tcstod(buf,NULL)*100000.0+0.000001; write_int32(&ch->data[20],(DWORD)tmpdbl);
				GetDlgItemText(hwnd,IDC_EDIT7,buf,100); tmpdbl=_tcstod(buf,NULL)*100000.0+0.000001; write_int32(&ch->data[24],(DWORD)tmpdbl);
				GetDlgItemText(hwnd,IDC_EDIT8,buf,100); tmpdbl=_tcstod(buf,NULL)*100000.0+0.000001; write_int32(&ch->data[28],(DWORD)tmpdbl);
				break;

			case CHUNK_sRGB:
				if(IsDlgButtonChecked(hwnd,IDC_SRGB_0)==BST_CHECKED) ch->data[0]=0;
				if(IsDlgButtonChecked(hwnd,IDC_SRGB_1)==BST_CHECKED) ch->data[0]=1;
				if(IsDlgButtonChecked(hwnd,IDC_SRGB_2)==BST_CHECKED) ch->data[0]=2;
				if(IsDlgButtonChecked(hwnd,IDC_SRGB_3)==BST_CHECKED) ch->data[0]=3;
				break;

			case CHUNK_sTER:
				if(IsDlgButtonChecked(hwnd,IDC_STER_0)==BST_CHECKED) ch->data[0]=0;
				if(IsDlgButtonChecked(hwnd,IDC_STER_1)==BST_CHECKED) ch->data[0]=1;
				break;

			case CHUNK_pHYs:
				tmpint1=GetDlgItemInt(hwnd,IDC_EDIT1,NULL,FALSE);
				tmpint2=GetDlgItemInt(hwnd,IDC_EDIT2,NULL,FALSE);
				if(IsDlgButtonChecked(hwnd,IDC_RADIO0)==BST_CHECKED) ch->data[8]=0;
				if(IsDlgButtonChecked(hwnd,IDC_RADIO1)==BST_CHECKED) ch->data[8]=1;
				//if(IsDlgButtonChecked(hwnd,IDC_RADIO2)==BST_CHECKED) {
				//	// convert inches to meters
				//	ch->data[8]=1;
				//	tmpint1 = (int)( ((double)tmpint1 * 100.0) /2.54);
				//	tmpint2 = (int)( ((double)tmpint2 * 100.0) /2.54);
				//}

				write_int32(&ch->data[0],tmpint1);
				write_int32(&ch->data[4],tmpint2);
				break;

			case CHUNK_sBIT:
				if(ch->length>=1)
					ch->data[0]=(unsigned char)GetDlgItemInt(hwnd,IDC_EDIT1,NULL,FALSE);
				if(ch->length>=2)
					ch->data[1]=(unsigned char)GetDlgItemInt(hwnd,IDC_EDIT2,NULL,FALSE);
				if(ch->length>=3)
					ch->data[2]=(unsigned char)GetDlgItemInt(hwnd,IDC_EDIT3,NULL,FALSE);
				if(ch->length>=4)
					ch->data[3]=(unsigned char)GetDlgItemInt(hwnd,IDC_EDIT4,NULL,FALSE);
				break;

			case CHUNK_bKGD:
				switch(ch->m_parentpng->m_colortype) {
				case 3:
					tmpint1=GetDlgItemInt(hwnd,IDC_EDIT1,NULL,FALSE);
					ch->data[0] = (unsigned char)tmpint1;
					break;
				case 0: case 4:
					tmpint1=GetDlgItemInt(hwnd,IDC_EDIT1,NULL,FALSE);
					write_int16(&ch->data[0],tmpint1);
					break;
				case 2: case 6:
					tmpint1=GetDlgItemInt(hwnd,IDC_EDIT1,NULL,FALSE);
					write_int16(&ch->data[0],tmpint1);
					tmpint1=GetDlgItemInt(hwnd,IDC_EDIT2,NULL,FALSE);
					write_int16(&ch->data[2],tmpint1);
					tmpint1=GetDlgItemInt(hwnd,IDC_EDIT3,NULL,FALSE);
					write_int16(&ch->data[4],tmpint1);
					break;
				}
				break;

			case CHUNK_tRNS:
				switch(ch->m_parentpng->m_colortype) {
				case 0: case 4:
					tmpint1=GetDlgItemInt(hwnd,IDC_EDIT1,NULL,FALSE);
					write_int16(&ch->data[0],tmpint1);
					break;
				case 2: case 6:
					tmpint1=GetDlgItemInt(hwnd,IDC_EDIT1,NULL,FALSE);
					write_int16(&ch->data[0],tmpint1);
					tmpint1=GetDlgItemInt(hwnd,IDC_EDIT2,NULL,FALSE);
					write_int16(&ch->data[2],tmpint1);
					tmpint1=GetDlgItemInt(hwnd,IDC_EDIT3,NULL,FALSE);
					write_int16(&ch->data[4],tmpint1);
					break;
				}
				break;

			case CHUNK_tIME:
				tmpint1=GetDlgItemInt(hwnd,IDC_YEAR,NULL,FALSE);
				write_int16(&ch->data[0],tmpint1);
				tmpint1=(int)SendDlgItemMessage(hwnd,IDC_MONTH,CB_GETCURSEL,0,0);
				ch->data[2]=(unsigned char)(tmpint1+1);
				tmpint1=GetDlgItemInt(hwnd,IDC_DAY,NULL,FALSE);
				ch->data[3]=(unsigned char)tmpint1;
				tmpint1=GetDlgItemInt(hwnd,IDC_HOUR,NULL,FALSE);
				ch->data[4]=(unsigned char)tmpint1;
				tmpint1=GetDlgItemInt(hwnd,IDC_MINUTE,NULL,FALSE);
				ch->data[5]=(unsigned char)tmpint1;
				tmpint1=GetDlgItemInt(hwnd,IDC_SECOND,NULL,FALSE);
				ch->data[6]=(unsigned char)tmpint1;
				break;

			case CHUNK_acTL:
				// num frames
				tmpint1=GetDlgItemInt(hwnd,IDC_EDIT1,NULL,FALSE);
				write_int32(&ch->data[0],tmpint1);
				// num plays
				tmpint1=GetDlgItemInt(hwnd,IDC_EDIT2,NULL,FALSE);
				tmpint2=(IsDlgButtonChecked(hwnd,IDC_CHECK1)==BST_CHECKED)?1:0;
				if(tmpint2) tmpint1=0;
				write_int32(&ch->data[4],tmpint1);
				break;

			case CHUNK_fcTL:
				tmpint1=GetDlgItemInt(hwnd,IDC_EDIT1,NULL,FALSE);
				write_int32(&ch->data[0],tmpint1);
				tmpint1=GetDlgItemInt(hwnd,IDC_EDIT2,NULL,FALSE);
				write_int32(&ch->data[4],tmpint1);
				tmpint1=GetDlgItemInt(hwnd,IDC_EDIT3,NULL,FALSE);
				write_int32(&ch->data[8],tmpint1);
				tmpint1=GetDlgItemInt(hwnd,IDC_EDIT4,NULL,FALSE);
				write_int32(&ch->data[12],tmpint1);
				tmpint1=GetDlgItemInt(hwnd,IDC_EDIT5,NULL,FALSE);
				write_int32(&ch->data[16],tmpint1);
				tmpint1=GetDlgItemInt(hwnd,IDC_EDIT6,NULL,FALSE);
				write_int16(&ch->data[20],tmpint1);
				tmpint1=GetDlgItemInt(hwnd,IDC_EDIT7,NULL,FALSE);
				write_int16(&ch->data[22],tmpint1);
				tmpint1=(int)SendDlgItemMessage(hwnd,IDC_DISPOSE,CB_GETCURSEL,0,0);
				ch->data[24]=(unsigned char)tmpint1;
				tmpint1=(int)SendDlgItemMessage(hwnd,IDC_BLEND,CB_GETCURSEL,0,0);
				ch->data[25]=(unsigned char)tmpint1;
				break;

			case CHUNK_fdAT:
				tmpint1=GetDlgItemInt(hwnd,IDC_EDIT1,NULL,FALSE);
				write_int32(&ch->data[0],tmpint1);
				break;

			case CHUNK_oFFs:
				tmpint1=(int)GetDlgItemInt(hwnd,IDC_EDIT1,NULL,TRUE);
				tmpint2=(int)GetDlgItemInt(hwnd,IDC_EDIT2,NULL,TRUE);
				if(IsDlgButtonChecked(hwnd,IDC_RADIO0)==BST_CHECKED) ch->data[8]=0;
				if(IsDlgButtonChecked(hwnd,IDC_RADIO1)==BST_CHECKED) ch->data[8]=1;
				write_int32(&ch->data[0],tmpint1);
				write_int32(&ch->data[4],tmpint2);
				break;

			case CHUNK_sCAL:
				ch->process_sCAL_dlg(hwnd);
				break;

			case CHUNK_iCCP:
				ch->process_iCCP_dlg(p,hwnd);
				break;

			case CHUNK_vpAg:
				tmpint1=(int)GetDlgItemInt(hwnd,IDC_EDIT1,NULL,TRUE);
				tmpint2=(int)GetDlgItemInt(hwnd,IDC_EDIT2,NULL,TRUE);
				write_int32(&ch->data[0],tmpint1);
				write_int32(&ch->data[4],tmpint2);
				tmpint1=(int)GetDlgItemInt(hwnd,IDC_EDIT3,NULL,FALSE);
				ch->data[8]=(unsigned char)tmpint1;
				break;
			}
			EndDialog(hwnd,1);
			return 1;
				
		case IDCANCEL:
			EndDialog(hwnd, 0);
			return 1;
		}
	}
	return 0;
}

#define EDITPAL_MARGIN 3

// set i to -1 to clear values
static void update_irgba(struct edit_plte_ctx *p,HWND hwnd,
	int i,int r,int g,int b,int a,int aflag, int force)
{
	TCHAR buf_i[20],buf_r[20],buf_g[20],buf_b[20],buf_a[20];

	if(i==p->curr_i && !force) return;
	if(i<0) i= -1;
	p->curr_i=i;

	if(i<0) {
		StringCchPrintf(buf_i,20,_T("I:"));
		StringCchPrintf(buf_r,20,_T("R:"));
		StringCchPrintf(buf_g,20,_T("G:"));
		StringCchPrintf(buf_b,20,_T("B:"));
		StringCchPrintf(buf_a,20,_T("A:"));
	}
	else {
		StringCchPrintf(buf_i,20,_T("I: %d"),i);
		StringCchPrintf(buf_r,20,_T("R: %d"),r);
		StringCchPrintf(buf_g,20,_T("G: %d"),g);
		StringCchPrintf(buf_b,20,_T("B: %d"),b);
		if(aflag)
			StringCchPrintf(buf_a,20,_T("A: (255)"));
		else
			StringCchPrintf(buf_a,20,_T("A: %d"),a);
	}

	SetDlgItemText(hwnd,IDC_STATIC_I,buf_i);
	SetDlgItemText(hwnd,IDC_STATIC_R,buf_r);
	SetDlgItemText(hwnd,IDC_STATIC_G,buf_g);
	SetDlgItemText(hwnd,IDC_STATIC_B,buf_b);
	SetDlgItemText(hwnd,IDC_STATIC_A,buf_a);
}

static void update_irgba_byindex(struct edit_plte_ctx *p, HWND hwnd, int c, int force)
{
	int tmp_a, tmp_aflag;
	if(p->caneditcolors) {
		tmp_a = p->plte[c].alpha;
		tmp_aflag = (c>=p->numtrns);
	}
	else {
		if(p->numtrns && c==p->trnscolor) {
			tmp_a=0;
			tmp_aflag=0;
		}
		else {
			tmp_a=255;
			tmp_aflag=1;
		}
	}
	update_irgba(p,hwnd,c,p->plte[c].red,p->plte[c].green,p->plte[c].blue,
		tmp_a,tmp_aflag,force);
}


// return 0-255 palette entry, or -1 for nowhere
static int where_is_cursor(int x,int y, int item_w, int item_h, int maxval)
{
	int x1,y1, v;
	if(item_w<1 || item_h<1) return -1;

	if(x<EDITPAL_MARGIN) return -1;
	if(y<EDITPAL_MARGIN) return -1;

	x1=(x-EDITPAL_MARGIN)/item_w;
	y1=(y-EDITPAL_MARGIN)/item_h;

	if(x1>15) return -1;
	if(y1>15) return -1;

	v=y1*16+x1;
	if(v>=maxval) return -1;
	return v;
}

// This is the message handler just for the embedded
// palette window that shows the palette colors, not the dialog box.
LRESULT CALLBACK WndProcEditPal(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	struct edit_plte_ctx *p = NULL;
	int c;
	unsigned char cr,cg,cb;
	TCHAR buf[100];

	if(msg==WM_CREATE) {
		LPCREATESTRUCT lpcs;
		lpcs = (LPCREATESTRUCT)lParam;
		p = (struct edit_plte_ctx*) lpcs->lpCreateParams;
		SetWindowLongPtr(hwnd,GWLP_USERDATA,(LONG_PTR)lpcs->lpCreateParams);
		return 0;
	}
	else {
		p = (struct edit_plte_ctx*)GetWindowLongPtr(hwnd,GWLP_USERDATA);
		if(!p) goto done;
	}

	switch(msg) {

	case WM_SETCURSOR:
		if(p->bkgdbuttonstate || (p->alphabuttonstate && !p->caneditcolors)) {
			SetCursor(LoadCursor(NULL,IDC_CROSS));
			return 0;
		}
		break;

	case WM_MOUSEMOVE:
		c= where_is_cursor((int)(LOWORD(lParam)),(int)(HIWORD(lParam)),
			p->item_w,p->item_h, p->numplte);
		if(c<0) {
			update_irgba(p,p->hwnd,c,0,0,0,0,0,0);
		}
		else {
			update_irgba_byindex(p,p->hwnd,c,0);
		}
		return 0;

	case WM_LBUTTONDOWN:

		c= where_is_cursor((int)(LOWORD(lParam)),(int)(HIWORD(lParam)),
			p->item_w,p->item_h, p->numplte);
		if(c<0) return 0;


		if(p->bkgdbuttonstate) {
			p->bkgd=c;
			if(p->caneditcolors) {
				p->bkgdbuttonstate=0;
				CheckDlgButton(p->hwnd,IDC_SETBKGD,BST_UNCHECKED);
			}
			InvalidateRect(hwnd,NULL,TRUE);
		}
		else if(p->alphabuttonstate) {
			struct get_int_ctx st;

			if(p->caneditcolors) {
				if(c>=p->numtrns) return 0;
				st.value=(int)p->plte[c].alpha;
				st.label=_T("Enter alpha (0=transparent, 255=opaque)");
				StringCchPrintf(buf,100,_T("Alpha for palette index %d"),c);
				st.title=buf;
				if(DialogBoxParam(globals.hInst,_T("DLG_NUMBER1"),hwnd,
					DlgProcGetInt, (LPARAM)(&st)) )
				{
					if(st.value<0) st.value=0;
					if(st.value>255) st.value=255;
					p->plte[c].alpha=(unsigned char)st.value;
					update_irgba(p,p->hwnd,-2,0,0,0,0,0, 0); // clear
					InvalidateRect(hwnd,NULL,TRUE);
				}
			}
			else {
				p->trnscolor=c;
				update_irgba_byindex(p,p->hwnd,c,1);
				InvalidateRect(hwnd,NULL,TRUE);
			}

		}
		else if(p->caneditcolors) {  // normal palette color edit
			cr= p->plte[c].red;
			cg= p->plte[c].green;
			cb= p->plte[c].blue;
			if( choose_color_dialog(hwnd,&cr,&cg,&cb) ) {
				p->plte[c].red   = cr;
				p->plte[c].green = cg;
				p->plte[c].blue  = cb;
				InvalidateRect(hwnd,NULL,FALSE);
			}
		}
		return 0;

	case WM_PAINT:
		{
			PAINTSTRUCT paintStruct;
			HDC hdc;
			RECT rect;
			HBRUSH hbrush;
			HPEN hpen1,hpen2,hpen3;
			int i,xp,yp,pos;

			if(!p) break;

			hdc = BeginPaint(hwnd,&paintStruct);

			GetClientRect(hwnd,&rect);

			p->item_w= (rect.right-EDITPAL_MARGIN*2)/16;
			if(p->maxplte>16) {
				p->item_h= (rect.bottom-EDITPAL_MARGIN*2)/16;
			}
			else {
				p->item_h= (rect.bottom-EDITPAL_MARGIN*2);
			}

			// indicate background palette entry
			if(p->numbkgd>0) {
				xp= (p->bkgd%16)*p->item_w + EDITPAL_MARGIN;
				yp= (p->bkgd/16)*p->item_h + EDITPAL_MARGIN;

				SelectObject(hdc,GetStockObject(NULL_PEN));
				hbrush=CreateSolidBrush(RGB(0,0,255));
				SelectObject(hdc,hbrush);
				Rectangle(hdc,xp,yp,xp+p->item_w+1,yp+p->item_h+1);

				SelectObject(hdc,GetStockObject(NULL_BRUSH));   // fixme
				DeleteObject(hbrush);
			}

			SelectObject(hdc,GetStockObject(BLACK_PEN));
			for(i=0; i<p->numplte; i++) {
				xp= (i%16)*p->item_w + EDITPAL_MARGIN;
				yp= (i/16)*p->item_h + EDITPAL_MARGIN;
				hbrush=CreateSolidBrush(RGB(p->plte[i].red,p->plte[i].green,p->plte[i].blue));
				SelectObject(hdc,hbrush);
				Rectangle(hdc,xp+1,yp+2,xp+p->item_w-1,yp+p->item_h-2);
				SelectObject(hdc,GetStockObject(NULL_BRUSH));   // fixme
				DeleteObject(hbrush);
			}

			hpen1=CreatePen(PS_SOLID,0,RGB(0,128,128)); // opaque
			hpen2=CreatePen(PS_SOLID,0,RGB(128,0,0)); // translucent
			hpen3=CreatePen(PS_SOLID,0,RGB(255,0,0)); // transparent
			for(i=0; i<p->numtrns; i++) {
				int i_index;
				unsigned char i_alpha;

				if(p->caneditcolors) {
					i_index=i;
					i_alpha=p->plte[i].alpha;
				}
				else {
					i_index=p->trnscolor;
					i_alpha=0;
				}

				xp= (i_index%16)*p->item_w + EDITPAL_MARGIN;
				yp= (i_index/16)*p->item_h + EDITPAL_MARGIN;

				pos= (int)( (double)(p->item_w-2.1)* ((double)i_alpha / 255.0) );

				if(i_alpha==0) SelectObject(hdc,hpen3);
				else if(i_alpha==255) SelectObject(hdc,hpen1);
				else SelectObject(hdc,hpen2);

				MoveToEx(hdc,xp+1+pos,yp+p->item_h-7,NULL);
				LineTo(hdc,xp+1+pos,yp+p->item_h+0);

				MoveToEx(hdc,xp+0+pos,yp+p->item_h-6,NULL);
				LineTo(hdc,xp+0+pos,yp+p->item_h-1);
				MoveToEx(hdc,xp+2+pos,yp+p->item_h-6,NULL);
				LineTo(hdc,xp+2+pos,yp+p->item_h-1);
			}
			SelectObject(hdc,GetStockObject(NULL_PEN));
			DeleteObject(hpen1);
			DeleteObject(hpen2);
			DeleteObject(hpen3);

			EndPaint(hwnd,&paintStruct);
			return 0;
		}

	}

done:
	return (DefWindowProc(hwnd, msg, wParam, lParam));
}


static void set_pal_labels(HWND hwnd,struct edit_plte_ctx *p)
{
	TCHAR buf[256];
	if(p->caneditcolors)
		StringCchPrintf(buf,256,_T("Colors in palette (%d") SYM_ENDASH _T("%d):"),p->minplte,p->maxplte);
	else StringCchCopy(buf,256,_T("Gray shades:"));
	SetDlgItemText(hwnd,IDC_STATIC1,buf);

	if(p->caneditcolors)
		StringCchPrintf(buf,256,_T("Alpha values (%d") SYM_ENDASH _T("%d):"),p->mintrns,(p->mintrns)?(p->numplte):0);
	else
		StringCchCopy(buf,256,_T("Transparent colors:"));
	SetDlgItemText(hwnd,IDC_STATIC2,buf);
}


static INT_PTR CALLBACK DlgProcEdit_PLTE(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id, code;
	int r;
	struct edit_plte_ctx *p = NULL;

	RECT rd,r1;


	id=LOWORD(wParam);
	code=HIWORD(wParam);


	if(msg==WM_INITDIALOG) {
		p = (struct edit_plte_ctx*)lParam;
		if(!p) return TRUE;
		SetWindowLongPtr(hwnd,DWLP_USER,lParam);

		SendMessage(hwnd,WM_SETICON,ICON_BIG,(LPARAM)LoadIcon(globals.hInst,_T("ICONMAIN")));

		// save some control position metrics for later resizing
		GetClientRect(hwnd,&rd);

		GetPosInParent(GetDlgItem(hwnd,IDC_STATIC1),&r1);
		p->border_width=r1.left;

		GetPosInParent(GetDlgItem(hwnd,IDC_EDIT2),&r1);
		p->color_top= r1.bottom + 5;

		GetPosInParent(GetDlgItem(hwnd,IDC_STATIC_A),&r1);
		p->color_left=r1.right+3;

		GetPosInParent(GetDlgItem(hwnd,IDOK),&r1);
		p->border_buttonoffset = (rd.right-rd.left)-r1.left;
		p->border_btn1y = r1.top;
		p->border_height=r1.top;

		GetPosInParent(GetDlgItem(hwnd,IDCANCEL),&r1);
		p->border_btn2y = r1.top;

//		GetPosInParent(hwndEditPal,&r1);
//		border_editx = (rd.right-rd.left)-r1.right;
//		border_edity = (rd.bottom-rd.top)-r1.bottom;

		//g_editpal_palinfo_pointer = p;
		p->hwndEditPal = CreateWindow(
			_T("TWEAKPNGEDITPAL"),_T("EditPal"),
			WS_VISIBLE|WS_CHILD|WS_BORDER,
			p->color_left,p->color_top,rd.right-p->border_width-p->color_left,
			rd.bottom-p->color_top-p->border_height,
			hwnd,NULL,globals.hInst,(void*)p);

		set_pal_labels(hwnd,p);
		update_irgba(p,hwnd,-2,0,0,0,0,0,0);

		SendDlgItemMessage(hwnd,IDC_EDIT1,EM_LIMITTEXT,3,0);
		SendDlgItemMessage(hwnd,IDC_EDIT2,EM_LIMITTEXT,3,0);

		SetDlgItemInt(hwnd,IDC_EDIT1,p->numplte,TRUE);
		SetDlgItemInt(hwnd,IDC_EDIT2,p->numtrns,TRUE);
		if(!p->caneditcolors) {
			SetDlgItemText(hwnd,IDC_EDITALPHA,_T("Set trns")); // change button label
		}

		CheckDlgButton(hwnd,IDC_SETBKGD,p->bkgdbuttonstate?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hwnd,IDC_EDITALPHA,p->alphabuttonstate?BST_CHECKED:BST_UNCHECKED);

		EnableWindow(GetDlgItem(hwnd,IDC_EDIT1),p->caneditcolors?1:0);
		EnableWindow(GetDlgItem(hwnd,IDC_EDIT2),(p->mintrns && p->caneditcolors)?1:0);
		EnableWindow(GetDlgItem(hwnd,IDC_SETBKGD),(p->numbkgd?1:0));
		EnableWindow(GetDlgItem(hwnd,IDC_EDITALPHA),(p->mintrns?1:0));
		EnableWindow(GetDlgItem(hwnd,IDC_EDITALLALPHA),(p->mintrns && p->caneditcolors)?1:0);


		p->hwnd=hwnd;

		twpng_SetWindowPos(hwnd,&globals.window_prefs.plte);
		if(globals.window_prefs.plte.max) ShowWindow(hwnd,SW_SHOWMAXIMIZED);

		return TRUE;
	}
	else {
		p = (struct edit_plte_ctx*)GetWindowLongPtr(hwnd,DWLP_USER);
		if(!p) return FALSE;
	}

	switch (msg) {
	case WM_SIZE:

		GetClientRect(hwnd,&rd);

		SetWindowPos(GetDlgItem(hwnd,IDOK),NULL,rd.right-p->border_buttonoffset,p->border_btn1y,0,0,SWP_NOSIZE|SWP_NOZORDER);
		SetWindowPos(GetDlgItem(hwnd,IDCANCEL),NULL,rd.right-p->border_buttonoffset,p->border_btn2y,0,0,SWP_NOSIZE|SWP_NOZORDER);

		SetWindowPos(p->hwndEditPal,NULL,
		p->color_left, p->color_top, 
		rd.right-p->border_width-p->color_left, rd.bottom-p->color_top-p->border_height,
			SWP_NOMOVE|SWP_NOZORDER);
		return 1;

	case WM_GETMINMAXINFO:
		{
			LPMINMAXINFO mm;
			mm=(LPMINMAXINFO)lParam;
			mm->ptMinTrackSize.x=400;
			mm->ptMinTrackSize.y=280;
			return 1;
		}

	case WM_DESTROY:
		twpng_StoreWindowPos(hwnd,&globals.window_prefs.plte);
		break;

	case WM_COMMAND:
		switch(id) {
		case IDC_EDIT1:
			if(code==EN_CHANGE) {   // user changed palette size
				r=(int)GetDlgItemInt(hwnd,IDC_EDIT1,NULL,TRUE);
				if(r<p->minplte) r=p->minplte;
				if(r>p->maxplte) r=p->maxplte;
				p->numplte=r;
				set_pal_labels(hwnd,p);
				InvalidateRect(p->hwndEditPal,NULL,TRUE);
				return 1;
			}
			break;
		case IDC_EDIT2:
			if(code==EN_CHANGE) {  // user changed # of alpha values
				r=(int)GetDlgItemInt(hwnd,IDC_EDIT2,NULL,TRUE);
				if(r<p->mintrns) r=p->mintrns;
				if(r>p->maxplte) r=p->maxplte;
				p->numtrns=r;
				set_pal_labels(hwnd,p);
				InvalidateRect(p->hwndEditPal,NULL,TRUE);
				return 1;
			}
			break;
		case IDC_SETBKGD:
			if(code==BN_CLICKED) {
				if(p->caneditcolors) {
					p->bkgdbuttonstate= !p->bkgdbuttonstate;
					CheckDlgButton(hwnd,IDC_SETBKGD,p->bkgdbuttonstate?BST_CHECKED:BST_UNCHECKED);
				}
				else {
					// If we can't edit colors, BKGD and TRNS buttons act like radio buttons.
					p->bkgdbuttonstate=1;
					p->alphabuttonstate=0;
					CheckDlgButton(hwnd,IDC_SETBKGD,BST_CHECKED);
					CheckDlgButton(hwnd,IDC_EDITALPHA,BST_UNCHECKED);
				}
			}
			break;
		case IDC_EDITALPHA:
			if(code==BN_CLICKED) {
				if(p->caneditcolors) {
					p->alphabuttonstate= !p->alphabuttonstate;
					CheckDlgButton(hwnd,IDC_EDITALPHA,p->alphabuttonstate?BST_CHECKED:BST_UNCHECKED);
				}
				else {
					p->bkgdbuttonstate=0;
					p->alphabuttonstate=1;
					CheckDlgButton(hwnd,IDC_SETBKGD,BST_UNCHECKED);
					CheckDlgButton(hwnd,IDC_EDITALPHA,BST_CHECKED);
				}
			}
			break;
		case IDC_EDITALLALPHA:
			{
				struct get_int_ctx st;
				int i;

				st.value=255;
				st.label=_T("Set all alpha (0=transparent, 255=opaque)");
				st.title=_T("Set alpha for all palette colors");
				if(DialogBoxParam(globals.hInst,_T("DLG_NUMBER1"),hwnd,
					DlgProcGetInt, (LPARAM)(&st)) )
				{
					if(st.value<0) st.value=0;
					if(st.value>255) st.value=255;

					for(i=0;i<p->maxplte;i++) {
						p->plte[i].alpha = (unsigned char)st.value;
					}
					p->numtrns= (st.value==255)?1:p->numplte;
					SetDlgItemInt(hwnd,IDC_EDIT2,p->numtrns,TRUE);
					InvalidateRect(hwnd,NULL,TRUE);
				}
			}
			return 1;
		case IDOK:
			EndDialog(hwnd,1);
			return 1;
		case IDCANCEL:
			EndDialog(hwnd,0);
			return 1;
		}
	}
	return 0;
}


// returns 1 if successful, 0 if user canceled
static INT_PTR CALLBACK DlgProcGetInt(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	struct get_int_ctx *st = NULL;
	WORD id;

	if(msg==WM_INITDIALOG) {
		st = (struct get_int_ctx*)lParam;
		if(!st) return 1;
		SetWindowLongPtr(hwnd,DWLP_USER,lParam);
		SetWindowText(hwnd,st->title);
		SetDlgItemText(hwnd,IDC_LABEL1,st->label);
		SetDlgItemInt(hwnd,IDC_EDIT1,st->value,TRUE);
		return 1;
	}
	else {
		st = (struct get_int_ctx*)GetWindowLongPtr(hwnd,DWLP_USER);
		if(!st) return 0;
	}

	id=LOWORD(wParam);

	switch (msg) {
	case WM_COMMAND:
		if(!st) return 1;
		switch(id) {
		case IDOK:
			st->value=GetDlgItemInt(hwnd,IDC_EDIT1,NULL,TRUE);
			EndDialog(hwnd,1);
			return 1;
		case IDCANCEL:
			EndDialog(hwnd, 0);
			return 1;
		}
		break;
	}
	return 0;
}
