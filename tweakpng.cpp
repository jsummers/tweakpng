// tweakpng.cpp
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

// Try to use a less ancient version of comctl32.dll than the default.
// This belongs in the .manifest file(s), but I can't figure out how to
// get it to work.
#ifdef _WIN64
#pragma comment(linker, \
	"\"/manifestdependency:type='Win32' "\
	"name='Microsoft.Windows.Common-Controls' "\
	"version='6.0.0.0' "\
	"processorArchitecture='amd64' "\
	"publicKeyToken='6595b64144ccf1df' "\
	"language='*'\"")
#else
#pragma comment(linker, \
	"\"/manifestdependency:type='Win32' "\
	"name='Microsoft.Windows.Common-Controls' "\
	"version='6.0.0.0' "\
	"processorArchitecture='x86' "\
	"publicKeyToken='6595b64144ccf1df' "\
	"language='*'\"")
#endif

#include "twpng-config.h"

#include <windows.h>
#include <tchar.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <stdarg.h>
#include <commctrl.h>

#include "resource.h"
#include "tweakpng.h"
#ifdef TWPNG_HAVE_ZLIB
#include <zlib.h>
#else
#define Z_DEFAULT_COMPRESSION  (-1)
#endif
#include <strsafe.h>

#define UPDATE_DELAY 400  // milliseconds

static Png *png;
Viewer *g_viewer;

struct globals_struct globals;

static LRESULT CALLBACK WndProcMain(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcAbout(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcPrefs(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcSplitIDAT(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcSetSig(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcTools(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static int OkToClosePNG();
static void SetTitle(Png *p);
static void ImportChunkByFilename(const TCHAR *fn, int pos);
static int GetLVFocus(HWND hwnd);

/* make the table for a fast crc */
void make_crc_table()
{
  int n;

  for (n = 0; n < 256; n++) {
    DWORD c;
    int k;

    c = (DWORD)n;
    for (k = 0; k < 8; k++)
      c = c & 1 ? 0xedb88320L ^ (c >> 1):c >> 1;

    globals.crc_table[n] = c;
  }
}


DWORD update_crc(DWORD crc, unsigned char *buf, int len)
{
  DWORD c = crc;
  unsigned char *p = buf;
  int n = len;

  if (n > 0) do {
    c = globals.crc_table[(c ^ (*p++)) & 0xff] ^ (c >> 8);
  } while (--n);
  return c;
}

void mesg(int severity, const TCHAR *fmt, ...)
{
	va_list ap;
	TCHAR buf[2048];
	const TCHAR *t;
	int flags;

	va_start(ap, fmt);
	StringCbVPrintf(buf,sizeof(buf),fmt,ap);
	va_end(ap);

	switch(severity) {
	case MSG_S: t=_T("Error");   flags=MB_ICONERROR;        break;
	case MSG_W: t=_T("Warning"); flags=MB_ICONWARNING;      break;
	case MSG_I: t=_T("Notice");  flags=MB_ICONINFORMATION;  break;
	default:    t=_T("Error");   flags=MB_ICONWARNING;      break;  // (MSG_E)
	}
	MessageBox(globals.hwndMain,buf,t,MB_OK|flags);
}

// Some functions for reading/writing big-endian integers.

DWORD read_int32(unsigned char *x)
{
//	return x[0]*256*256*256 +
//	       x[1]*256*256 +
//	       x[2]*256 +
//	       x[3];
	return (((DWORD)x[0])<<24) +
	       (((DWORD)x[1])<<16) +
	       (((DWORD)x[2])<<8) +
	       ((DWORD)x[3]);
}

void write_int32(unsigned char *buf, DWORD x)
{
	buf[0]= (unsigned char) ((x & 0xff000000)>>24);
	buf[1]= (unsigned char) ((x & 0x00ff0000)>>16);
	buf[2]= (unsigned char) ((x & 0x0000ff00)>> 8);
	buf[3]= (unsigned char) ( x & 0x000000ff     );
}


int read_int16(unsigned char *x)
{
	return x[0]*256 +
	       x[1];
}

void write_int16(unsigned char *buf, int x)
{
	buf[0]= (unsigned char) ((x & 0x0000ff00)>> 8);
	buf[1]= (unsigned char) ( x & 0x000000ff     );
}


// return pointer to first chunk of requested type.
// return NULL if none exist
// returns the index of the chunk if index != NULL
Chunk* Png::find_first_chunk(int id, int *index)
{
	int i;
	for(i=0;i<m_num_chunks;i++) {
		if(chunk[i]->m_chunktype_id==id) {
			if(index) (*index)=i;
			return chunk[i];
		}
	}
	if(index) (*index)= -1;
	return NULL;
}

// fill in a single row (chunk) of the listview window
void Png::update_row(HWND hwnd,int i)
{
	TCHAR buf[4096];
	LV_ITEM lvi;
	int rv;

	ZeroMemory((void*)&lvi,sizeof(LV_ITEM));
	lvi.mask= LVIF_TEXT    /* LVIF_DI_SETITEM */;

	lvi.iItem=i;
	lvi.iSubItem=0;
	lvi.pszText=chunk[i]->m_chunktype_tchar;
	rv=ListView_SetItem(hwnd,&lvi);

	lvi.pszText=buf;

	StringCchPrintf(buf,4096,_T("%u"),chunk[i]->length);       // length
	lvi.iSubItem=1;
	rv=ListView_SetItem(hwnd,&lvi);

	StringCchPrintf(buf,4096,_T("%08x"),chunk[i]->m_crc);          // crc
	lvi.iSubItem=2;
	rv=ListView_SetItem(hwnd,&lvi);


	chunk[i]->get_text_descr_generic(buf,4096);        // attributes
	lvi.iSubItem=3;
	rv=ListView_SetItem(hwnd,&lvi);

	chunk[i]->get_text_descr(buf,4096);     // contents
	lvi.iSubItem=4;
	rv=ListView_SetItem(hwnd,&lvi);
}

// fill listview control, one line per PNG chunk
void Png::fill_listbox(HWND hwnd)
{
	int i;
	int rv;
	TCHAR textbuf[50];

	LV_ITEM lvi;

	ListView_DeleteAllItems(hwnd);

	ZeroMemory((void*)&lvi,sizeof(LV_ITEM));
	lvi.mask= LVIF_TEXT    /* | LVIF_DI_SETITEM ? */;

	StringCbCopy(textbuf, sizeof(textbuf), _T(""));
	for(i=0;i<m_num_chunks;i++) {
		lvi.iItem=i;
		lvi.iSubItem=0;
		lvi.pszText=textbuf;  // start with blank; update_row() will set everything
		rv=ListView_InsertItem(hwnd,&lvi);

		update_row(hwnd,i);
	}
}

DWORD Png::get_file_size()
{
	int i;
	DWORD s=0;

	// calculate new file size
	s=8;   // the file signature
	for(i=0;i<m_num_chunks;i++) {
		s+= 12+chunk[i]->length;
	}
	return s;
}

static void update_viewer_filename()
{
#ifdef TWPNG_SUPPORT_VIEWER
	if(!g_viewer) return;
	if(png && png->m_named)
		g_viewer->SetCurrentFileName(png->m_filename);
	else
		g_viewer->SetCurrentFileName(NULL);
#endif
}

static void update_status_bar_and_viewer()
{
	TCHAR buf[100];
	DWORD s;
	const TCHAR *type;

	if(!globals.hwndStBar) { goto done; }
	if(!png) {
		SetWindowText(globals.hwndStBar,_T("No file loaded"));
		goto done;
	}

	switch(png->m_imgtype) {
	case IMG_PNG: type=_T("PNG"); break;
	case IMG_MNG: type=_T("MNG"); break;
	case IMG_JNG: type=_T("JNG"); break;
	default:      type=_T("???"); break;
	}

	s=png->get_file_size();

	StringCchPrintf(buf,100,_T("%s file size: %u bytes"),type,(unsigned int)s);
	SetWindowText(globals.hwndStBar,buf);

done:
	if(g_viewer) {
		SetTimer(globals.hwndMain,1,UPDATE_DELAY,NULL);
		globals.timer_set=1;
	}
}

void Png::modified()
{
	if(!m_dirty) {
		m_dirty=1;
		SetTitle(this);
	}
	update_status_bar_and_viewer();
}

int Png::write_to_mem(unsigned char **pmem, int *plen)
{
	int s;
	int i;
	int pos;

	unsigned char *m;

	// create an image of the file in memory
	s=get_file_size();

	m=(unsigned char*)malloc(s);
	if(!m) return 0;

	CopyMemory(m,signature,8);

	pos=8;
	for(i=0;i<m_num_chunks;i++) {
		chunk[i]->copy_to_memory(&m[pos]);
		pos+= 12+chunk[i]->length;
	}

	*pmem = m;
	*plen = s; // should also = pos
	return 1;
}

// Call before a sequence of stream_file_read() calls.
void Png::stream_file_start()
{
	m_stream_phase = 0;
	m_stream_curchunk = 0; // -1 means we're reading the signature
	m_stream_curpos_in_curchunk = 0;
}

// Starting at position m_stream_curpos_in_curchunk in chunk m_stream_curchunk,
// copy nbytes bytes of the PNG file into buf (unless end of file is reached).
// This may require reading from the following chunk(s).
// Returns the number of bytes copied.
DWORD Png::stream_file_read(unsigned char *buf, DWORD bytes_requested)
{
	DWORD total_bytes_copied=0; // bytes copied in this call to stream_file_read
	DWORD nread;
	DWORD bytes_to_copy;

	// Read the file signature, if necessary.
	if(m_stream_phase==0) {
		bytes_to_copy = 8-m_stream_curpos_in_curchunk;
		if(bytes_to_copy>bytes_requested) bytes_to_copy=bytes_requested;

		CopyMemory(buf,&signature[m_stream_curpos_in_curchunk],bytes_to_copy);
		m_stream_curpos_in_curchunk+=bytes_to_copy;
		total_bytes_copied+=bytes_to_copy;
		if(total_bytes_copied>=8) {
			// Done reading signature. Now switch to reading chunks.
			m_stream_phase=1;
			m_stream_curchunk=0;
			m_stream_curpos_in_curchunk=0;
		}
		else {
			goto done;
		}
	}

	// Read the chunk(s).
	while(1) {
		if(total_bytes_copied>=bytes_requested) goto done;

		if(m_stream_curchunk>=m_num_chunks) goto done;

		if(m_stream_curpos_in_curchunk >= chunk[m_stream_curchunk]->length+12) {
			// advance to next chunk
			m_stream_curchunk++;
			m_stream_curpos_in_curchunk=0;
			if(m_stream_curchunk>=m_num_chunks) goto done;
		}
	
		nread = chunk[m_stream_curchunk]->copy_segment_to_memory(&buf[total_bytes_copied],
			m_stream_curpos_in_curchunk, bytes_requested-total_bytes_copied);
		m_stream_curpos_in_curchunk+=nread;
		total_bytes_copied+=nread;
	}

done:
	return total_bytes_copied;
}

// opens up space for new chunks and optionally creates them.
// if init is 0, opens up space for new chunks but doesn't create them
void Png::insert_chunks(int pos, int num, int init)
{
	int i;

	init_new_chunk(m_num_chunks+num);

	for(i=m_num_chunks+num-1;i>=(pos+num);i--) {
		chunk[i] = chunk[i-num];
	}

	if(init) {
		for(i=pos;i<pos+num;i++) {
			chunk[i] = new Chunk;
			chunk[i]->m_parentpng=this;
		}
	}

	m_num_chunks+=num;
}

// insert a new chunk of the specified type
void Png::new_chunk(int newid)
{
	int pos,ok,i;
	SYSTEMTIME st;
	Chunk *c;
	char newname[5];
	int plte_pos;

	pos=1;
	ok=1;

	c = new Chunk();
	c->m_parentpng=png;

	if(!get_name_from_id(newname,newid)) {
		mesg(MSG_S,_T("internal: chunk name not found for %d"),newid);
		delete c;
		return;
	}
	StringCchCopyA(c->m_chunktype_ascii,5,newname);
	c->set_chunktype_tchar_from_ascii();

	// plte_pos will be -1 if no PLTE chunk exists
	find_first_chunk(CHUNK_PLTE, &plte_pos);
	
	switch(newid) {
	case CHUNK_tEXt:
		pos=m_num_chunks-1;
		c->length=8;
		c->data = (unsigned char*)malloc(c->length);  // fixme, check for failure
		StringCchCopyA((char*)c->data,8,"Comment");
		break;

	case CHUNK_gAMA:
		c->length=4;
		c->data = (unsigned char*)malloc(c->length);
		write_int32(c->data,45455);
		break;

	case CHUNK_oFFs:
		c->length=9;
		c->data = (unsigned char*)malloc(c->length);
		write_int32(&c->data[0],0);
		write_int32(&c->data[4],0);
		c->data[8]=0;
		break;

	case CHUNK_pHYs:
		c->length=9;
		c->data = (unsigned char*)malloc(c->length);
		write_int32(&c->data[0],1);
		write_int32(&c->data[4],1);
		c->data[8]=0;
		break;

	case CHUNK_sRGB:
		c->length=1;
		c->data = (unsigned char*)malloc(c->length);
		c->data[0]=0;
		break;

	case CHUNK_sTER:
		c->length=1;
		c->data = (unsigned char*)malloc(c->length);
		c->data[0]=0;
		break;

	case CHUNK_cHRM:
		c->length=32;
		c->data = (unsigned char*)malloc(c->length);
		write_int32(&c->data[ 0],31270);
		write_int32(&c->data[ 4],32900);
		write_int32(&c->data[ 8],64000);
		write_int32(&c->data[12],33000);
		write_int32(&c->data[16],30000);
		write_int32(&c->data[20],60000);
		write_int32(&c->data[24],15000);
		write_int32(&c->data[28], 6000);
		break;

	case CHUNK_bKGD:
		if(plte_pos>=0) pos=plte_pos+1;
		switch(m_colortype) {
		case 3: c->length=1; break;
		case 0: case 4: c->length=2; break;
		case 2: case 6: c->length=6; break;
		default: return;
		}
		c->data = (unsigned char*)calloc(c->length,1);
		break;

	case CHUNK_sBIT:
		switch(m_colortype) {
		case 0: c->length=1; break;
		case 2: case 3: c->length=3; break;
		case 4: c->length=2; break;
		case 6: c->length=4; break;
		default: return;
		}
		c->data = (unsigned char*)calloc(c->length,1);
		for(i=0;i<(int)c->length;i++) {
			c->data[i]= (m_colortype==3)?8:m_bitdepth;
		}
		break;

	case CHUNK_PLTE:
		if(m_colortype!=2 && m_colortype!=3 && m_colortype!=6) {
			mesg(MSG_E,_T("Palette is not allowed for grayscale images"));
			return;
		}
		c->length=3;
		c->data = (unsigned char*)calloc(c->length,1);
		break;

	case CHUNK_tRNS:
		if(plte_pos>=0) pos=plte_pos+1;
		switch(m_colortype) {
		case 3: c->length=1; break;
		case 0: c->length=2; break;
		case 2: c->length=6; break;
		case 4: case 6:
			mesg(MSG_E,_T("Transparency chunk is not allowed for this image, since ")
				_T("it already has a full alpha channel"));
			return;
		default: return;
		}
		c->data = (unsigned char*)calloc(c->length,1);
		if(m_colortype==3) c->data[0]=255;  // default to opaque
		break;

	case CHUNK_IHDR:
		pos=0;
		c->length=13;
		c->data = (unsigned char*)calloc(c->length,1);
		write_int32(&c->data[ 0],1); // width
		write_int32(&c->data[ 4],1); // height
		c->data[8]=1; // bit depth
		break;

	case CHUNK_IEND:
		pos=m_num_chunks;
		c->length=0;
		c->data = NULL;
		break;

	case CHUNK_tIME:
		pos=m_num_chunks-1;
		c->length=7;
		c->data = (unsigned char*)malloc(c->length);

		GetSystemTime(&st);
		write_int16(&c->data[0],(int)st.wYear);
		c->data[2]= (unsigned char)st.wMonth;
		c->data[3]= (unsigned char)st.wDay;
		c->data[4]= (unsigned char)st.wHour;
		c->data[5]= (unsigned char)st.wMinute;
		c->data[6]= (unsigned char)st.wSecond;
		break;

	case CHUNK_sCAL:
		c->length=8;
		c->data = (unsigned char*)malloc(c->length);
		c->data[0]= 1;
		c->data[1]= '1'; c->data[2]= '.'; c->data[3]= '0';
		c->data[4]= 0;
		c->data[5]= '1'; c->data[6]= '.'; c->data[7]= '0';
		break;

	case CHUNK_acTL:
		c->length=8;
		c->data = (unsigned char*)calloc(c->length,1);
		write_int32(&c->data[0],1);
		write_int32(&c->data[4],0);
		break;

	case CHUNK_fcTL:
		c->length=26;
		c->data = (unsigned char*)calloc(c->length,1);
		write_int32(&c->data[4],m_width);
		write_int32(&c->data[8],m_height);
		write_int16(&c->data[20],100); // delay numerator
		write_int16(&c->data[22],100); // delay denominator
		break;

	case CHUNK_vpAg:
		c->length=9;
		c->data = (unsigned char*)malloc(c->length);
		write_int32(&c->data[0],m_width);
		write_int32(&c->data[4],m_height);
		c->data[8]=0;
		break;

	default:
		ok=0;
	}

	if(!ok) {
		mesg(MSG_W,_T("Not implemented"));
		return;
	}

	if(pos<0) pos=0;
	if(pos>m_num_chunks) pos=m_num_chunks;

	insert_chunks(pos,1,1);

	chunk[pos]=c;
	c->after_init();
	c->chunkmodified();

	fill_listbox(globals.hwndMainList);
	twpng_SetLVSelection(globals.hwndMainList,pos,1);
	modified();
}


void Png::edit_chunk(int n)
{
	int r,i;

	if(n<0 || n>=m_num_chunks) return;
	r= chunk[n]->edit();

	if(r>0) {
		if(r==2) {
			// update all
			for(i=0;i<m_num_chunks;i++) {
				update_row(globals.hwndMainList,i);
			}
		}
		else if(r==1) {
			update_row(globals.hwndMainList,n);
		}
		else {
			mesg(MSG_S,_T("internal error: change code %d"),r);
		}
		modified();
	}
}

// save to disk
// returns 1 on success, 0 on failure
int Png::write_file(const TCHAR *fn)
{
	int i;
	HCURSOR hcur;
	DWORD written;
	HANDLE fh;

	fh=CreateFile(fn,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,NULL);
	if(fh==INVALID_HANDLE_VALUE) {
		mesg(MSG_E,_T("Can") SYM_RSQUO _T("t write file (%s)"),fn);
		return 0;
	}

	hcur=SetCursor(LoadCursor(NULL,IDC_WAIT));

	WriteFile(fh,(LPVOID)signature,8,&written,NULL);

	for(i=0;i<m_num_chunks;i++) {
		chunk[i]->write_to_file(fh,0);
	}
	CloseHandle(fh);
	SetCursor(hcur);
	return 1;
}

// make sure we have room for have n chunks in the chunk[] array
void Png::init_new_chunk(int n)   
{
	if(n<m_chunks_alloc) return;
	// oops -- overran our chunks array. try to alloc a bigger one
	m_chunks_alloc += 200;
	chunk=(Chunk**)realloc((void*)chunk, m_chunks_alloc*sizeof(Chunk*));
	if(!chunk) {
		mesg(MSG_S,_T("can") SYM_RSQUO _T("t alloc memory for chunks array"));
		m_num_chunks=0;
	}
}

void Png::delete_chunk(int n)
{
	int i;
	if(n<0 || n>=m_num_chunks) return;

	delete chunk[n];
	for(i=n+1;i<m_num_chunks;i++) {
		chunk[i-1]=chunk[i];
	}
	m_num_chunks--;
}

void Png::move_chunk(int n, int delta)  // move chunk n by delta
{
	Chunk *tmpchunk;
	int moveto;
	int i;

	if(n<0 || n>=m_num_chunks) return;
	moveto=n+delta;

	if(moveto<0) moveto=0;
	if(moveto>=m_num_chunks) moveto=m_num_chunks-1;

	if(moveto==n) return;

	if(moveto>n) {        // move down
		for(i=0;i<(moveto-n);i++) {
			tmpchunk=chunk[n+i];       //swap(n+i,n+i+1)
			chunk[n+i]=chunk[n+i+1];
			chunk[n+i+1]=tmpchunk;
		}
	}
	else {        // move up      (moveto < n)
		for(i=(n-moveto-1);i>=0;i--) {
			tmpchunk=chunk[moveto+i];   //swap(moveto+i+1,moveto+i)
			chunk[moveto+i]=chunk[moveto+i+1];
			chunk[moveto+i+1]=tmpchunk;
		}
	}
}


static const unsigned char sig_png[] = {137,80,78,71,13,10,26,10};
static const unsigned char sig_mng[] = {138,77,78,71,13,10,26,10};
static const unsigned char sig_jng[] = {139,74,78,71,13,10,26,10};

void Png::set_signature()
{
	switch(m_imgtype) {
	case IMG_PNG: memcpy(signature,sig_png,8); break;
	case IMG_MNG: memcpy(signature,sig_mng,8); break;
	case IMG_JNG: memcpy(signature,sig_jng,8); break;
	}
}

int Png::read_signature(HANDLE fh)
{
	DWORD n;
	int r;

	r=ReadFile(fh,(LPVOID)signature,8,&n,NULL);
	if(!r || n<8) return 0;
	if(!memcmp(signature,sig_png,8)) return IMG_PNG;
	if(!memcmp(signature,sig_mng,8)) return IMG_MNG;
	if(!memcmp(signature,sig_jng,8)) return IMG_JNG;
	return IMG_UNKNOWN;
}

int Png::read_next_chunk(HANDLE fh, DWORD *filepos)
{
	DWORD n;
	Chunk *c;
	unsigned char fbuf[8];
	DWORD ccrc;
	int r;
	int i;

	// first 4 bytes are the chunk data length,
	// next 4 bytes are the chunk type
	r=ReadFile(fh,(LPVOID)fbuf,8,&n,NULL);
	if(!r || n==0) {  // this is normal; we've reached the end of file
		return 0;
	}

	if(n!=8) {
		mesg(MSG_W,_T("Garbage found at end of file"));
		return 0;
	}

	// allocate a Chunk structure for this new chunk
	c = new Chunk();

	c->m_parentpng = this;  // chunks sometimes depend other chunks, ...

	c->length= read_int32(&fbuf[0]);

	memcpy(c->m_chunktype_ascii,&fbuf[4],4);
	c->m_chunktype_ascii[4]='\0';  // null-terminate for convenience
	for(i=0;i<4;i++) {
		if( !((c->m_chunktype_ascii[i]>='a' && c->m_chunktype_ascii[i]<='z') ||
			(c->m_chunktype_ascii[i]>='A' && c->m_chunktype_ascii[i]<='Z')))
		{
			mesg(MSG_W,_T("Invalid chunk type found at file position %u. ")
				_T("This may indicate garbage at the end of the file."),*filepos);
			delete c;
			return 0;
		}
	}
	c->set_chunktype_tchar_from_ascii();

	// now read the data
	if(c->length>0) {

		// A sanity test for the chunk length.
		if(c->length > m_pngfilesize - *filepos - 12) {
			mesg(MSG_W,_T("Bogus chunk length found. This may indicate garbage at the end of the file."));
			delete c;
			return 0;
		}

		c->data = (unsigned char*)malloc(c->length);
		if(!c->data) {
			mesg(MSG_S,_T("Can") SYM_RSQUO _T("t allocate memory for chunk"));
			delete c;
			return 0;
		}
	}

	r=ReadFile(fh,(LPVOID)c->data,c->length,&n,NULL);
	if(!r || n!=c->length) {
		mesg(MSG_W,_T("Garbage found at end of file"));
		delete c;
		return 0;
	}

	// now read the CRC
	r=ReadFile(fh,(LPVOID)fbuf,4,&n,NULL);
	if(!r || n!=4) {
		mesg(MSG_W,_T("Garbage found at end of file"));
		delete c;
		return 0;
	}

	c->m_crc = read_int32(&fbuf[0]);

	// check the crc
	ccrc=c->calc_crc();

	if(c->m_crc != ccrc) {
		mesg(MSG_W,_T("Incorrect crc for %s chunk (is %08x, should be %08x)"),
			c->m_chunktype_tchar, c->m_crc, ccrc);
		c->m_crc=ccrc;  // correct it
	}

	init_new_chunk(m_num_chunks);  // make sure chunks array is large enough
	chunk[m_num_chunks++]=c;

	c->after_init();

	*filepos += c->length + 12;
	return 1;
}

Png::Png()
{
	m_num_chunks=0;
	chunk=NULL;
	m_chunks_alloc=0;
	StringCchCopy(m_filename,MAX_PATH,_T("untitled"));
	m_named=0;
	m_dirty=0;

	chunk=(Chunk**)malloc(200*sizeof(Chunk*));
	m_chunks_alloc=200;
	m_imgtype=IMG_PNG;

	m_width=1;
	m_height=1;
	m_bitdepth=1;
	m_colortype=0;
	//m_compression=0;
	//m_compressionfilter=0;
	//m_interlace=0;

	memcpy(signature,sig_png,8);

	m_valid=1;
}

// The reason for two filename params is to support the plan to add
// support for running "filter" utilities. A filter will accept a
// (temporary) PNG file, and write a new (temporary) PNG file, which
// we will read as a replacement for the original file.
Png::Png(const TCHAR *load_fn, const TCHAR *save_fn)
{
	int okay;
	HANDLE fh;
	DWORD filepos;

	m_valid=0;

	m_num_chunks=0;
	chunk=NULL;
	m_chunks_alloc=0;
	StringCchCopy(m_filename,MAX_PATH,save_fn);
	m_named=1;
	m_dirty=0;

	chunk=(Chunk**)malloc(200*sizeof(Chunk*));
	m_chunks_alloc=200;

	m_colortype=255;  // random invalid value

	fh=CreateFile(load_fn,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,NULL);
	if(fh==INVALID_HANDLE_VALUE) {
		mesg(MSG_E,_T("Can") SYM_RSQUO _T("t open file (%s)"),load_fn);
		return;
	}

	m_pngfilesize=GetFileSize(fh,NULL);  // note, this will fail for files over 4 GB

	m_imgtype=read_signature(fh);
	okay=(m_imgtype>=1);

	if(!okay) {
		CloseHandle(fh);
		SetForegroundWindow(globals.hwndMain);
		mesg(MSG_E,_T("Unrecognized file format\n\nThis is not a valid PNG file."));
		return;
	}

	filepos = 8;

	while(okay) {
		okay=read_next_chunk(fh,&filepos);
	}

	CloseHandle(fh);
	m_valid=1;
}


Png::~Png()
{
	int i;

	// free individual chunks
	for(i=0;i<m_num_chunks;i++) {
		if(chunk[i]) delete chunk[i];
	}

	// free chunk list
	if(chunk) free(chunk);
}


///////////////////////////////////////////////////////////////////

static int RegisterClasses()
{
	WNDCLASS  wc;

	ZeroMemory((void*)&wc, sizeof(WNDCLASS));
	wc.style = CS_DBLCLKS|CS_HREDRAW;
	wc.lpfnWndProc = (WNDPROC)WndProcMain;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = globals.hInst;
	wc.hIcon = LoadIcon(globals.hInst,_T("ICONMAIN"));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(DKGRAY_BRUSH);
	wc.lpszMenuName =  _T("MENUMAIN");
	wc.lpszClassName = _T("TWEAKPNGMAIN");
	if(!RegisterClass(&wc)) {
		return 0;
	}

	wc.style= CS_HREDRAW|CS_VREDRAW;
	wc.lpfnWndProc = (WNDPROC)WndProcEditPal;
	wc.hIcon = NULL;
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName =  NULL;
	wc.lpszClassName = _T("TWEAKPNGEDITPAL");
	if(!RegisterClass(&wc)) {
		return 0;
	}

	return 1;
}


int twpng_StoreWindowPos(HWND hwnd, struct windowpos_struct *q)
{
	WINDOWPLACEMENT wp;

	if(!IsWindow(hwnd)) return 0;
	ZeroMemory(&wp,sizeof(wp));
	wp.length=sizeof(wp);
	if(!GetWindowPlacement(hwnd,&wp)) return 0;
	q->x= wp.rcNormalPosition.left;
	q->y= wp.rcNormalPosition.top;
	q->w= wp.rcNormalPosition.right - q->x;
	q->h= wp.rcNormalPosition.bottom - q->y;
	q->max= (wp.showCmd == SW_SHOWMAXIMIZED);  // this isn't used
	return 1;
}

void twpng_SetWindowPos(HWND hwnd, const struct windowpos_struct *q)
{
	WINDOWPLACEMENT wp;

	// We don't save all the information that SetWindowPlacement requires.
	// I hope it's okay to call GetWindowPlacement to get the current
	// placement information, modify only the position, then turn around
	// and call SetWindowPlacement.

	ZeroMemory(&wp,sizeof(wp));
	wp.length=sizeof(wp);
	GetWindowPlacement(hwnd,&wp);
	wp.rcNormalPosition.left = q->x;
	wp.rcNormalPosition.top = q->y;
	wp.rcNormalPosition.right = q->x + q->w;
	wp.rcNormalPosition.bottom = q->y + q->h;
	SetWindowPlacement(hwnd,&wp);
}

static int SaveSettings()
{
	HKEY key;
	LONG r;
	DWORD disp, tmpd;
	TCHAR v[24];
	int i;

	r=RegCreateKeyEx(HKEY_CURRENT_USER,globals.twpng_reg_key,0,NULL,
		REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,NULL,&key,&disp);
	if(r!=ERROR_SUCCESS) {
		mesg(MSG_E,_T("Cannot save settings to registry"));
		return 0;
	}

	for(i=0;i<TWPNG_NUMTOOLS;i++) {
		StringCchPrintf(v,24,_T("tool%dn"),i);
		r=RegSetValueEx(key,v,0,REG_SZ,(LPBYTE)globals.tools[i].name  ,(DWORD)sizeof(TCHAR)*(1+lstrlen(globals.tools[i].name)));

		StringCchPrintf(v,24,_T("tool%dc"),i);
		r=RegSetValueEx(key,v,0,REG_SZ,(LPBYTE)globals.tools[i].cmd   ,(DWORD)sizeof(TCHAR)*(1+lstrlen(globals.tools[i].cmd)));

		StringCchPrintf(v,24,_T("tool%dp"),i);
		r=RegSetValueEx(key,v,0,REG_SZ,(LPBYTE)globals.tools[i].params,(DWORD)sizeof(TCHAR)*(1+lstrlen(globals.tools[i].params)));
	}

	r=RegSetValueEx(key,_T("deflate_lvl"),0,REG_DWORD,(LPBYTE)&globals.compression_level,sizeof(globals.compression_level));
	r=RegSetValueEx(key,_T("show_viewer"),0,REG_DWORD,(LPBYTE)&globals.autoopen_viewer,sizeof(DWORD));
	r=RegSetValueEx(key,_T("use_gamma"),0,REG_DWORD,(LPBYTE)&globals.use_gamma,sizeof(DWORD));
	r=RegSetValueEx(key,_T("use_custombg"),0,REG_DWORD,(LPBYTE)&globals.use_custombg,sizeof(DWORD));
	r=RegSetValueEx(key,_T("use_imagebg"),0,REG_DWORD,(LPBYTE)&globals.use_imagebg,sizeof(DWORD));
	r=RegSetValueEx(key,_T("windowbg"),0,REG_DWORD,(LPBYTE)&globals.window_bgcolor,sizeof(DWORD));
	r=RegSetValueEx(key,_T("zoom"),0,REG_DWORD,(LPBYTE)&globals.vsize,sizeof(DWORD));

	if(IsWindow(globals.hwndMainList)) {
		for(i=0;i<5;i++) {
			globals.window_prefs.column_width[i]=ListView_GetColumnWidth(globals.hwndMainList,i);
		}
	}

	twpng_StoreWindowPos(globals.hwndMain,&globals.window_prefs.main);

	RegSetValueEx(key,_T("windowpos"),0,REG_BINARY,(LPBYTE)(&globals.window_prefs),sizeof(globals.window_prefs));

	tmpd = (DWORD)globals.custombgcolor;
	RegSetValueEx(key,_T("bgcolor"),0,REG_DWORD,(LPBYTE)&tmpd,sizeof(DWORD));
	RegSetValueEx(key,_T("custcolors"),0,REG_BINARY,(LPBYTE)&globals.custcolors,16*sizeof(COLORREF));

	RegCloseKey(key);

	return 1;
}

static void ReadSettings()
{
	HKEY key;
	LONG r;
	DWORD datasize,tmpd;
	int i;
	TCHAR v[24];
	//prefs_t tmp_prefs;

	globals.compression_level=Z_DEFAULT_COMPRESSION;
	globals.use_gamma=1;
	globals.custombgcolor = RGB(128,128,128);
	globals.use_custombg = 1;
	globals.use_imagebg = 1;
	for(i=0;i<16;i++) globals.custcolors[i] = RGB(0,0,0);
	globals.autoopen_viewer=0;
	globals.window_bgcolor=TWPNG_WBG_SAMEASIMAGE;

	for(i=0;i<TWPNG_NUMTOOLS;i++) {
		StringCchCopy(globals.tools[i].name,MAX_TOOL_NAME,_T(""));
		StringCchCopy(globals.tools[i].cmd,MAX_TOOL_CMD,_T(""));
		StringCchCopy(globals.tools[i].params,MAX_TOOL_PARAMS,_T(""));
	}
	StringCchCopy(globals.tools[0].name,MAX_TOOL_NAME,_T("Default viewer"));
	globals.window_prefs.version = 1;
	globals.window_prefs.column_width[0]=50;
	globals.window_prefs.column_width[1]=60;
	globals.window_prefs.column_width[2]=60;
	globals.window_prefs.column_width[3]=130;
	globals.window_prefs.column_width[4]=600;

	globals.window_prefs.main.x=0;
	globals.window_prefs.main.y=0;
	globals.window_prefs.main.w=600;
	globals.window_prefs.main.h=300;
	globals.window_prefs.main.max=0;

	globals.window_prefs.text.x=0;
	globals.window_prefs.text.y=0;
	globals.window_prefs.text.w=500;
	globals.window_prefs.text.h=300;
	globals.window_prefs.text.max=0;

	globals.window_prefs.plte.x=0;
	globals.window_prefs.plte.y=0;
	globals.window_prefs.plte.w=490;
	globals.window_prefs.plte.h=440;
	globals.window_prefs.plte.max=0;

	globals.window_prefs.viewer.x=0;
	globals.window_prefs.viewer.y=100;
	globals.window_prefs.viewer.w=350;
	globals.window_prefs.viewer.h=250;
	globals.window_prefs.viewer.max=0;

	if(RegOpenKeyEx(HKEY_CURRENT_USER,globals.twpng_reg_key,0,KEY_READ,&key)
		!= ERROR_SUCCESS) return;

	for(i=0;i<TWPNG_NUMTOOLS;i++) {
		StringCchPrintf(v,24,_T("tool%dn"),i); datasize= sizeof(TCHAR)*(MAX_TOOL_NAME-1);
		r=RegQueryValueEx(key,v,NULL,NULL,(LPBYTE)globals.tools[i].name,&datasize);
		globals.tools[i].name[MAX_TOOL_NAME-1]='\0';

		StringCchPrintf(v,24,_T("tool%dc"),i); datasize= sizeof(TCHAR)*(MAX_TOOL_CMD-1);
		r=RegQueryValueEx(key,v,NULL,NULL,(LPBYTE)globals.tools[i].cmd,&datasize);
		globals.tools[i].cmd[MAX_TOOL_CMD-1]='\0';

		StringCchPrintf(v,24,_T("tool%dp"),i); datasize= sizeof(TCHAR)*(MAX_TOOL_PARAMS-1);
		r=RegQueryValueEx(key,v,NULL,NULL,(LPBYTE)globals.tools[i].params,&datasize);
		globals.tools[i].params[MAX_TOOL_PARAMS-1]='\0';
	}

	datasize=sizeof(globals.compression_level);
	r=RegQueryValueEx(key,_T("deflate_lvl"),NULL,NULL,(LPBYTE)&globals.compression_level,&datasize);

	datasize=sizeof(globals.window_prefs);
	r=RegQueryValueEx(key,_T("windowpos"),NULL,NULL,(LPBYTE)(&globals.window_prefs),&datasize);

	datasize=sizeof(DWORD);
	r=RegQueryValueEx(key,_T("show_viewer"),NULL,NULL,(LPBYTE)(&globals.autoopen_viewer),&datasize);

	datasize=sizeof(DWORD);
	r=RegQueryValueEx(key,_T("use_gamma"),NULL,NULL,(LPBYTE)(&globals.use_gamma),&datasize);
	datasize=sizeof(DWORD);
	r=RegQueryValueEx(key,_T("use_custombg"),NULL,NULL,(LPBYTE)(&globals.use_custombg),&datasize);
	datasize=sizeof(DWORD);
	r=RegQueryValueEx(key,_T("use_imagebg"),NULL,NULL,(LPBYTE)(&globals.use_imagebg),&datasize);
	datasize=sizeof(DWORD);
	r=RegQueryValueEx(key,_T("windowbg"),NULL,NULL,(LPBYTE)(&globals.window_bgcolor),&datasize);
	datasize=sizeof(DWORD);
	r=RegQueryValueEx(key,_T("zoom"),NULL,NULL,(LPBYTE)(&globals.vsize),&datasize);

	datasize=sizeof(DWORD);
	r=RegQueryValueEx(key,_T("bgcolor"),NULL,NULL,(LPBYTE)&tmpd,&datasize);
	if(r==ERROR_SUCCESS) {
		globals.custombgcolor = (COLORREF)tmpd;
	}

	datasize = 16*sizeof(COLORREF);
	r=RegQueryValueEx(key,_T("custcolors"),NULL,NULL,(LPBYTE)(&globals.custcolors),&datasize);
	RegCloseKey(key);
}

// Sets globals.file_from_cmdline.
static void get_filename_from_cmdline(const TCHAR *lpCmdLine)
{
	TCHAR buf[MAX_PATH];
	int len;
	DWORD ret;

	if(lpCmdLine[0]=='"') { // if quoted, strip quotes
		StringCbCopy(buf,sizeof(buf),&lpCmdLine[1]);
		len = lstrlen(buf);
		if(len>0 && buf[len-1]=='"') buf[len-1]='\0';
	}
	else {
		StringCbCopy(buf,sizeof(buf),lpCmdLine);
	}

	// Figure out the full filename.
	ret = GetFullPathName(buf,MAX_PATH,globals.file_from_cmdline,NULL);
	if(!ret) {
		StringCchCopy(globals.file_from_cmdline,MAX_PATH,_T(""));
	}
}

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
	MSG msg;
	HACCEL hAccTable;
	int p;

	ZeroMemory(&globals,sizeof(struct globals_struct));

	globals.hInst=hInstance;
	globals.dlgs_open = 0;
	globals.twpng_homepage = TWEAKPNG_HOMEPAGE;
	globals.twpng_reg_key = _T("SOFTWARE\\Generic\\TweakPNG");

#ifdef UNICODE
	globals.unicode_supported=1;
#else
	globals.unicode_supported=0;
#endif

#ifdef TWPNG_HAVE_ZLIB
	globals.zlib_available=1;
#else
	globals.zlib_available=0;
#endif

	globals.stbar_height=0;
	globals.hwndMain=NULL;
	globals.hwndMainList=NULL;
	globals.hwndStBar=NULL;
	png=NULL;
	g_viewer=NULL;
	globals.timer_set=0;
	globals.vborder=4;
	globals.vsize=TWPNG_VS_FIT;
	globals.viewer_correct_nonsquare=1;

	InitCommonControls();

	globals.pngchunk_cf = RegisterClipboardFormat(_T("pngchunks"));

	if(!RegisterClasses()) return 0;
#ifdef TWPNG_SUPPORT_VIEWER
	Viewer::GlobalInit();
#endif
	globals.hcurDrag2 = LoadCursor(globals.hInst,_T("DRAGCURSOR2"));

	StringCchCopy(globals.last_open_dir,MAX_PATH,_T(""));

	get_filename_from_cmdline(lpCmdLine);

	make_crc_table();

	StringCchCopy(globals.orig_dir,MAX_PATH,_T(""));
	StringCchCopy(globals.home_dir,MAX_PATH,_T(""));

	GetCurrentDirectory(MAX_PATH,globals.orig_dir);

	// record the directory we were loaded from
	if(GetModuleFileName(globals.hInst,globals.home_dir,MAX_PATH)) {
		// strip filename, leaving directory path
		p=lstrlen(globals.home_dir)-1;
		while(p>=0) {
			if(globals.home_dir[p]=='\\') {
				globals.home_dir[p]='\0';
				break;
			}
			p--;
		}
	}

	ReadSettings();

	globals.hwndMain = CreateWindow(
		_T("TWEAKPNGMAIN"),_T("TweakPNG"),
		WS_VISIBLE|WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
		CW_USEDEFAULT,CW_USEDEFAULT,
		globals.window_prefs.main.w,globals.window_prefs.main.h,
		NULL,NULL,globals.hInst,NULL);
	if (!globals.hwndMain) return 0;

	DragAcceptFiles(globals.hwndMain,TRUE);
	hAccTable=LoadAccelerators(globals.hInst,_T("ACCELTABLE"));

	while(GetMessage(&msg,NULL,0,0)){
		if (!TranslateAccelerator(globals.hwndMain, hAccTable, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

#ifdef TWPNG_SUPPORT_VIEWER
	Viewer::GlobalDestroy();
#endif
	return (int)msg.wParam;
}

struct filename_path_struct {
	const TCHAR *full_fn;
	const TCHAR *base_fn; // pointer into .full_fn
	TCHAR path[MAX_PATH];
};

// Caller sets fnp->full_fn; we calculate ->path and ->base_fn.
static int parse_filename_path(struct filename_path_struct *fnp)
{
	const TCHAR *sep;
	size_t sep_pos;

	sep = _tcsrchr(fnp->full_fn,'\\');
	if(!sep) {
		// Filename does not include a path
		fnp->base_fn = fnp->full_fn;
		fnp->path[0] = '\0';
		return 0;
	}

	sep_pos = sep - fnp->full_fn; // Pointer arithmetic

	// Make a copy of the full path...
	StringCchCopy(fnp->path,MAX_PATH,fnp->full_fn);
	// and truncate it after the last backslash.
	fnp->path[sep_pos+1] = '\0';

	fnp->base_fn = &fnp->full_fn[sep_pos+1];
	return 1;
}

// set the titlebar text for the main window
static void SetTitle(Png *p)
{
	TCHAR buf[1024];
	int buf_valid = 0;
	const TCHAR *basefn = NULL;
	struct filename_path_struct fnp;
	int ret;

	if(!p) {
		SetWindowText(globals.hwndMain,_T("TweakPNG"));
		return;
	}

	if(p->m_named) {
		// TODO: The filename is parsed more often than necessary.
		fnp.full_fn = p->m_filename;
		ret = parse_filename_path(&fnp);

		if(ret) {
			// Special format for filenames with paths.
			StringCbPrintf(buf,sizeof(buf),_T("%s%s (%s) - TweakPNG"),fnp.base_fn,
				p->m_dirty?_T("*"):_T(""),fnp.path);
			buf_valid = 1;
		}
	}

	if(!buf_valid) {
		StringCbPrintf(buf,sizeof(buf),_T("%s%s - TweakPNG"),p->m_filename,
			p->m_dirty?_T("*"):_T(""));
	}

	SetWindowText(globals.hwndMain,buf);
}

// Unconditionally close the current document, and update the UI.
static void ClosePngDocument()
{
	if(png) {
		delete png;
		png=NULL;
	}
	if(globals.hwndMainList) {
		ListView_DeleteAllItems(globals.hwndMainList);
	}
	SetTitle(NULL);
	update_viewer_filename();
	update_status_bar_and_viewer();
}

// Unconditionally close the current document, and create a new empty document.
static void NewPng()
{
	if(png) {
		delete png;
		png=NULL;
	}
	if(globals.hwndMainList) {
		ListView_DeleteAllItems(globals.hwndMainList);
	}
	png=new Png();
	SetTitle(png);
	update_viewer_filename();
	update_status_bar_and_viewer();
}

// handles loading a new png file
static int OpenPngByName(const TCHAR *fn)
{
	if(png) {
		delete png;
		png=NULL;
	}
	ListView_DeleteAllItems(globals.hwndMainList);
	png=new Png(fn, fn);

	if(!png->m_valid) {
		delete png;
		png=NULL;
		SetTitle(NULL);
		update_viewer_filename();
		update_status_bar_and_viewer();
		return 0;
	}

	png->fill_listbox(globals.hwndMainList);
	SetTitle(png);
	update_viewer_filename();
	update_status_bar_and_viewer();

	return 1;
}

static int OpenPngFromMenu(HWND hwnd)
{
	TCHAR fn[MAX_PATH];
	OPENFILENAME ofn;
	BOOL bRet;

	StringCchCopy(fn,MAX_PATH,_T(""));

	ZeroMemory(&ofn,sizeof(OPENFILENAME));

	ofn.lStructSize=sizeof(OPENFILENAME);
	ofn.hwndOwner=hwnd;
	ofn.hInstance=NULL;
	ofn.lpstrFilter=_T("PNG, MNG, JNG\0*.png;*.mng;*.jng\0PNG\0*.png\0MNG\0*.mng\0JNG\0*.jng\0All files\0*.*\0\0");
	ofn.nFilterIndex=1;
	ofn.lpstrTitle=_T("Open image file");
	if(lstrlen(globals.last_open_dir))
		ofn.lpstrInitialDir=globals.last_open_dir;  // else NULL ==> current dir
	ofn.lpstrFile=fn;
	ofn.nMaxFile=MAX_PATH;
	ofn.Flags=OFN_FILEMUSTEXIST|OFN_HIDEREADONLY|OFN_NOCHANGEDIR;

	globals.dlgs_open++;
	bRet=GetOpenFileName(&ofn);
	globals.dlgs_open--;
	if(!bRet) {
		return 0;  // user canceled
	}

	StringCchCopy(globals.last_open_dir,MAX_PATH,fn);
	globals.last_open_dir[ofn.nFileOffset]='\0'; // chop off filename; save the path for next time

	return OpenPngByName(fn);
}

static void ReopenPngDocument()
{
	TCHAR fn[MAX_PATH];
	int x;

	if(!png) return;
	if(!png->m_named) return;

	if(png->m_dirty) {
		x=MessageBox(globals.hwndMain,_T("Reload and lose changes?"),
			_T("TweakPNG"),MB_OKCANCEL|MB_ICONWARNING|MB_DEFBUTTON1);
		if(x!=IDOK) return;
	}

	// Use a copy of the filename, because OpenPngByName will
	// delete the png object.
	StringCbCopy(fn,sizeof(fn),png->m_filename);
	OpenPngByName(fn);
}

void DroppedFiles(HDROP hDrop)
{
	UINT num_files;
	UINT rv;
	DWORD attr;
	TCHAR fn[MAX_PATH];
	int fnlen;

	// Ask how many files were dropped.
	num_files = DragQueryFile(hDrop,0xFFFFFFFF,NULL,0);
	if(num_files!=1) goto done;

	// Look up the filename of the dropped file.
	rv=DragQueryFile(hDrop,0,fn,MAX_PATH);
	if(!rv) goto done;

	// Don't try to open directories.
	attr = GetFileAttributes(fn);
	if(attr&FILE_ATTRIBUTE_DIRECTORY) goto done;

	// Look at the file extension, to guess what type of file was dropped.
	// We have special handling of .chunk and .icc files.
	fnlen = lstrlen(fn);
	if(fnlen>=7 && !_tcsicmp(&fn[fnlen-6],_T(".chunk"))) {
		// TODO: How to decide where to insert the new chunk?
		int pos=GetLVFocus(globals.hwndMainList);
		ImportChunkByFilename(fn,pos);
	}
	else if(fnlen>=5 && !_tcsicmp(&fn[fnlen-4],_T(".icc"))) {
		ImportICCProfileByFilename(png,fn);
	}
	else {
		// Assume this is a PNG/MNG/JNG file to be opened.
		if(OkToClosePNG()) {
			OpenPngByName(fn);
		}
	}

done:
	DragFinish(hDrop);
}

// Do a few validity checks on the number and ordering of chunks
// sets pointer to string describing the problem. Setting msg to NULL
// makes this function display a dialog box about any errors (or even
// if all is okay)
// This function only checks the overall structure, not parameter values,
// etc.
//
// msgmode==0: prints results (good or bad), returns 1 if okay, 0 if errors found
// msgmode==1: if errors found, asks if you want to save, returns 0 if user says no, otherwise 1
int Png::check_validity(int msgmode)
{
	const TCHAR *m;
	int i;
	int e=0;   // error count
	int t;
	int prev_chunk;
	TCHAR buf[500];

	int idat_seen=0, plte_seen=0, chrm_seen=0, gama_seen=0;
	int iccp_seen=0, sbit_seen=0, srgb_seen=0, bkgd_seen=0;
	int hist_seen=0, trns_seen=0, phys_seen=0, time_seen=0;
	int ster_seen=0, offs_seen=0, pcal_seen=0, scal_seen=0;
	int dsig_pending=0;
	int dsig_nesting_level=0;

	m=_T("No problems found.");

	if(m_imgtype!=IMG_PNG) {
		if(msgmode==0) {
			e++; m=_T("Can only check PNG files");
		}
		goto done;
	}

	i=0;
	prev_chunk=CHUNK_UNKNOWN;

	while(i<m_num_chunks && !e) {
		t=chunk[i]->m_chunktype_id;   // to save typing

		if(chunk[i]->is_critical() && t!=CHUNK_IHDR && t!=CHUNK_PLTE
			&& t!=CHUNK_IDAT && t!=CHUNK_IEND)
		{
			e++; m=_T("Unrecognized critical chunk");
		}

		if(dsig_pending && t!=CHUNK_dSIG) {
			if(t!=CHUNK_IEND) {
				e++; m=_T("Misplaced dSIG chunk");
			}
			dsig_pending=0;
		}

		if(i==0 && t!=CHUNK_IHDR) {
			e++; m=_T("First chunk must be IHDR");
		}
		else if((i==m_num_chunks-1) && t!=CHUNK_IEND) {
			e++; m=_T("Last chunk must be IEND");
		}
		else {
			switch(t) {
			case CHUNK_IHDR:
				if(i!=0) {
					e++; m=_T("Misplaced or extra IHDR");
				}
				break;
			case CHUNK_IEND:
				if(i!=m_num_chunks-1) {
					e++; m=_T("Misplaced or extra IEND");
				}
				break;
			case CHUNK_IDAT:
				if(idat_seen && prev_chunk!=CHUNK_IDAT) {
					e++; m=_T("IDAT chunks must be consecutive");
				}
				idat_seen++;
				break;
			case CHUNK_PLTE:
				if(plte_seen) { e++; m=_T("Multiple PLTE chunks not allowed"); }
				else if(m_colortype==0 || m_colortype==4) {
					e++; m=_T("PLTE chunk not allowed in grayscale image");
				}
				else if(idat_seen) { e++; m=_T("PLTE must appear before IDAT"); }
				else if(bkgd_seen) { e++; m=_T("bKGD must appear after PLTE"); }
				else if(hist_seen) { e++; m=_T("hIST must appear after PLTE"); }
				else if(trns_seen) { e++; m=_T("tRNS must appear after PLTE"); }
				plte_seen++;
				break;
			case CHUNK_tIME:
				if(time_seen) { e++; m=_T("Multiple tIME chunks not allowed"); }
				time_seen++;
				break;
			case CHUNK_cHRM:
				if(chrm_seen) { e++; m=_T("Multiple cHRM chunks not allowed"); }
				else if(plte_seen) { e++; m=_T("cHRM must appear before PLTE"); }
				else if(idat_seen) { e++; m=_T("cHRM must appear before IDAT"); }
				chrm_seen++;
				break;
			case CHUNK_gAMA:
				if(gama_seen) { e++; m=_T("Multiple gAMA chunks not allowed"); }
				else if(plte_seen) { e++; m=_T("gAMA must appear before PLTE"); }
				else if(idat_seen) { e++; m=_T("gAMA must appear before IDAT"); }
				gama_seen++;
				break;
			case CHUNK_iCCP:
				if(iccp_seen) { e++; m=_T("Multiple iCCP chunks not allowed"); }
				else if(plte_seen) { e++; m=_T("iCCP must appear before PLTE"); }
				else if(idat_seen) { e++; m=_T("iCCP must appear before IDAT"); }
				iccp_seen++;
				break;
			case CHUNK_sBIT:
				if(sbit_seen) { e++; m=_T("Multiple sBIT chunks not allowed"); }
				else if(plte_seen) { e++; m=_T("sBIT must appear before PLTE"); }
				else if(idat_seen) { e++; m=_T("sBIT must appear before IDAT"); }
				sbit_seen++;
				break;
			case CHUNK_sRGB:
				if(srgb_seen) { e++; m=_T("Multiple sRGB chunks not allowed"); }
				else if(plte_seen) { e++; m=_T("sRGB must appear before PLTE"); }
				else if(idat_seen) { e++; m=_T("sRGB must appear before IDAT"); }
				srgb_seen++;
				break;
			case CHUNK_sTER:
				if(ster_seen) { e++; m=_T("Multiple sTER chunks not allowed"); }
				else if(idat_seen) { e++; m=_T("sTER must appear before IDAT"); }
				ster_seen++;
				break;
			case CHUNK_bKGD:
				if(bkgd_seen) { e++; m=_T("Multiple bKGD chunks not allowed"); }
				else if(idat_seen) { e++; m=_T("bKGD must appear before IDAT"); }
				bkgd_seen++;
				break;
			case CHUNK_hIST:
				if(hist_seen) { e++; m=_T("Multiple hIST chunks not allowed"); }
				else if(idat_seen) { e++; m=_T("hIST must appear before IDAT"); }
				hist_seen++;
				break;
			case CHUNK_tRNS:
				if(trns_seen) { e++; m=_T("Multiple tRNS chunks not allowed"); }
				else if(idat_seen) { e++; m=_T("tRNS must appear before IDAT"); }
				trns_seen++;
				break;
			case CHUNK_pHYs:
				if(phys_seen) { e++; m=_T("Multiple pHYs chunks not allowed"); }
				else if(idat_seen) { e++; m=_T("pHYs must appear before IDAT"); }
				phys_seen++;
				break;
			case CHUNK_sPLT:
				if(idat_seen) { e++; m=_T("sPLT must appear before IDAT"); }
				break;
			case CHUNK_dSIG:
				if(prev_chunk!=CHUNK_IHDR && prev_chunk!=CHUNK_dSIG) {
					dsig_pending=1; // The next non-dSIG chunk must be IEND
				}
				if(idat_seen) dsig_nesting_level--;
				else dsig_nesting_level++;
				break;
			case CHUNK_oFFs:
				if(offs_seen) { e++; m=_T("Multiple oFFs chunks not allowed"); }
				else if(idat_seen) { e++; m=_T("oFFs must appear before IDAT"); }
				offs_seen++;
				break;
			case CHUNK_pCAL:
				if(pcal_seen) { e++; m=_T("Multiple pCAL chunks not allowed"); }
				else if(idat_seen) { e++; m=_T("pCAL must appear before IDAT"); }
				pcal_seen++;
				break;
			case CHUNK_sCAL:
				if(scal_seen) { e++; m=_T("Multiple sCAL chunks not allowed"); }
				else if(idat_seen) { e++; m=_T("sCAL must appear before IDAT"); }
				scal_seen++;
				break;
			}
		}

		prev_chunk=t;
		i++;
	}

	if(!e) {  // if no errors yet, we'll test some more things
		if(m_num_chunks<1) {
			e++; m=_T("No chunks. Not valid.");
		}
		else if(!idat_seen) {
			e++; m=_T("Required IDAT chunk not found");
		}
		else if(m_colortype==3 && !plte_seen) {
			e++; m=_T("Required PLTE chunk not found");
		}
		else if(dsig_nesting_level!=0) {
			e++; m=_T("Mismatched dSIG chunks");
		}
	}

done:
	if(msgmode==0) {
		MessageBox(globals.hwndMain,m,_T("Validity check"),MB_OK|(e?MB_ICONWARNING:MB_ICONINFORMATION));
		return (!e);
	}
	//else

	if(e) {
		StringCchPrintf(buf,500,_T("A problem was detected with the current file:\n\n%s\n\n")
			_T("Do you want to save it anyway?"),m);
		if(MessageBox(globals.hwndMain,buf,_T("Validity check"),MB_OKCANCEL|MB_ICONWARNING)!=IDOK)
		{
			return 0;
		}
	}
	return 1;

}

// returns 1 if saved, else 0
static int SavePngAs(HWND hwnd)
{
	OPENFILENAME ofn;
	TCHAR fn[MAX_PATH];
	BOOL bRet;

	if(!png->check_validity(1)) return 0;

	StringCchCopy(fn,MAX_PATH,png->m_filename);

	ZeroMemory(&ofn,sizeof(OPENFILENAME));
	ofn.lStructSize=sizeof(OPENFILENAME);
	ofn.hwndOwner=hwnd;

	if(png->m_imgtype==IMG_MNG) {
		ofn.lpstrFilter=_T("MNG (*.mng)\0*.mng\0\0");
		ofn.lpstrDefExt=_T("mng");
	}
	else if(png->m_imgtype==IMG_JNG) {
		ofn.lpstrFilter=_T("JNG (*.jng)\0*.jng\0\0");
		ofn.lpstrDefExt=_T("jng");
	}
	else {
		ofn.lpstrFilter=_T("PNG (*.png)\0*.png\0\0");
		ofn.lpstrDefExt=_T("png");
	}

	ofn.nFilterIndex=1;
	ofn.lpstrTitle=_T("Save Image As...");
	ofn.lpstrFile=fn;
	ofn.nMaxFile=MAX_PATH;
	ofn.Flags=OFN_PATHMUSTEXIST|OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR;

	globals.dlgs_open++;
	bRet = GetSaveFileName(&ofn);
	globals.dlgs_open--;
	if(bRet) {
		if(png->write_file(ofn.lpstrFile)) {
			StringCchCopy(png->m_filename,MAX_PATH,ofn.lpstrFile);
			png->m_named=1;
			png->m_dirty=0;
			SetTitle(png);
			return 1;
		}
	}
	return 0;
}

// returns 1 if saved, else 0
static int SavePng(HWND hwnd)
{
	if(png->m_named) {
		if(!png->check_validity(1)) return 0;
		if(png->write_file(png->m_filename)) {
			png->m_dirty=0;
			SetTitle(png);
			return 1;
		}
		return 0;
	}
	else {
		return SavePngAs(hwnd);
	}
}

static void DeleteChunks()          // delete all selected items
{
	int i;
	int numdeleted=0;
	int firstdeleted;

	firstdeleted= png->m_num_chunks;

	for(i=png->m_num_chunks-1;i>=0;i--) {
		if(ListView_GetItemState(globals.hwndMainList,i,LVIS_SELECTED) & LVIS_SELECTED) {
			png->delete_chunk(i);
			numdeleted++;
			firstdeleted=i;
		}
	}

	if(numdeleted>0) {
		png->fill_listbox(globals.hwndMainList);
	}

	if(firstdeleted<0) firstdeleted=0;
	if(firstdeleted>(png->m_num_chunks-1)) firstdeleted=png->m_num_chunks-1;
	twpng_SetLVSelection(globals.hwndMainList,firstdeleted,1);
	png->modified();
}

// selects a range of items, and sets the focus to the first of that range
void twpng_SetLVSelection(HWND hwnd, int pos1, int num)
{
	int i;

	for(i=pos1;i<pos1+num;i++) {
		ListView_SetItemState(hwnd,i,
			LVIS_SELECTED| ((i==pos1)?LVIS_FOCUSED:0),
			LVIS_SELECTED| ((i==pos1)?LVIS_FOCUSED:0)  );
	}
	ListView_EnsureVisible(hwnd,pos1,FALSE);
}


static void LVUnselectAll(HWND hwnd)
{
	int i,c;

	c=ListView_GetItemCount(hwnd);
	for(i=0;i<c;i++) {
		ListView_SetItemState(hwnd,i,0,LVIS_SELECTED);
	}
}

static int GetLVFocus(HWND hwnd)
{
	int i;
	for(i=0;i<png->m_num_chunks;i++) {
		if(ListView_GetItemState(hwnd,i,LVIS_FOCUSED) & LVIS_FOCUSED) {
			return i;
		}
	}
	return 0;
}	

/* returns the selected item if exactly one item is selected,
 * otherwise returns -1 */
static int GetLVSelection(HWND hwnd)
{
	int i;

	if(ListView_GetSelectedCount(hwnd)!=1) return(-1);

	for(i=0;i<png->m_num_chunks;i++) {
		if(ListView_GetItemState(hwnd,i,LVIS_SELECTED) & LVIS_SELECTED) {
			return i;
		}
	}
	return(-1);
}

static void PasteChunks()
{
	DWORD msize=0;
	HGLOBAL hClip;
	unsigned char* lpClip;
	DWORD p;
	int r,inspos1,inspos,numnewchunks;

	inspos1=inspos=GetLVFocus(globals.hwndMainList);
	numnewchunks=0;

	if(!IsClipboardFormatAvailable(globals.pngchunk_cf)) return;

	OpenClipboard(NULL);
	hClip=GetClipboardData(globals.pngchunk_cf);
	if(hClip) {
		lpClip= (unsigned char*)GlobalLock(hClip);
		if(lpClip) {
			//msize=GlobalSize(hClip);
			msize=read_int32(&lpClip[0]);
			p=4;
			while(p<msize) {
				png->insert_chunks(inspos,1,1);  // this is wrong, I should make a new chunk, then insert it if things go okay
				r= png->chunk[inspos]->init_from_memory(&lpClip[p], msize-p);
				if(!r) break;  // error occurred
				inspos++;
				numnewchunks++;
				p+=r;
			}
			GlobalUnlock(hClip);
			png->fill_listbox(globals.hwndMainList);

			// reselect the new or changed chunks
			twpng_SetLVSelection(globals.hwndMainList,inspos1,numnewchunks);
			png->modified();
		}
	}
	CloseClipboard();
}

// returns 1 if any chunks were selected, and successfully copied
// otherwize 0
static int CopyChunks()
{
	int i;
	DWORD msize=0;
	HGLOBAL hClip;
	unsigned char* lpClip;
	int p;

	for(i=0;i<png->m_num_chunks;i++) {
		if(ListView_GetItemState(globals.hwndMainList,i,LVIS_SELECTED) & LVIS_SELECTED) {
			msize+= 12+png->chunk[i]->length;
		}
	}
	if(msize<1) return 0;  // nothing selected
	msize+=4;

	hClip=GlobalAlloc(GMEM_MOVEABLE|GMEM_DDESHARE,msize);
	lpClip= (unsigned char*)GlobalLock(hClip);

	// I don't know how to figure out the size of the clipboard data
	// when retrieving it. Am I just stupid?
	// GlobalSize() often returns a larger than actual size ...
	// As a workaround, the first 4 bytes of the clipboard data
	// will contain the total length (including those 4 bytes)

	write_int32(&lpClip[0],msize);
	p=4;
	for(i=0;i<png->m_num_chunks;i++) {
		if(ListView_GetItemState(globals.hwndMainList,i,LVIS_SELECTED) & LVIS_SELECTED) {
			png->chunk[i]->copy_to_memory((unsigned char*)&lpClip[p]);
			p+= 12+png->chunk[i]->length;
		}
	}
	GlobalUnlock(hClip);

	OpenClipboard(NULL);
	EmptyClipboard();
	SetClipboardData(globals.pngchunk_cf,hClip);
	CloseClipboard();
	return 1;
}

static void CutChunks()
{
	if(CopyChunks()) {
		DeleteChunks();
	}
}

static void ImportChunkByFilename(const TCHAR *fn, int pos)
{
	HANDLE fh;
	Chunk *c;
	DWORD n;

	fh=CreateFile(fn,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,NULL);
	if(fh==INVALID_HANDLE_VALUE) {
		mesg(MSG_E,_T("Can") SYM_RSQUO _T("t open file (%s)"),fn);
		return;
	}

	c=new Chunk;
	c->length=GetFileSize(fh,NULL)-4;
	ReadFile(fh,(LPVOID)c->m_chunktype_ascii,4,&n,NULL);
	c->m_chunktype_ascii[4]='\0';
	c->set_chunktype_tchar_from_ascii();
	c->data=(unsigned char*)malloc(c->length);
	ReadFile(fh,(LPVOID)c->data,c->length,&n,NULL);
	CloseHandle(fh);

	c->m_parentpng=png;
	c->after_init();
	c->chunkmodified();


	png->insert_chunks(pos,1,0);
	png->chunk[pos]=c;

	png->fill_listbox(globals.hwndMainList);
	png->modified();
	twpng_SetLVSelection(globals.hwndMainList,pos,1);
}

static void ImportChunk()
{
	OPENFILENAME ofn;
	TCHAR fn[MAX_PATH];
	int pos;
	BOOL bRet;

	pos=GetLVFocus(globals.hwndMainList);

	StringCchCopy(fn,MAX_PATH,_T(""));
	ZeroMemory(&ofn,sizeof(OPENFILENAME));

	ofn.lStructSize=sizeof(OPENFILENAME);
	ofn.hwndOwner=globals.hwndMain;
	ofn.hInstance=NULL;
	ofn.lpstrFilter=_T("*.chunk\0*.chunk\0*.*\0*.*\0\0");
	ofn.nFilterIndex=1;
	ofn.lpstrTitle=_T("Import chunk...");
//	if(strlen(last_open_dir))
//		ofn.lpstrInitialDir=last_open_dir;  // else NULL ==> current dir
	ofn.lpstrFile=fn;
	ofn.nMaxFile=MAX_PATH;
	ofn.Flags=OFN_FILEMUSTEXIST|OFN_HIDEREADONLY|OFN_NOCHANGEDIR;

	globals.dlgs_open++;
	bRet = GetOpenFileName(&ofn);
	globals.dlgs_open--;
	if(!bRet) return;

	ImportChunkByFilename(fn, pos);
}

static void ExportChunk()
{
	OPENFILENAME ofn;
	TCHAR fn[MAX_PATH];
	HANDLE fh;
	int n;
	BOOL bRet;

	n=GetLVSelection(globals.hwndMainList);
	if(n<0) {
		mesg(MSG_I,_T("You must select a single chunk first"));
		return;
	}

	StringCchCopy(fn,MAX_PATH,_T(""));

	ZeroMemory(&ofn,sizeof(OPENFILENAME));
	ofn.lStructSize=sizeof(OPENFILENAME);
	ofn.hwndOwner=globals.hwndMain;
	ofn.lpstrFilter=_T("*.chunk\0*.chunk\0*.*\0*.*\0\0");
	ofn.nFilterIndex=1;
	ofn.lpstrTitle=_T("Export chunk to file...");
	ofn.lpstrFile=fn;
	ofn.nMaxFile=MAX_PATH;
	ofn.Flags=OFN_PATHMUSTEXIST|OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR;
	ofn.lpstrDefExt=_T("dat");

	globals.dlgs_open++;
	bRet = GetSaveFileName(&ofn);
	globals.dlgs_open--;
	if(bRet) {
		fh=CreateFile(ofn.lpstrFile,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,NULL);
		if(fh==INVALID_HANDLE_VALUE) {
			mesg(MSG_E,_T("Can") SYM_RSQUO _T("t create file"));
		}
		else {
			png->chunk[n]->write_to_file(fh,1);
			CloseHandle(fh);
		}
	}
}

static void MoveChunkUp()
{
	int i;

	if(ListView_GetSelectedCount(globals.hwndMainList)<1) return;

	// mark selected chunks so we can reselect them later
	for(i=0;i<png->m_num_chunks;i++) {
		png->chunk[i]->m_flag= (ListView_GetItemState(globals.hwndMainList,i,LVIS_SELECTED) & LVIS_SELECTED)?1:0;
	}

	// can't move up if first is selected
	if(png->chunk[0]->m_flag) return;

	for(i=1;i<png->m_num_chunks;i++) {
		if(png->chunk[i]->m_flag) {
			png->move_chunk(i,-1);
		}
	}
		
	png->fill_listbox(globals.hwndMainList);

	for(i=png->m_num_chunks-1;i>=0;i--) {   // reselect moved chunks
		if(png->chunk[i]->m_flag) twpng_SetLVSelection(globals.hwndMainList,i,1);
	}

	png->modified();
}


static void MoveChunkDown()
{
	int i;

	if(ListView_GetSelectedCount(globals.hwndMainList)<1) return;

	// mark selected chunks so we can reselect them later
	for(i=0;i<png->m_num_chunks;i++) {
		png->chunk[i]->m_flag= (ListView_GetItemState(globals.hwndMainList,i,LVIS_SELECTED) & LVIS_SELECTED)?1:0;
	}

	// can't move down if last is selected
	if(png->chunk[png->m_num_chunks-1]->m_flag) return;

	for(i=png->m_num_chunks-1;i>=0;i--) {
		if(png->chunk[i]->m_flag) {
			png->move_chunk(i,1);
		}
	}
		
	png->fill_listbox(globals.hwndMainList);

	for(i=0;i<png->m_num_chunks;i++) {   // reselect moved chunks
		if(png->chunk[i]->m_flag) twpng_SetLVSelection(globals.hwndMainList,i,1);
	}

	png->modified();
}


// split chunk n at size ssize (and repeat if repeat==1)
int Png::split_idat(int n, int ssize, int repeat)
{
	Chunk *c;
	int new_chunks;
	int i;
	int bytes_used;
	int thissize;
	TCHAR buf[200];

	c=chunk[n];

	// how many chunks will there be after the split
	if(!repeat) {
		new_chunks=2;
	}
	else {
		new_chunks= (c->length + ssize-1) / ssize;
	}

	if(new_chunks>100) {
		StringCchPrintf(buf,200,_T("This will create %d new chunks. Are you sure you want to continue?"),new_chunks);
		i=MessageBox(globals.hwndMain,buf,_T("Warning"),MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2);
		if(i!=IDYES) return 0;
	}

	chunk[n]=new Chunk;

	insert_chunks(n,new_chunks-1,1);  // -1 because we start with one already
	for(i=n;i<n+new_chunks;i++) {
		chunk[i]->m_parentpng=png;
		StringCchCopyA(chunk[i]->m_chunktype_ascii,5,c->m_chunktype_ascii);
		StringCchCopy(chunk[i]->m_chunktype_tchar,5,c->m_chunktype_tchar);
	}

	bytes_used=0;

	for(i=n;i<n+new_chunks;i++) {
		if(i<n+new_chunks-1) {
			thissize=ssize;
		}
		else {
			thissize= c->length - bytes_used;  // == all remaining bytes
		}

		chunk[i]->length = thissize;
		if(thissize>0) {
			chunk[i]->data = (unsigned char*)malloc(thissize);
			memcpy((void*)chunk[i]->data,(void*)&c->data[bytes_used],thissize);
		}
		else {
			chunk[i]->data = NULL;
		}

		bytes_used += thissize;
	}

	delete c;

	for(i=n;i<n+new_chunks;i++) {
		chunk[i]->after_init();
		chunk[i]->chunkmodified();
	}
	modified();
	fill_listbox(globals.hwndMainList);

	// reselect the new or changed chunks
	twpng_SetLVSelection(globals.hwndMainList,n,new_chunks);

	return 1;
}

static void SplitIDAT()
{
	int n,id;
	
	n=GetLVSelection(globals.hwndMainList);
	if(n<0) {
		mesg(MSG_I,_T("Select a single IDAT chunk first"));
		return;
	}

	id=png->chunk[n]->m_chunktype_id;

	if(id!=CHUNK_IDAT && id!=CHUNK_JDAT) {
		mesg(MSG_I,_T("Select an IDAT chunk first"));
		return;
	}

	png->chunk[n]->m_index=n;
	globals.dlgs_open++;
	DialogBoxParam(globals.hInst,_T("DLG_SPLITIDAT"),globals.hwndMain,DlgProcSplitIDAT,
		(LPARAM)png->chunk[n]);
	globals.dlgs_open--;

}

static void CombineIDAT_range(int first, int last)
{
	DWORD len,pos;
	int i;
	unsigned char *newdata;
	Chunk *c;

	// calculate total length of data in new IDAT chunk
	len=0;
	for(i=first;i<=last;i++) {
		len+=png->chunk[i]->length;
	}

	newdata= (unsigned char*)malloc(len);
	if(!newdata) {
		mesg(MSG_S,_T("Can") SYM_RSQUO _T("t allocate memory"));
		return;
	}

	pos=0;
	for(i=first;i<=last;i++) {
		if(png->chunk[i]->length>0) {
			memcpy((void*)&newdata[pos],
				(void*)png->chunk[i]->data,png->chunk[i]->length);
			pos+=png->chunk[i]->length;
		}
	}

	c=new Chunk;
	c->m_parentpng = png;
	c->data=newdata;
	c->length=len;
	StringCchCopyA(c->m_chunktype_ascii,5,png->chunk[first]->m_chunktype_ascii);
	StringCchCopy(c->m_chunktype_tchar,5,png->chunk[first]->m_chunktype_tchar);
	c->after_init();

	c->chunkmodified();    // set crc

	for(i=first;i<=last;i++) {
		delete png->chunk[i];
	}

	png->chunk[first]=c;

	// we're deleting  (last-first) chunks
	for(i=first+1;i<=png->m_num_chunks-(last-first+1);i++) {
		png->chunk[i] = png->chunk[i+(last-first)];
	}

	png->m_num_chunks -= (last-first);
}

static void CombineIDAT_selected()
{
	int i, first, last;
	int idat_count, jdat_count;
	int chunktype;

	first= -1; last= -1;
	idat_count=0;
	jdat_count=0;

	for(i=0;i<png->m_num_chunks;i++) {

		if(ListView_GetItemState(globals.hwndMainList,i,LVIS_SELECTED) & LVIS_SELECTED) {
			chunktype = png->chunk[i]->m_chunktype_id;
			if(chunktype==CHUNK_IDAT) {
				idat_count++;
			}
			else if(chunktype==CHUNK_JDAT) {
				jdat_count++;
			}
			else {
				mesg(MSG_E,_T("Only IDAT chunks may be selected"));
				return;
			}
			if(first == -1) {
				first = i;
			}
			else if(last != -1) {
				mesg(MSG_E,_T("Selected chunks must be consecutive"));
				return;
			}
		}
		else {    // not selected
			if(first != -1) {
				if(last == -1) {
					last = i-1;
				}
			}
		}
	}

	if(first == -1) {
		mesg(MSG_E,_T("Must select IDAT chunks first"));
		return;
	}

	if(last == -1) last=png->m_num_chunks-1;

	if(idat_count>0 && jdat_count>0) {
		mesg(MSG_E,_T("Selected chunks must be the same type"));
		return;
	}

	if(last<=first) {
		mesg(MSG_E,_T("Must select more than one IDAT chunk"));
		return;
	}

	CombineIDAT_range(first,last);

	png->fill_listbox(globals.hwndMainList);
	twpng_SetLVSelection(globals.hwndMainList,first,1);

	png->modified();
}

// Locate the first run of 2 or more IDAT or JDAT chunks.
// Returns 0 if not found.
static int find_IDAT_range(int *pfirst, int *plast)
{
	int prevchunktype = CHUNK_UNKNOWN;
	int chunktype = CHUNK_UNKNOWN;
	int i;
	int in_run = 0;


	for(i=0;i<png->m_num_chunks;i++) {
		chunktype = png->chunk[i]->m_chunktype_id;
		if(in_run) {
			if(chunktype==prevchunktype) {
				// Countinuing a run
				*plast = i;
			}
			else {
				// End of a run
				*plast = i-1;
				return 1;
			}

		}
		else {
			if(chunktype==prevchunktype && (chunktype==CHUNK_IDAT || chunktype==CHUNK_JDAT)) {
				// Found the start of a run.
				in_run = 1;
				*pfirst = i-1;
			}
		}
		prevchunktype = chunktype;
	}

	if(in_run)
		return 1;
	return 0;
}

static void CombineIDAT_all()
{
	int first, last;
	int ret;
	int num_idat;
	int num_ranges;
	int i;
	int x;

	// Count number if IDAT/JDAT chunks, and complain if there aren't any.
	num_idat = 0;
	for(i=0;i<png->m_num_chunks;i++) {
		if(png->chunk[i]->m_chunktype_id==CHUNK_IDAT ||
			png->chunk[i]->m_chunktype_id==CHUNK_JDAT)
		{
			num_idat++;
		}
	}

	if(num_idat<1) {
		mesg(MSG_E,_T("No IDAT chunks present"));
		return;
	}

	// Find all ranges of 2 or more IDAT/JDAT chunks, and combine them.
	num_ranges = 0;
	while(1) {
		first= -1; last= -1;
		ret = find_IDAT_range(&first,&last);
		if(!ret) break;
		CombineIDAT_range(first,last);
		num_ranges++;
	}

	// If we made any changes, refresh the list.
	if(num_ranges>0) {
		png->fill_listbox(globals.hwndMainList);
		twpng_SetLVSelection(globals.hwndMainList,first,1);

		png->modified();
	}

	// Select all IDAT and JDAT chunks
	for(i=0;i<png->m_num_chunks;i++) {
		if(png->chunk[i]->m_chunktype_id==CHUNK_IDAT ||
			png->chunk[i]->m_chunktype_id==CHUNK_JDAT)
		{
			x = LVIS_SELECTED;
		}
		else {
			x = 0;
		}

		ListView_SetItemState(globals.hwndMainList,i,x,LVIS_SELECTED);
	}
}

static void DblClickOnList()
{
	int s;

	s=GetLVSelection(globals.hwndMainList);
	if(s>=0 && s<png->m_num_chunks) {
		png->edit_chunk(s);
	}
}

static int CreateMainWindows(HWND hwnd)
{
	LV_COLUMN lvc;
	RECT r;
	DWORD style;
	TCHAR textbuf[50];

	globals.hwndStBar=CreateStatusWindow(WS_CHILD|WS_VISIBLE,_T(""),hwnd,ID_STBAR);
	if(!globals.hwndStBar) {
		mesg(MSG_S,_T("Cannot create status bar"));
		return 0;
	}

	GetWindowRect(globals.hwndStBar,&r);
	globals.stbar_height=r.bottom-r.top;

	GetClientRect(hwnd,&r);

	globals.hwndMainList=CreateWindowEx(WS_EX_CLIENTEDGE,
		WC_LISTVIEW, _T("main listview"),
		WS_VISIBLE|WS_CHILD|LVS_REPORT|LVS_SHOWSELALWAYS|LVS_NOSORTHEADER,
		r.left, r.top, r.right-r.left, r.bottom-r.top-globals.stbar_height,
		hwnd,NULL,globals.hInst,NULL);
	if(!globals.hwndMainList) {
		mesg(MSG_S,_T("Cannot create listview control"));
		return 0;
	}

	style=ListView_GetExtendedListViewStyle(globals.hwndMainList);
	ListView_SetExtendedListViewStyle(globals.hwndMainList,style|LVS_EX_FULLROWSELECT);

	ZeroMemory((void*)&lvc, sizeof(LV_COLUMN));
	lvc.mask= LVCF_FMT | LVCF_TEXT | LVCF_WIDTH;
    lvc.cchTextMax=0;
	lvc.iSubItem=0;

	StringCbCopy(textbuf, sizeof(textbuf), _T("Chunk"));
	lvc.pszText=textbuf;
	lvc.cx= globals.window_prefs.column_width[0];
	lvc.fmt= LVCFMT_LEFT;
	ListView_InsertColumn(globals.hwndMainList,0,&lvc);

	StringCbCopy(textbuf, sizeof(textbuf), _T("Length"));
	lvc.pszText=textbuf;
	lvc.cx= globals.window_prefs.column_width[1];
	lvc.fmt= LVCFMT_RIGHT;
	ListView_InsertColumn(globals.hwndMainList,1,&lvc);

	StringCbCopy(textbuf, sizeof(textbuf), _T("CRC"));
	lvc.pszText=textbuf;
	lvc.cx= globals.window_prefs.column_width[2];
	lvc.fmt= LVCFMT_LEFT;
	ListView_InsertColumn(globals.hwndMainList,2,&lvc);

	StringCbCopy(textbuf, sizeof(textbuf), _T("Attributes"));
	lvc.pszText=textbuf;
	lvc.cx= globals.window_prefs.column_width[3];
	lvc.fmt= LVCFMT_LEFT;
	ListView_InsertColumn(globals.hwndMainList,3,&lvc);

	StringCbCopy(textbuf, sizeof(textbuf), _T("Contents"));
	lvc.pszText=textbuf;
	lvc.cx= globals.window_prefs.column_width[4];
	lvc.fmt= LVCFMT_LEFT;
	ListView_InsertColumn(globals.hwndMainList,4,&lvc);

	return 1;
}

#define TWPNG_TPARAMS_EMPTY               0
#define TWPNG_TPARAMS_VIEWER_NOREPLACE    1
#define TWPNG_TPARAMS_VIEWER_REPLACE      2
#define TWPNG_TPARAMS_FILTER              3

// Evaluates a tool's "param" string.
// Returns the first of these which applies:
//   TWPNG_TPARAMS_FILTER           if s contains %2
//   TWPNG_TPARAMS_VIEWER_REPLACE   if s contains %1
//   TWPNG_TPARAMS_VIEWER_NOREPLACE if s is nonempty
//   TWPNG_TPARAMS_EMPTY            if s is empty
static int check_tool_param_string(const TCHAR *s)
{
	int i;
	int found_p1=0;
	int found_p2=0;
	int nonempty=0;
	TCHAR prevchar = ' ';

	for(i=0;s[i];i++) {
		if(s[i]=='1') {
			if(prevchar=='%') found_p1=1;
		}
		else if(s[i]=='2') {
			if(prevchar=='%') found_p2=1;
		}
		if(s[i]!=' ') nonempty=1;
		prevchar=s[i];
	}
	if(!nonempty) return TWPNG_TPARAMS_EMPTY;
	if(found_p2) return TWPNG_TPARAMS_FILTER;
	if(found_p1) return TWPNG_TPARAMS_VIEWER_REPLACE;
	return TWPNG_TPARAMS_VIEWER_NOREPLACE;
}

static void twpng_ReplaceParams(const TCHAR *src,
	TCHAR *dst, int dstlen, const TCHAR *param1, const TCHAR *param2)
{
	int srcpos,dstpos;
	int param1len, param2len;
	TCHAR prevchar=' ';

	param1len = lstrlen(param1);
	param2len = lstrlen(param2);

	dstpos=0;
	for(srcpos=0;src[srcpos];srcpos++) {
		if(dstpos>=dstlen-10) goto done;
		if(src[srcpos]=='1' && prevchar=='%') {
			dstpos--; // overwrite the '%'
			if(dstpos+param1len<dstlen-10) {
				StringCchCopy(&dst[dstpos],dstlen,param1);
				dstpos+=param1len;
			}
		}
		else if(src[srcpos]=='2' && prevchar=='%') {
			dstpos--;
			if(dstpos+param2len<dstlen-10) {
				StringCchCopy(&dst[dstpos],dstlen,param2);
				dstpos+=param2len;
			}
		}
		else {
			dst[dstpos++]=src[srcpos];
		}
		prevchar=src[srcpos];
	}
done:
	dst[dstpos]='\0';

}

static int twpng_FileExists(const TCHAR *fn)
{
	DWORD ret = GetFileAttributes(fn);
	return !(ret==INVALID_FILE_ATTRIBUTES);
}

// Returns 0 on failure, nonzero on apparent success.
static int twpng_RunFilter(struct tools_t_struct *t, const TCHAR *infn, const TCHAR *outfn)
{
	TCHAR parambuf1[1000];
	TCHAR parambuf2[1500];
	const TCHAR *app;
	BOOL b;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	DWORD ret;
	int retval=0;

	if(lstrlen(t->cmd)) {
		app=t->cmd;
		twpng_ReplaceParams(t->params,parambuf1,1000,infn,outfn);
		StringCbPrintf(parambuf2,sizeof(parambuf2),_T("\"%s\" %s"),app,parambuf1);
	}
	else {
		app=NULL;
		twpng_ReplaceParams(t->params,parambuf2,1000,infn,outfn);
	}

	ZeroMemory(&si,sizeof(STARTUPINFO));
	ZeroMemory(&pi,sizeof(PROCESS_INFORMATION));
	si.cb = sizeof(STARTUPINFO);
	b = CreateProcess(app,parambuf2,NULL,NULL,FALSE,0,NULL,NULL,&si,&pi);
	if(b) {
		ret = WaitForSingleObject(pi.hProcess,INFINITE); 
		retval=1;
	}

	// Are we supposed to close these even if CreateProcess failed?
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return retval;
}

// n is the tool number (0-5)
static void RunTool(int n)
{
	TCHAR tmpdirname[MAX_PATH+2];
	TCHAR tmpname[MAX_PATH+2];
	TCHAR filt_infn[MAX_PATH+2];
	TCHAR filt_outfn[MAX_PATH+2];
	TCHAR parambuf[1000];
	HINSTANCE hInst;
	unsigned __int64 h;
	int ret;
	int paramstype;
	const TCHAR *ext;

	if(n<0 || n>=TWPNG_NUMTOOLS) return;
	if(!lstrlen(globals.tools[n].name)) {
		mesg(MSG_E,_T("Tool %d not defined"),n+1);
		return;
	}

	// Figure out what kind of tool this is, etc.
	paramstype=check_tool_param_string(globals.tools[n].params);

	SetCurrentDirectory(globals.home_dir);

	if(!GetTempPath(MAX_PATH,tmpdirname)) {
		//mesg(MSG_S,"Can't find a temp directory");
		//return;
		StringCchCopy(tmpdirname,MAX_PATH+2,_T(""));
	}

	// make sure pathname ends in a backslash or is empty
	if(lstrlen(tmpdirname)) {
		if(tmpdirname[lstrlen(tmpdirname)-1] != '\\') StringCchCat(tmpdirname,MAX_PATH+2,_T("\\"));
	}

	if(png->m_imgtype==IMG_MNG) { ext = _T("mng"); }
	else if(png->m_imgtype==IMG_JNG) { ext = _T("jng"); }
	else { ext = _T("png"); }

	if(paramstype==TWPNG_TPARAMS_FILTER) {
		Png *newpng;

		StringCchPrintf(filt_infn,MAX_PATH+2,_T("%stwptemp1.%s"),tmpdirname,ext);
		StringCchPrintf(filt_outfn,MAX_PATH+2,_T("%stwptemp2.%s"),tmpdirname,ext);

		if(!png->write_file(filt_infn)) {
			goto done;
		}

		// Make sure we don't get confused by a preexisting output file.
		DeleteFile(filt_outfn);

		ret=twpng_RunFilter(&globals.tools[n],filt_infn,filt_outfn);
		DeleteFile(filt_infn);
		if(!ret) {
			mesg(MSG_E,_T("Cannot execute filter tool"));
			DeleteFile(filt_outfn);
			goto done;
		}

		if(!twpng_FileExists(filt_outfn)){
			mesg(MSG_E,_T("Filter failed to produce an output file"));
			goto done;
		}

		newpng = new Png(filt_outfn,png->m_filename);
		if(newpng->m_valid) {
			// Replace current Png object with new one.
			if(png) delete png;
			png = newpng;

			png->fill_listbox(globals.hwndMainList);
			png->modified();
		}
		else {
			mesg(MSG_E,_T("Filter produced an invalid output file"));
			delete newpng;
		}

		DeleteFile(filt_outfn);
	}
	else { // Tool is a viewer
		StringCchPrintf(tmpname  ,MAX_PATH+2,_T("%stemp$$$$.%s"),tmpdirname,ext);
		if(!png->write_file(tmpname)) {
			goto done;
		}

		if(lstrlen(globals.tools[n].cmd)) {
			if(paramstype==TWPNG_TPARAMS_VIEWER_REPLACE) {
				// Replace "%1" with tmpname
				twpng_ReplaceParams(globals.tools[n].params,parambuf,1000,tmpname,_T(""));
			}
			else if(paramstype==TWPNG_TPARAMS_VIEWER_NOREPLACE) {
				// params doesn't contain '%1': Append tmpname to it.
				StringCbPrintf(parambuf,sizeof(parambuf),_T("%s %s"),globals.tools[n].params,tmpname);
			}
			else { // paramstype==TWPNG_TPARAMS_EMPTY
				// params is empty: Use tmpname as the only param.
				StringCbCopy(parambuf,sizeof(parambuf),tmpname);
			}

			hInst = ShellExecute(globals.hwndMain,_T("open"),globals.tools[n].cmd,parambuf,NULL,SW_SHOWNORMAL);
			h = (unsigned __int64)hInst;
			if(h <= 32) {
				mesg(MSG_E,_T("Cannot execute tool (error %u)"), (unsigned int)h);
			}
		}
		else {
			// If no application name was given, use the default PNG opener.
			hInst = ShellExecute(globals.hwndMain,_T("open"),tmpname,NULL,NULL,SW_SHOWNORMAL);
			h = (unsigned __int64)hInst;
			if(h <= 32) {
				mesg(MSG_E,_T("Cannot execute tool (error %u)"), (unsigned int)h);
			}
		}
	}

done:
	SetCurrentDirectory(globals.orig_dir);
}

// hwnd is the window to attach the menu to, x,y is the mouse position
static void ContextMenu(HWND hwnd, int x, int y)
{
	int sel;
	int cmd;
	HMENU menu;
	int selected_chunk_index=0;
	int can_edit_selected_chunk=0;

	if(!png) return;

	sel=ListView_GetSelectedCount(globals.hwndMainList);
	if(sel<1) return;

	if(sel==1) {
		// Exactly one chunk is currently selected.
		selected_chunk_index=GetLVSelection(globals.hwndMainList);
		if(selected_chunk_index>=0 && selected_chunk_index<png->m_num_chunks) {
			if(png->chunk[selected_chunk_index]->can_edit()) {
				can_edit_selected_chunk=1;
			}
		}
	}

	if(x == -1 && y == -1) {
		// Special case; most likely the user pressed the Menu key.
		POINT tmppt;

		tmppt.x = 4;
		tmppt.y = 4;
		ClientToScreen(globals.hwndMainList,&tmppt);
		x = tmppt.x;
		y = tmppt.y;
	}

	menu=CreatePopupMenu();

	// FIXME: Don't enable "Edit" for chunks that aren't editable.
	AppendMenu(menu,can_edit_selected_chunk?MF_ENABLED:MF_GRAYED,ID_EDITCHUNK,_T("&Edit Chunk..."));
	AppendMenu(menu,MF_ENABLED,ID_DELCHUNK,_T("&Delete"));
	AppendMenu(menu,MF_ENABLED,ID_MOVEUP,_T("Move &Up"));
	AppendMenu(menu,MF_ENABLED,ID_MOVEDOWN,_T("Mo&ve Down"));
	AppendMenu(menu,MF_SEPARATOR,0,NULL);
	AppendMenu(menu,MF_ENABLED,ID_CUT,_T("Cu&t"));
	AppendMenu(menu,MF_ENABLED,ID_COPY,_T("&Copy"));
	AppendMenu(menu,MF_ENABLED,ID_PASTE,_T("&Paste"));

	cmd=TrackPopupMenuEx(menu, TPM_LEFTALIGN|TPM_TOPALIGN|TPM_NONOTIFY|TPM_RETURNCMD|
		TPM_RIGHTBUTTON,x,y,hwnd,NULL);
	DestroyMenu(menu);

	switch(cmd) {
	case ID_EDITCHUNK:    png->edit_chunk(selected_chunk_index);  return;
	case ID_DELCHUNK:     DeleteChunks();    return;
	case ID_MOVEUP:       MoveChunkUp();     return;
	case ID_MOVEDOWN:     MoveChunkDown();   return;
	case ID_COPY:   CopyChunks();    return;
	case ID_CUT:    CutChunks();     return;
	case ID_PASTE:  PasteChunks();   return;
	}
}

static int OkToClosePNG()
{
	TCHAR buf[500];
	int x,ret;

	if(!png) return 1;

	if(png->m_dirty) {
		SetForegroundWindow(globals.hwndMain);
		StringCchPrintf(buf,500,_T("Save changes to %s?"),png->m_filename);
		x=MessageBox(globals.hwndMain,buf,_T("TweakPNG"),MB_YESNOCANCEL|MB_ICONWARNING|MB_DEFBUTTON3);
		if(x==IDYES) {
			ret=SavePng(globals.hwndMain);
			return ret?1:0;
		}
		else if(x==IDNO) {
			return 1;
		}
		else if(x==IDCANCEL) {
			return 0;
		}
		return 0;
	}
	else {
		return 1; // not modified
	}
}

void update_viewer()
{
#ifdef TWPNG_SUPPORT_VIEWER
	if(!g_viewer) return;

	if(!png) {
		g_viewer->Update(NULL);
		return;
	}

	if(png->m_num_chunks < 1) {
		g_viewer->Update(NULL);
		return;
	}

	g_viewer->Update(png);
#endif
}

struct prefs_dlg_ctx {
	int remember_menusetting;
};

static LRESULT CALLBACK WndProcMain(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	RECT r;
	int i;
	TCHAR buf[150];

	id=LOWORD(wParam);

	switch(msg) {
	case WM_ERASEBKGND:
		return 1;

	case WM_CREATE:
		globals.hwndMain=hwnd;

		// create the listview control, statusbar, etc.
		if(!CreateMainWindows(hwnd)) return -1;  // abort program
			
		if(lstrlen(globals.file_from_cmdline)) {
			OpenPngByName(globals.file_from_cmdline);
		}
		if(globals.autoopen_viewer) {
			PostMessage(hwnd,WM_COMMAND,ID_IMGVIEWER,(LPARAM)0);
		}
		return 0;

	case WM_CLOSE:
		// close requested e.g. by user clicking on 'x' icon.
		if(OkToClosePNG()) break;
		return 0; // handle this message --> will not be closed

	case WM_DESTROY:
		if(globals.timer_set) {
			KillTimer(hwnd,1);
			globals.timer_set=0;
		}
		SaveSettings();
		if(png) delete png;
		png=NULL;
		PostQuitMessage(0);
		return 0;

	case WM_SETFOCUS:
		if(globals.hwndMainList) {
			SetFocus(globals.hwndMainList);
			return 0;
		}
		break;

	case WM_CONTEXTMENU:
		ContextMenu(hwnd,((int)(short)LOWORD(lParam)),((int)(short)HIWORD(lParam)));
		return 0;

	case WM_SIZE:
		GetClientRect(hwnd,&r);
		SetWindowPos(globals.hwndStBar,NULL,  // this is probably not the right way to do this...
			r.bottom-globals.stbar_height,r.left,r.right,r.bottom,
			SWP_NOZORDER);
		SetWindowPos(globals.hwndMainList,NULL,
			r.left,r.top,r.right-r.left,r.bottom-r.top-globals.stbar_height,
			SWP_NOZORDER);
		return 0;

	case WM_DROPFILES:
		DroppedFiles((HDROP)wParam);
		return 0;

	case WM_TIMER:
		if(globals.timer_set) {
			KillTimer(hwnd,1);
			globals.timer_set=0;
		}
		update_viewer();
		return 0;

	case WM_INITMENU:
		{
			HMENU m, mtools;
			UINT x;
			int sel;

			// commands requiring an open file
			static const UINT cmdlist1[] = {ID_SAVE,ID_SAVEAS,ID_CLOSEDOCUMENT,
				ID_NEWTEXT,ID_NEWBKGD,ID_NEWGAMA,ID_NEWIEND,ID_NEWPHYS,
				ID_NEWIHDR,
				ID_NEWSRGB,ID_NEWTIME,ID_NEWCHRM,ID_NEWTRNS,ID_NEWSBIT,
				ID_NEWPLTE,ID_NEWSTER,ID_NEWACTL,ID_NEWFCTL,ID_NEWOFFS,
				ID_NEWSCAL,ID_NEWVPAG,
				ID_COMBINEALLIDAT,
				ID_IMPORTCHUNK,ID_IMPORTICCPROF,ID_SIGNATURE,ID_CHECKPNG,
				ID_TOOL_1,ID_TOOL_2,ID_TOOL_3,ID_TOOL_4,ID_TOOL_5,ID_TOOL_6,
				0};
			// commands requiring exactly 1 selected chunk
			static const UINT cmdlist2[] = {ID_EDITCHUNK,
				ID_SPLITIDAT,ID_EXPORTCHUNK,
				0};
			// requiring 1 or more selected chunks
			static const UINT cmdlist3[] = {ID_DELCHUNK,ID_COPY,ID_CUT,ID_MOVEUP,ID_MOVEDOWN,
				0};

			static const UINT toolsi[] = {ID_TOOL_1,ID_TOOL_2,ID_TOOL_3,ID_TOOL_4,
				ID_TOOL_5,ID_TOOL_6};

			m=(HMENU)wParam;

			mtools=CreatePopupMenu();
#ifdef TWPNG_SUPPORT_VIEWER
			AppendMenu(mtools,MF_STRING|MF_ENABLED,ID_IMGVIEWER,_T("&Image Viewer\tF7"));
			AppendMenu(mtools,MF_SEPARATOR,0,NULL);
#endif
			for(i=0;i<TWPNG_NUMTOOLS;i++) {
				if(lstrlen(globals.tools[i].name)) {
					StringCchPrintf(buf,150,_T("&%d  %s\tCtrl+%d"),i+1,globals.tools[i].name,i+1);
					AppendMenu(mtools,MF_STRING|MF_ENABLED,toolsi[i],buf);
				}
			}

			// the Tools menu has an index of 5
			ModifyMenu(m,4,MF_POPUP|MF_BYPOSITION|MF_ENABLED,(UINT_PTR)mtools,_T("&Tools"));

			sel=0;
			if(png) {
				if(globals.hwndMainList) {
					sel=ListView_GetSelectedCount(globals.hwndMainList);
				}
			}

			x= MF_BYCOMMAND | (png?MF_ENABLED:MF_GRAYED);
			for(i=0;cmdlist1[i];i++) {
				EnableMenuItem(m,cmdlist1[i],x);
			}

			x= MF_BYCOMMAND | ( (sel==1)?MF_ENABLED:MF_GRAYED );
			for(i=0;cmdlist2[i];i++) {
				EnableMenuItem(m,cmdlist2[i],x);
			}

			x= MF_BYCOMMAND | ( (sel>=1)?MF_ENABLED:MF_GRAYED );
			for(i=0;cmdlist3[i];i++) {
				EnableMenuItem(m,cmdlist3[i],x);
			}

			// commands with unusual rules for enabling
			EnableMenuItem(m,ID_REOPEN, MF_BYCOMMAND |
				((png && png->m_named)?MF_ENABLED:MF_GRAYED) );

			EnableMenuItem(m,ID_COMBINEIDAT, MF_BYCOMMAND |
				((sel>1)?MF_ENABLED:MF_GRAYED) );
			
			x= MF_BYCOMMAND;
			if((png) && IsClipboardFormatAvailable(globals.pngchunk_cf)) {
				x |= MF_ENABLED;
			}
			else {
				x |= MF_GRAYED;
			}
			EnableMenuItem(m,ID_PASTE,x);

			CheckMenuItem(m,ID_IMGVIEWER,MF_BYCOMMAND|
				(g_viewer?MF_CHECKED:MF_UNCHECKED));
			return 0;
		}

	case WM_NOTIFY:
		{
			LPNMHDR  lpnmh = (LPNMHDR) lParam;
			if(!png) break;
			switch(lpnmh->code) {
			case NM_DBLCLK:
			case NM_RETURN:
				if(lpnmh->hwndFrom==globals.hwndMainList) {
					DblClickOnList();
					return 0;
				}
			}
			break;
		}

	case WM_COMMAND:
		// commands always available
		switch(id) {
#ifdef TWPNG_SUPPORT_VIEWER
		case ID_IMGVIEWER:
			if(g_viewer) {
				g_viewer->Close();
				if(g_viewer) { delete g_viewer; g_viewer=NULL; }
				globals.autoopen_viewer=0;
			}
			else {
				const TCHAR *fn;
				if(png && png->m_named) fn=png->m_filename;
				else fn=NULL;
				g_viewer = new Viewer(globals.hwndMain,fn);
				update_viewer();
				globals.autoopen_viewer=1;
			}
			return 0;
		case ID_UPDATEVIEWER:
			update_viewer();
			return 0;
#endif
		case ID_EXIT:
			if(OkToClosePNG()) DestroyWindow(hwnd);
			return 0;
		case ID_OPEN:
			if(OkToClosePNG()) OpenPngFromMenu(hwnd);
			return 0;
		case ID_NEW:   
			if(OkToClosePNG()) NewPng();
			return 0;

		case ID_REOPEN:
			ReopenPngDocument();
			return 0;

		case ID_CLOSEDOCUMENT:
			// TODO: There's too much redundant code here.
			if(png) {
				if(OkToClosePNG()) {
					ClosePngDocument();
				}
			}
			return 0;

		case ID_PREFS:
			globals.dlgs_open++;
			struct prefs_dlg_ctx prctx;
			ZeroMemory((void*)&prctx,sizeof(struct prefs_dlg_ctx));
			DialogBoxParam(globals.hInst,_T("DLG_PREFS"),hwnd,DlgProcPrefs,(LPARAM)&prctx);
			globals.dlgs_open--;
			return 0;

		case ID_EDITTOOLS:
			globals.dlgs_open++;
			DialogBox(globals.hInst,_T("DLG_TOOLS"),globals.hwndMain,DlgProcTools);
			globals.dlgs_open--;
			return 0;

		case ID_ABOUT:
			globals.dlgs_open++;
			DialogBox(globals.hInst,_T("DLG_ABOUT"),hwnd,DlgProcAbout);
			globals.dlgs_open--;
			return 0;

		case ID_HELPCONTENTS:
			SetCurrentDirectory(globals.home_dir);
			if((unsigned __int64)ShellExecute(globals.hwndMain,_T("open"),_T("..\\tweakpng.txt"),
				NULL,NULL,SW_SHOWNORMAL)<=32)
			{
				if((unsigned __int64)ShellExecute(globals.hwndMain,_T("open"),_T("tweakpng.txt"),
					NULL,NULL,SW_SHOWNORMAL)<=32)
				{
					mesg(MSG_E,_T("Cannot display help file"));
				}
			}
			SetCurrentDirectory(globals.orig_dir);
			return 0;
		case ID_SWITCHWINDOW:
			if(g_viewer) {
				if(GetFocus()==g_viewer->m_hwndViewer) {
					SetFocus(globals.hwndMain);
				}
				else {
					SetFocus(g_viewer->m_hwndViewer);
				}
			}
			return 0;
		}

		// commands only available if we have an image loaded
		if(png) switch(id) {
		case ID_SIGNATURE:
			globals.dlgs_open++;
			DialogBoxParam(globals.hInst,_T("DLG_SIGNATURE"),globals.hwndMain,DlgProcSetSig,
				(LPARAM)png);
			globals.dlgs_open--;
			return 0;

		case ID_SAVE:         SavePng(hwnd);     return 0;
		case ID_SAVEAS:       SavePngAs(hwnd);   return 0;
		case ID_CHECKPNG:     png->check_validity(0);  return 0;

		case ID_EDITCHUNK:    DblClickOnList();  return 0;
		case ID_DELCHUNK:     DeleteChunks();    return 0;
		case ID_MOVEUP:       MoveChunkUp();     return 0;
		case ID_MOVEDOWN:     MoveChunkDown();   return 0;

		case ID_NEWACTL: png->new_chunk(CHUNK_acTL); return 0;
		case ID_NEWBKGD: png->new_chunk(CHUNK_bKGD); return 0;
		case ID_NEWCHRM: png->new_chunk(CHUNK_cHRM); return 0;
		case ID_NEWFCTL: png->new_chunk(CHUNK_fcTL); return 0;
		case ID_NEWGAMA: png->new_chunk(CHUNK_gAMA); return 0;
		case ID_NEWIEND: png->new_chunk(CHUNK_IEND); return 0;
		case ID_NEWIHDR: png->new_chunk(CHUNK_IHDR); return 0;
		case ID_NEWOFFS: png->new_chunk(CHUNK_oFFs); return 0;
		case ID_NEWPHYS: png->new_chunk(CHUNK_pHYs); return 0;
		case ID_NEWPLTE: png->new_chunk(CHUNK_PLTE); return 0;
		case ID_NEWSBIT: png->new_chunk(CHUNK_sBIT); return 0;
		case ID_NEWSCAL: png->new_chunk(CHUNK_sCAL); return 0;
		case ID_NEWSRGB: png->new_chunk(CHUNK_sRGB); return 0;
		case ID_NEWSTER: png->new_chunk(CHUNK_sTER); return 0;
		case ID_NEWTEXT: png->new_chunk(CHUNK_tEXt); return 0;
		case ID_NEWTIME: png->new_chunk(CHUNK_tIME); return 0;
		case ID_NEWTRNS: png->new_chunk(CHUNK_tRNS); return 0;
		case ID_NEWVPAG: png->new_chunk(CHUNK_vpAg); return 0;

		case ID_CUT:    CutChunks();     return 0;
		case ID_COPY:   CopyChunks();    return 0;
		case ID_PASTE:  PasteChunks();   return 0;
		case ID_SELECTALL:
			twpng_SetLVSelection(globals.hwndMainList,0,png->m_num_chunks);
			return 0;

		case ID_SPLITIDAT:    SplitIDAT();       return 0;
		case ID_COMBINEIDAT:     CombineIDAT_selected(); return 0;
		case ID_COMBINEALLIDAT:  CombineIDAT_all();      return 0;

		case ID_IMPORTCHUNK:  ImportChunk();     return 0;
		case ID_EXPORTCHUNK:  ExportChunk();     return 0;
		case ID_IMPORTICCPROF: ImportICCProfile(png); return 0;

		case ID_TOOL_1: RunTool(0); return 0;
		case ID_TOOL_2: RunTool(1); return 0;
		case ID_TOOL_3: RunTool(2); return 0;
		case ID_TOOL_4: RunTool(3); return 0;
		case ID_TOOL_5: RunTool(4); return 0;
		case ID_TOOL_6: RunTool(5); return 0;

		}

	}
	return (DefWindowProc(hwnd, msg, wParam, lParam));
}

static void twpng_HandleAboutInitDialog(HWND hwnd)
{
	TCHAR buf[4000],buf1[1000];
	TCHAR buf2[200];

	StringCbPrintf(buf1,sizeof(buf1),_T("Version %s %s %s %s %d-bit"),TWEAKPNG_VER_STRING,
		SYM_MIDDOT,_T(__DATE__),SYM_MIDDOT,(int)(sizeof(void*)*8));

	//StringCbPrintf(buf2,sizeof(buf2),_T(" %s %s"),SYM_MIDDOT,_T(__DATE__));
	//StringCbCat(buf1,sizeof(buf1),buf2);

#ifndef UNICODE
	StringCbPrintf(buf2,sizeof(buf2),_T(" %s non-Unicode"),SYM_MIDDOT);
	StringCbCat(buf1,sizeof(buf1),buf2);
#endif
#ifdef _DEBUG
	StringCbPrintf(buf2,sizeof(buf2),_T(" %s Debug build"),SYM_MIDDOT);
	StringCbCat(buf1,sizeof(buf1),buf2);
#endif

	StringCchPrintf(buf,4000,_T("TweakPNG %s A PNG image file manipulation utility\r\n\r\n%s\r\n")
		_T("Copyright ") SYM_COPYRIGHT _T(" %s by Jason Summers\r\nWebsite: %s\r\n"),
		SYM_MIDDOT,buf1,
		TWEAKPNG_COPYRIGHT_DATE,globals.twpng_homepage);

	StringCchCat(buf,4000,_T("\r\nThis program is distributed under the terms ")
		_T("of the GNU General Public License, version 3 or higher. Please read the file tweakpng.txt for more information.\r\n"));

#ifdef TWPNG_HAVE_ZLIB
	StringCchPrintf(buf1,1000,_T("\r\nUses zlib, version %s."),_T(ZLIB_VERSION));
	StringCchCat(buf,4000,buf1);
#else
	StringCchCat(buf,4000,_T("\r\nCompiled without compression/decompression support."));
#endif

#ifdef TWPNG_SUPPORT_VIEWER
	twpng_get_libpng_version(buf2,200);
	StringCchPrintf(buf1,1000,_T("\r\nUses libpng, version %s."),buf2);
	StringCchCat(buf,4000,buf1);
#else
	StringCchCat(buf,4000,_T("\r\nCompiled without image viewing support."));
#endif
	SetDlgItemText(hwnd,IDC_ABOUTTEXT,buf);
}

static INT_PTR CALLBACK DlgProcAbout(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		twpng_HandleAboutInitDialog(hwnd);
		return 1;
	case WM_COMMAND:
		switch(id) {
		case IDOK:
		case IDCANCEL:
			EndDialog(hwnd, 0);
			return 1;
		}
	}
	return 0;
}

// Returns the "name" of files with a given extension.
// format = ".png", ".jng", ".mng"
// returns zero if does not exist
// caller supplies buf[500]
static int get_reg_name_for_ext(const TCHAR *format, TCHAR *logicalname, int logicalname_len)
{
	HKEY key1;
	LONG rv;
	int retval;
	DWORD datatype,datasize;
	TCHAR buf1[500];

	retval=0;
	key1=NULL;
	StringCchCopy(logicalname,logicalname_len,_T(""));

	// First check FileExts key. If a UserChoice key refers us to a logical
	// name, we'll use that name.
	// This does not seem right at all, but it's the only thing I've figured out
	// that seems to work.
	// This probably won't work for everyone, and it will stop working whenever
	// you change your default PNG application.
	StringCbPrintf(buf1,sizeof(buf1),_T("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\%s\\UserChoice"),format);
	rv=RegOpenKeyEx(HKEY_CURRENT_USER,buf1,0,KEY_READ,&key1);
	if(rv==ERROR_SUCCESS) {
		datasize=sizeof(TCHAR)*logicalname_len;
		rv=RegQueryValueEx(key1,_T("Progid"),NULL,&datatype,(BYTE*)logicalname,&datasize);
		if(rv==ERROR_SUCCESS && datatype==REG_SZ) {
			retval = 1;
			RegCloseKey(key1); key1=NULL;
			goto done;
		}
		RegCloseKey(key1); key1=NULL;
	}

	// If UserChoice doesn't exist, check Classes key directly.
	StringCbPrintf(buf1,sizeof(buf1),_T("Software\\Classes\\%s"),format);
	rv=RegOpenKeyEx(HKEY_CURRENT_USER,buf1,0,KEY_READ,&key1);
	if(rv!=ERROR_SUCCESS) goto done;

	datasize=sizeof(TCHAR)*logicalname_len;
	rv=RegQueryValueEx(key1,NULL,NULL,&datatype,(BYTE*)logicalname,&datasize);
	if(rv!=ERROR_SUCCESS) goto done;
	if(datatype!=REG_SZ) goto done;
	if(datasize<sizeof(TCHAR)*2) goto done;
	RegCloseKey(key1); key1=NULL;
	retval=1;

done:
	if(key1) RegCloseKey(key1);
	return retval;
}

static int is_explorer_menu_set()
{
	HKEY key1;
	LONG rv;
	int retval;
	TCHAR logicalname[500];
	TCHAR buf[500];
	int x;


	retval=0;
	key1=NULL;

	x=get_reg_name_for_ext(_T(".png"),logicalname,500);
	if(!x) goto abort;

	StringCchPrintf(buf,500,_T("Software\\Classes\\%s\\shell\\tweakpng"),logicalname);
	// we only want to know if this key exists
	rv=RegOpenKeyEx(HKEY_CURRENT_USER,buf,0,KEY_READ,&key1);

	if(rv!=ERROR_SUCCESS) goto abort;

	retval=1;

abort:
	if(key1) RegCloseKey(key1);
	return retval;
}

static const TCHAR *fmts[] = { _T(".png"),_T(".mng"),_T(".jng") };
static const TCHAR *dflt_fmt_name[] = { _T("png_auto_file"),_T("mng_auto_file"),_T("jng_auto_file") };

// Add to Windows Explorer right-click menu for .[pmj]ng files.
static void add_to_explorer_menu()
{
	int i;
	int x;
	TCHAR logicalname[500];
	HKEY key1;
	DWORD disp;
	LONG rv;
	TCHAR buf1[500];
	TCHAR buf[500];
	TCHAR cmd[500];

	for(i=0;i<=2;i++) {
		x=get_reg_name_for_ext(fmts[i],logicalname,500);
		if(!x) {
			StringCchCopy(logicalname,500,dflt_fmt_name[i]);
			StringCbPrintf(buf1,sizeof(buf1),_T("Software\\Classes\\%s"),fmts[i]);
			rv=RegCreateKeyEx(HKEY_CURRENT_USER,buf1,0,NULL,
				REG_OPTION_NON_VOLATILE,KEY_WRITE,NULL,&key1,&disp);
			if(rv==ERROR_SUCCESS) {
				RegSetValueEx(key1,_T(""),0,REG_SZ,(BYTE*)logicalname,(DWORD)sizeof(TCHAR)*(1+lstrlen(logicalname)));
				RegCloseKey(key1);
			}
		}

		StringCbPrintf(buf1,sizeof(buf1),_T("Software\\Classes\\%s"),logicalname);
		rv=RegCreateKeyEx(HKEY_CURRENT_USER,buf1,0,NULL,
			REG_OPTION_NON_VOLATILE,KEY_WRITE,NULL,&key1,&disp);
		if(rv==ERROR_SUCCESS) {
			RegCloseKey(key1);
		}

		StringCchPrintf(buf,500,_T("Software\\Classes\\%s\\shell"),logicalname);
		rv=RegCreateKeyEx(HKEY_CURRENT_USER,buf,0,NULL,
			REG_OPTION_NON_VOLATILE,KEY_WRITE,NULL,&key1,&disp);
		if(rv==ERROR_SUCCESS) {
			RegCloseKey(key1);
		}

		StringCchPrintf(buf,500,_T("Software\\Classes\\%s\\shell\\tweakpng"),logicalname);
		rv=RegCreateKeyEx(HKEY_CURRENT_USER,buf,0,NULL,
			REG_OPTION_NON_VOLATILE,KEY_WRITE,NULL,&key1,&disp);
		if(rv==ERROR_SUCCESS) {
			// The name on the menu:
			StringCchCopy(buf,500,_T("TweakPNG"));
			RegSetValueEx(key1,_T(""),0,REG_SZ,(BYTE*)buf,(DWORD)sizeof(TCHAR)*(1+lstrlen(buf)));
			RegCloseKey(key1);
		}

		StringCchPrintf(buf,500,_T("Software\\Classes\\%s\\shell\\tweakpng\\command"),logicalname);
		rv=RegCreateKeyEx(HKEY_CURRENT_USER,buf,0,NULL,
			REG_OPTION_NON_VOLATILE,KEY_WRITE,NULL,&key1,&disp);
		if(rv==ERROR_SUCCESS) {
			GetModuleFileName(globals.hInst,buf,500);
			StringCchPrintf(cmd,500,_T("\"%s\" \"%%1\""), buf);
			RegSetValueEx(key1,_T(""),0,REG_SZ,(BYTE*)cmd,(DWORD)sizeof(TCHAR*)*(1+lstrlen(cmd)));
			RegCloseKey(key1);
		}
	}
}

static void remove_from_explorer_menu()
{
	TCHAR logicalname[500];
	TCHAR buf[500];
	int i;
	int x;


	for(i=0;i<=2;i++) {
		x=get_reg_name_for_ext(fmts[i],logicalname,500);
		if(x) {
			StringCchPrintf(buf,500,_T("Software\\Classes\\%s\\shell\\tweakpng\\command"),logicalname);
			RegDeleteKey(HKEY_CURRENT_USER,buf);
			StringCchPrintf(buf,500,_T("Software\\Classes\\%s\\shell\\tweakpng"),logicalname);
			RegDeleteKey(HKEY_CURRENT_USER,buf);
		}
	}
	return;
}

static const TCHAR *cmprlevel_names[] = { _T("Default compression"),
      _T("0 = No compression"),
	  _T("1 = Fastest"),_T("2"),_T("3"),_T("4"),_T("5"),_T("6"),_T("7"),_T("8"),
	  _T("9 = Best compression"),NULL };

static INT_PTR CALLBACK DlgProcPrefs(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	int i;
	WORD id;
	struct prefs_dlg_ctx *prctx = NULL;

	id=LOWORD(wParam);

	if(msg==WM_INITDIALOG) {
		prctx = (struct prefs_dlg_ctx*)lParam;
		if(!prctx) return 1;
		SetWindowLongPtr(hwnd,DWLP_USER,lParam);

		for(i=0;cmprlevel_names[i];i++) {
			SendDlgItemMessage(hwnd,IDC_CMPRLEVEL,CB_ADDSTRING,0,
			(LPARAM)cmprlevel_names[i]);
		}

		if(globals.compression_level == Z_DEFAULT_COMPRESSION) {
			i=0;
		}
		else {
			i=globals.compression_level+1;
		}
		SendDlgItemMessage(hwnd,IDC_CMPRLEVEL,CB_SETCURSEL,(WPARAM)i,0);

		{
			int x;
			x=is_explorer_menu_set();
			CheckDlgButton(hwnd,IDC_ADDTOMENU,x?BST_CHECKED:BST_UNCHECKED);
			prctx->remember_menusetting=x;
		}

		return 1;
	}
	else {
		prctx = (struct prefs_dlg_ctx*)GetWindowLongPtr(hwnd,DWLP_USER);
		if(!prctx) return 0;
	}

	switch (msg) {
	case WM_COMMAND:
		switch(id) {
		case IDOK:
			{
				int x;
				x= (IsDlgButtonChecked(hwnd,IDC_ADDTOMENU) == BST_CHECKED);

				if(x!=prctx->remember_menusetting) {
					if(x) {
						add_to_explorer_menu();
					}
					else {
						remove_from_explorer_menu();
					}
				}
			}

			i=(int)SendDlgItemMessage(hwnd,IDC_CMPRLEVEL,CB_GETCURSEL,0,0);
			if(i==CB_ERR) i=0;
			if(i==0) globals.compression_level= Z_DEFAULT_COMPRESSION;
			else globals.compression_level= i-1;
			SaveSettings();
			EndDialog(hwnd, 0);
			return 1;

		case IDCANCEL:
			EndDialog(hwnd, 0);
			return 1;
		}
	}
	return 0;
}

static INT_PTR CALLBACK DlgProcSplitIDAT(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	Chunk *ch=NULL;
	WORD id;
	TCHAR buf[500];

	id=LOWORD(wParam);

	if(msg==WM_INITDIALOG) {
		ch = (Chunk*)lParam;
		if(!ch) return 1;
		SetWindowLongPtr(hwnd,DWLP_USER,lParam);
		StringCchPrintf(buf,500,_T("Size in bytes of first section (0") SYM_ENDASH _T("%d)"),ch->length);
		SetDlgItemText(hwnd,IDC_SPLIT_TEXT,buf);

		return 1;
	}
	else {
		ch = (Chunk*)GetWindowLongPtr(hwnd,DWLP_USER);
	}

	switch (msg) {
	case WM_DESTROY:
		ch=NULL;
		return 1;

	case WM_COMMAND:
		if(!ch) return 1;
		switch(id) {
		case IDOK:
			{
				int n,rep;

				n=GetDlgItemInt(hwnd,IDC_SPLIT_SIZE,NULL,TRUE);
				if(n<0 || n>(int)ch->length) {
					EndDialog(hwnd, 0);
					return 1;
				}

				rep= (IsDlgButtonChecked(hwnd,IDC_SPLIT_REPEAT)==BST_CHECKED)?1:0;

				if(n==0 && rep) {
					mesg(MSG_E,_T("Can") SYM_RSQUO _T("t divide repeatedly into chunks of zero size."));
					return 1;
				}

				if(ch->m_parentpng->split_idat(ch->m_index,n,rep)) {
					EndDialog(hwnd, 0);
				}
				return 1;
			}
		case IDCANCEL:
			EndDialog(hwnd, 0);
			return 1;
		}
	}
	return 0;
}

static INT_PTR CALLBACK DlgProcSetSig(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	Png *png1=NULL;
	WORD id;
	UINT x;

	id=LOWORD(wParam);

	if(msg==WM_INITDIALOG) {
		png1 = (Png*)lParam;
		if(!png1) return 1;
		SetWindowLongPtr(hwnd,DWLP_USER,lParam);

		switch(png1->m_imgtype) {
		case IMG_PNG: x=IDC_RADIO1; break;
		case IMG_MNG: x=IDC_RADIO2; break;
		case IMG_JNG: x=IDC_RADIO3; break;
		default: x=0;
		}
		if(x) CheckDlgButton(hwnd,x,BST_CHECKED);

		return 1;

	}
	else {
		png1 = (Png*)GetWindowLongPtr(hwnd,DWLP_USER);
		if(!png1) return 0;
	}

	switch (msg) {
	case WM_DESTROY:
		png1=NULL;
		return 1;

	case WM_COMMAND:
		if(!png1) return 1;
		switch(id) {
		case IDOK:
			{
				int oldtype=png1->m_imgtype;
				if(IsDlgButtonChecked(hwnd,IDC_RADIO1)==BST_CHECKED) png1->m_imgtype=IMG_PNG;
				if(IsDlgButtonChecked(hwnd,IDC_RADIO2)==BST_CHECKED) png1->m_imgtype=IMG_MNG;
				if(IsDlgButtonChecked(hwnd,IDC_RADIO3)==BST_CHECKED) png1->m_imgtype=IMG_JNG;
				if(png1->m_imgtype!=oldtype) {
					png1->set_signature();
					png1->modified();
				}
				EndDialog(hwnd, 0);
				return 1;
			}
		case IDCANCEL:
			EndDialog(hwnd, 0);
			return 1;
		}
	}
	return 0;
}

static void twpng_BrowseForTool(HWND hwnd, int dlgitem)
{
	OPENFILENAME ofn;
	TCHAR fn[MAX_PATH];

	StringCchCopy(fn,MAX_PATH,_T(""));
	ZeroMemory(&ofn,sizeof(OPENFILENAME));
	ofn.lStructSize=sizeof(OPENFILENAME);
	ofn.hwndOwner=hwnd;
	ofn.hInstance=NULL;
	ofn.lpstrFilter=_T("Executable Files\0*.exe;*.bat\0All files (*.*)\0*.*\0\0");
	ofn.nFilterIndex=1;
	ofn.lpstrTitle=_T("Browse for tool");
	//ofn.lpstrInitialDir=NULL;
	ofn.lpstrFile=fn;
	ofn.nMaxFile=MAX_PATH;
	ofn.Flags=OFN_FILEMUSTEXIST|OFN_HIDEREADONLY|OFN_NOCHANGEDIR;

	if(!GetOpenFileName(&ofn)) {
		return;  // user canceled
	}

	SetDlgItemText(hwnd,dlgitem,fn);
}

static INT_PTR CALLBACK DlgProcTools(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	int i;

	static const UINT toolsn[] = {IDC_EDIT1,IDC_EDIT2,IDC_EDIT3,IDC_EDIT4,
		IDC_EDIT5,IDC_EDIT6};
	static const UINT toolsc[] = {IDC_EDITC1,IDC_EDITC2,IDC_EDITC3,IDC_EDITC4,
		IDC_EDITC5,IDC_EDITC6};
	static const UINT toolsp[] = {IDC_EDITP1,IDC_EDITP2,IDC_EDITP3,IDC_EDITP4,
		IDC_EDITP5,IDC_EDITP6};


	id=LOWORD(wParam);

	switch (msg) {
	case WM_INITDIALOG:
		SetDlgItemText(hwnd,IDC_STATIC1,_T("Configure the tools to list on the Tools menu. ")
		  _T("The Name field is any name you choose. ")
		  _T("In the Parameters field, for a viewer, ") SYM_LDQUO _T("%1") SYM_RDQUO _T(" will be replaced by the PNG filename. ")
		  _T("For a filter, ") SYM_LDQUO _T("%1") SYM_RDQUO _T(" is the input file and ") SYM_LDQUO _T("%2") SYM_RDQUO _T(" is the output file."));
		for(i=0;i<TWPNG_NUMTOOLS;i++) {
			SetDlgItemText(hwnd,toolsn[i],globals.tools[i].name);
			SetDlgItemText(hwnd,toolsc[i],globals.tools[i].cmd);
			SetDlgItemText(hwnd,toolsp[i],globals.tools[i].params);
			SendDlgItemMessage(hwnd,toolsn[i],EM_LIMITTEXT,MAX_TOOL_NAME  -1,0);
			SendDlgItemMessage(hwnd,toolsc[i],EM_LIMITTEXT,MAX_TOOL_CMD   -1,0);
			SendDlgItemMessage(hwnd,toolsp[i],EM_LIMITTEXT,MAX_TOOL_PARAMS-1,0);
		}
		return 1;

//	case WM_DESTROY:
//		return 1;

	case WM_COMMAND:
		switch(id) {
		case IDC_TOOL1BROWSE: twpng_BrowseForTool(hwnd,IDC_EDITC1); return 1;
		case IDC_TOOL2BROWSE: twpng_BrowseForTool(hwnd,IDC_EDITC2); return 1;
		case IDC_TOOL3BROWSE: twpng_BrowseForTool(hwnd,IDC_EDITC3); return 1;
		case IDC_TOOL4BROWSE: twpng_BrowseForTool(hwnd,IDC_EDITC4); return 1;
		case IDC_TOOL5BROWSE: twpng_BrowseForTool(hwnd,IDC_EDITC5); return 1;
		case IDC_TOOL6BROWSE: twpng_BrowseForTool(hwnd,IDC_EDITC6); return 1;

		case IDOK:
			for(i=0;i<TWPNG_NUMTOOLS;i++) {
				GetDlgItemText(hwnd,toolsn[i],globals.tools[i].name,MAX_TOOL_NAME);
				globals.tools[i].name[MAX_TOOL_NAME-1]='\0';
				GetDlgItemText(hwnd,toolsc[i],globals.tools[i].cmd,MAX_TOOL_CMD);
				globals.tools[i].cmd[MAX_TOOL_CMD-1]='\0';
				GetDlgItemText(hwnd,toolsp[i],globals.tools[i].params,MAX_TOOL_PARAMS);
				globals.tools[i].params[MAX_TOOL_PARAMS-1]='\0';
				SaveSettings();
				EndDialog(hwnd, 0);
			}
			return 1;

		case IDCANCEL:
			EndDialog(hwnd, 0);
			return 1;
		}
	}
	return 0;
}

