// pngtodib.cpp - part of TweakPNG
/*
    Copyright (C) 2011 Jason Summers

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

#ifdef TWPNG_SUPPORT_VIEWER

#include <windows.h>
#include <tchar.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <malloc.h>
#include <math.h>

#include <png.h>

#include "pngtodib.h"
#include <strsafe.h>

#define PNGDIB_ERRMSG_MAX 200

struct PNGD_COLOR_struct {
	unsigned char red, green, blue, reserved;
};

struct p2d_struct {
	void *userdata;
	TCHAR *errmsg;

	pngdib_read_cb_type read_cb;

	int use_file_bg_flag;
	int use_custom_bg_flag;

	// TODO: I don't understand exactly what the following 3 variables do,
	// but I'm planning major changes to the background color code, so they
	// will probably be replaced at some point.
	struct PNGD_COLOR_struct bkgd_color; // Background color read from PNG file(?)
	struct PNGD_COLOR_struct bgcolor; // Custom background color set by caller(?)
	int bgcolor_returned; // Does bgcolor contain a color that can be returned to the app(?)

	int gamma_correction; // should we gamma correct (using screen_gamma)?
	double screen_gamma;

	BITMAPINFOHEADER*   pdib;
	int        dibsize;
	int        bits_offs;
	int        bitssize;
	RGBQUAD*   palette;
	void*      pbits;
	int        res_x,res_y;
	int        res_units;
	int        res_valid;  // are res_x, res_y, res_units valid?

	int has_bkgd;  // ==1 if there a bkgd chunk, and USE_BKGD flag
	int is_grayscale;
	int png_bit_depth;  // FIXME: rename this
	int has_trns;
	int color_type;
	int has_alpha_channel;
	int trns_color;
	int manual_trns;

	double file_gamma;
	int manual_gamma;

	int palette_entries;
	png_colorp png_palette;
};

struct errstruct {
	jmp_buf *jbufp;
	TCHAR *errmsg;
};

static void pngd_get_error_message(int rv,TCHAR *e, int e_len)
{
	switch(rv) {
	case PNGD_E_ERROR:   StringCchCopy(e,e_len,_T("Unknown error")); break;
	case PNGD_E_NOMEM:   StringCchCopy(e,e_len,_T("Unable to allocate memory")); break;
	case PNGD_E_LIBPNG:  StringCchCopy(e,e_len,_T("libpng reported an error")); break;
	case PNGD_E_BADPNG:  StringCchCopy(e,e_len,_T("Invalid PNG image")); break;
	case PNGD_E_READ:    StringCchCopy(e,e_len,_T("Unable to read file")); break;
	}
}

static void my_png_error_fn(png_structp png_ptr, const char *err_msg)
{
	struct errstruct *errinfop;
	jmp_buf *j;

	errinfop = (struct errstruct *)png_get_error_ptr(png_ptr);
	j = errinfop->jbufp;

#ifdef _UNICODE
	StringCchPrintf(errinfop->errmsg,PNGDIB_ERRMSG_MAX,_T("[libpng] %S"),err_msg);
#else
	StringCchPrintf(errinfop->errmsg,PNGDIB_ERRMSG_MAX,"[libpng] %s",err_msg);
#endif

	longjmp(*j, -1);
}

static void my_png_warning_fn(png_structp png_ptr, const char *warn_msg)
{
	return;
}

// A callback function used with custom I/O.
static void my_png_read_fn_custom(png_structp png_ptr,
      png_bytep data, png_size_t length)
{
	struct p2d_struct *p2d;
	int ret;

	p2d = (struct p2d_struct*)png_get_io_ptr(png_ptr);
	if(!p2d->read_cb) return;

	ret = p2d->read_cb(p2d->userdata,(void*)data,(int)length);
	if(ret < (int)length) {
		// This error message is just a guess. It might be nice to
		// have a way to get a real error message.
		png_error(png_ptr, "Read error: Unexpected end of file");
	}
}

// This function should perform identically to libpng's gamma correction.
// I'd prefer to have libpng do all gamma correction itself,
// but I can't figure out how to do that efficiently.
static void gamma_correct(double screen_gamma,double file_gamma,
	 unsigned char *red, unsigned char *green, unsigned char *blue)
{
	double g;

#ifndef PNG_GAMMA_THRESHOLD
#  define PNG_GAMMA_THRESHOLD 0.05
#endif

	if(fabs(screen_gamma*file_gamma-1.0)<=PNG_GAMMA_THRESHOLD) return;

	if (screen_gamma>0.000001)
		g=1.0/(file_gamma*screen_gamma);
	else
		g=1.0;

	(*red)   = (unsigned char)(pow((double)(*red  )/255.0,g)*255.0+0.5);
	(*green) = (unsigned char)(pow((double)(*green)/255.0,g)*255.0+0.5);
	(*blue)  = (unsigned char)(pow((double)(*blue )/255.0,g)*255.0+0.5);
}

// Reads pHYs chunk, sets p2d->res_*.
static void p2d_read_density(PNGDIB *p2d, png_structp png_ptr, png_infop info_ptr)
{
	int has_phys;
	int res_unit_type;
	png_uint_32 res_x, res_y;

	has_phys=png_get_valid(png_ptr,info_ptr,PNG_INFO_pHYs);
	if(!has_phys) return;

	png_get_pHYs(png_ptr,info_ptr,&res_x,&res_y,&res_unit_type);
	if(res_x<1 || res_y<1) return;

	p2d->res_x=res_x;
	p2d->res_y=res_y;
	p2d->res_units=res_unit_type;
	p2d->res_valid=1;
}

static void p2d_handle_background(PNGDIB *p2d, png_structp png_ptr, png_infop info_ptr)
{
	png_color_16p bg_colorp;  // background color (if has_bkgd)
	png_color_16p temp_colorp;
	png_color_16 bkgd; // used with png_set_background
	png_bytep trns_trans;
	int i;

	// look for bKGD chunk, and process if applicable
	if(p2d->use_file_bg_flag) {
		if(png_get_bKGD(png_ptr, info_ptr, &bg_colorp)) {
			// process the background, store 8-bit RGB in bkgd_color
			p2d->has_bkgd=1;

			if(p2d->is_grayscale && p2d->png_bit_depth<8) {
				p2d->bkgd_color.red  =
				p2d->bkgd_color.green=
				p2d->bkgd_color.blue =
					(unsigned char) ( (bg_colorp->gray*255)/( (1<<p2d->png_bit_depth)-1 ) );
			}
			else if(p2d->png_bit_depth<=8) {
				p2d->bkgd_color.red=(unsigned char)(bg_colorp->red);
				p2d->bkgd_color.green=(unsigned char)(bg_colorp->green);
				p2d->bkgd_color.blue =(unsigned char)(bg_colorp->blue);
			}
			else {
				p2d->bkgd_color.red=(unsigned char)(bg_colorp->red>>8);
				p2d->bkgd_color.green=(unsigned char)(bg_colorp->green>>8);
				p2d->bkgd_color.blue =(unsigned char)(bg_colorp->blue>>8);
			}
		}
	}

	if( !(p2d->color_type & PNG_COLOR_MASK_ALPHA) && !p2d->has_trns) {
		// If no transparency, we can skip this whole background-color mess.
		return;
	}

	if(p2d->has_bkgd && (p2d->png_bit_depth>8 || !p2d->is_grayscale || p2d->has_alpha_channel)) {
		png_set_background(png_ptr, bg_colorp,
			   PNG_BACKGROUND_GAMMA_FILE, 1, 1.0);
	}
	else if(p2d->is_grayscale && p2d->has_trns && p2d->png_bit_depth<=8
		&& (p2d->has_bkgd || (p2d->use_custom_bg_flag)) )
	{
		// grayscale binarytrans,<=8bpp: transparency is handle manually
		// by modifying a palette entry (later)
		png_get_tRNS(png_ptr,info_ptr,&trns_trans, &i, &temp_colorp);
		if(i>=1) {
			p2d->trns_color= temp_colorp->gray; // corresponds to a palette entry
			p2d->manual_trns=1;
		}
	}
	else if(!p2d->has_bkgd && (p2d->has_trns || p2d->has_alpha_channel) && 
		(p2d->use_custom_bg_flag) ) 
	{      // process most CUSTOM background colors
		bkgd.index = 0; // unused
		bkgd.red   = p2d->bgcolor.red;
		bkgd.green = p2d->bgcolor.green;
		bkgd.blue  = p2d->bgcolor.blue;

		// libpng may use bkgd.gray if bkgd.red==bkgd.green==bkgd.blue.
		// Not sure if that's a libpng bug or not.
		bkgd.gray  = p2d->bgcolor.red;

		if(p2d->png_bit_depth>8) {
			bkgd.red  = (bkgd.red  <<8)|bkgd.red; 
			bkgd.green= (bkgd.green<<8)|bkgd.green;
			bkgd.blue = (bkgd.blue <<8)|bkgd.blue;
			bkgd.gray = (bkgd.gray <<8)|bkgd.gray;
		}

		if(p2d->is_grayscale) {
			/* assert(p2d->png_bit_depth>8); */

			/* Need to expand to full RGB if unless background is pure gray */
			if(bkgd.red!=bkgd.green || bkgd.red!=bkgd.blue) {
				png_set_gray_to_rgb(png_ptr);

				// png_set_tRNS_to_alpha() is called here because otherwise
				// binary transparency for 16-bps grayscale images doesn't
				// work. Libpng will think black pixels are transparent.
				// I don't know exactly why it works. It does *not* add an
				// alpha channel, as you might think (adding an alpha
				// channnel makes no sense if you are using 
				// png_set_background).
				//
				// Here's an alternate hack that also seems to work, but
				// uses direct structure access:
				//
				// png_ptr->trans_values.red   =    				
				//  png_ptr->trans_values.green =
				//	png_ptr->trans_values.blue  = png_ptr->trans_values.gray;
				if(p2d->has_trns) 
					png_set_tRNS_to_alpha(png_ptr);

				png_set_background(png_ptr, &bkgd,
					  PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);

			}
			else {  // gray custom background
				png_set_background(png_ptr, &bkgd,
					  PNG_BACKGROUND_GAMMA_SCREEN, 1, 1.0);
			}

		}
		else {
			png_set_background(png_ptr, &bkgd,
				  PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
		}
	}
}

static void p2d_handle_gamma(PNGDIB *p2d, png_structp png_ptr, png_infop info_ptr)
{
	int has_gama;
	int intent;

	if (png_get_sRGB(png_ptr, info_ptr, &intent)) {
		has_gama=1;
		p2d->file_gamma = 0.45455;
	}
	else if(png_get_gAMA(png_ptr, info_ptr, &p2d->file_gamma)) {
		has_gama=1;
	}
	else {
		has_gama=0;
		p2d->file_gamma = 0.45455;
	}

	p2d->manual_gamma=0;
	if(p2d->gamma_correction) {

		if(!p2d->is_grayscale || p2d->png_bit_depth>8 || p2d->has_alpha_channel) {
			png_set_gamma(png_ptr, p2d->screen_gamma, p2d->file_gamma);
			//png_ptr->transformations |= 0x2000; // hack for old libpng versions
		}
		else p2d->manual_gamma=1;

		if(p2d->has_bkgd) {
			// Gamma correct the background color (if we got it from the file)
			// before returning it to the app.
			gamma_correct(p2d->screen_gamma,p2d->file_gamma,&p2d->bkgd_color.red,&p2d->bkgd_color.green,&p2d->bkgd_color.blue);
		}
	}
}

static void p2d_read_or_create_palette(PNGDIB *p2d, png_structp png_ptr, png_infop info_ptr)
{
	int i;

	// set up the DIB palette, if needed
	switch(p2d->color_type) {
	case PNG_COLOR_TYPE_PALETTE:
		for(i=0;i<p2d->palette_entries;i++) {
			p2d->palette[i].rgbRed   = p2d->png_palette[i].red;
			p2d->palette[i].rgbGreen = p2d->png_palette[i].green;
			p2d->palette[i].rgbBlue  = p2d->png_palette[i].blue;
		}
		break;
	case PNG_COLOR_TYPE_GRAY:
	case PNG_COLOR_TYPE_GRAY_ALPHA:
		for(i=0;i<p2d->palette_entries;i++) {
			p2d->palette[i].rgbRed   =
			p2d->palette[i].rgbGreen =
			p2d->palette[i].rgbBlue  = (i*255)/(p2d->palette_entries-1);
			if(p2d->manual_gamma) {
				gamma_correct(p2d->screen_gamma,p2d->file_gamma,
					  &(p2d->palette[i].rgbRed),
					  &(p2d->palette[i].rgbGreen),
					  &(p2d->palette[i].rgbBlue));
			}
		}
		if(p2d->manual_trns) {
			p2d->palette[p2d->trns_color].rgbRed   = p2d->bkgd_color.red;
			p2d->palette[p2d->trns_color].rgbGreen = p2d->bkgd_color.green;
			p2d->palette[p2d->trns_color].rgbBlue  = p2d->bkgd_color.blue;
		}
		break;
	}
}

static int p2d_convert_2bit_to_4bit(PNGDIB *p2d, png_uint_32 width, png_uint_32 height,
   unsigned char **row_pointers)
{
	unsigned char *tmprow;
	int i,j;

	tmprow = (unsigned char*)malloc((width+3)/4 );
	if(!tmprow) { return 0; }

	for(j=0;j<(int)height;j++) {
		CopyMemory(tmprow, row_pointers[j], (width+3)/4 );
		ZeroMemory(row_pointers[j], (width+1)/2 );

		for(i=0;i<(int)width;i++) {
			row_pointers[j][i/2] |= 
				( ((tmprow[i/4] >> (2*(3-i%4)) ) & 0x03)<< (4*(1-i%2)) );
		}
	}
	free((void*)tmprow);
	return 1;
}

int pngdib_p2d_run(PNGDIB *p2d)
{
	png_structp png_ptr;
	png_infop info_ptr;
	jmp_buf jbuf;
	struct errstruct errinfo;
	png_uint_32 width, height;
	int interlace_type;
	unsigned char **row_pointers;
	unsigned char *lpdib;
	unsigned char *dib_palette;
	unsigned char *dib_bits;
	int dib_bpp, dib_bytesperrow;
	int j;
	int rv;
	size_t palette_offs;

	p2d->manual_trns=0;
	p2d->has_trns=p2d->has_bkgd=0;
	rv=PNGD_E_ERROR;
	png_ptr=NULL;
	info_ptr=NULL;
	row_pointers=NULL;
	lpdib=NULL;

	StringCchCopy(p2d->errmsg,PNGDIB_ERRMSG_MAX,_T(""));

	if(p2d->use_custom_bg_flag) {
		p2d->bkgd_color.red=   p2d->bgcolor.red;
		p2d->bkgd_color.green= p2d->bgcolor.green;
		p2d->bkgd_color.blue=  p2d->bgcolor.blue;
	}
	else {
		p2d->bkgd_color.red=   255; // Should never get used. If the
		p2d->bkgd_color.green= 128; // background turns orange, it's a bug.
		p2d->bkgd_color.blue=  0;
	}

	// Set the user-defined pointer to point to our jmp_buf. This will
	// hopefully protect against potentially different sized jmp_buf's in
	// libpng, while still allowing this library to be threadsafe.
	errinfo.jbufp = &jbuf;
	errinfo.errmsg = p2d->errmsg;

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,(void*)(&errinfo),
		my_png_error_fn, my_png_warning_fn);
	if(!png_ptr) { rv=PNGD_E_NOMEM; goto abort; }

	png_set_user_limits(png_ptr,100000,100000); // max image dimensions

#if PNG_LIBPNG_VER >= 10400
	// Number of ancillary chunks stored.
	// I don't think we need any of these, but there appears to be no
	// way to set the limit to 0. (0 is reserved to mean "unlimited".)
	// I'll just set it to an arbitrary low number.
	png_set_chunk_cache_max(png_ptr,50);
#endif

#if PNG_LIBPNG_VER >= 10401
	png_set_chunk_malloc_max(png_ptr,1000000);
#endif

	info_ptr = png_create_info_struct(png_ptr);
	if(!info_ptr) {
		//png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
		rv=PNGD_E_NOMEM; goto abort;
	}

	if(setjmp(jbuf)) {
		// we'll get here if an error occurred in any of the following
		// png_ functions

		rv=PNGD_E_LIBPNG;
		goto abort;
	}

	png_set_read_fn(png_ptr, (void*)p2d, my_png_read_fn_custom);

	png_read_info(png_ptr, info_ptr);

	png_get_IHDR(png_ptr, info_ptr, &width, &height, &p2d->png_bit_depth, &p2d->color_type,
		&interlace_type, NULL, NULL);

	p2d->is_grayscale = !(p2d->color_type&PNG_COLOR_MASK_COLOR);
	p2d->has_alpha_channel = (p2d->color_type&PNG_COLOR_MASK_ALPHA)?1:0;

	p2d->has_trns = png_get_valid(png_ptr,info_ptr,PNG_INFO_tRNS);

	//////// BACKGROUND stuff

	p2d_handle_background(p2d,png_ptr,info_ptr);
	////////

	// If we don't have any background color at all that we can use,
	// strip the alpha channel.
	if(p2d->has_alpha_channel && !p2d->has_bkgd && 
		!(p2d->use_custom_bg_flag) )
	{
		png_set_strip_alpha(png_ptr);
	}

	if(p2d->png_bit_depth>8)
		png_set_strip_16(png_ptr);

	p2d_handle_gamma(p2d,png_ptr,info_ptr);

	png_read_update_info(png_ptr, info_ptr);

	// color type may have changed, due to our transformations
	p2d->color_type = png_get_color_type(png_ptr,info_ptr);


	//////// Decide on DIB image type, etc.

	switch(p2d->color_type) {
	case PNG_COLOR_TYPE_RGB:
		dib_bpp= 24;
		p2d->palette_entries=0;
		png_set_bgr(png_ptr);
		break;
	case PNG_COLOR_TYPE_PALETTE:
		dib_bpp=p2d->png_bit_depth;
		png_get_PLTE(png_ptr,info_ptr,&p2d->png_palette,&p2d->palette_entries);
		break;
	case PNG_COLOR_TYPE_GRAY:
	case PNG_COLOR_TYPE_GRAY_ALPHA:
		dib_bpp=p2d->png_bit_depth;
		if(p2d->png_bit_depth>8) dib_bpp=8;
		p2d->palette_entries= 1<<dib_bpp;
		// we'll construct a grayscale palette later
		break;
	default:
		rv=PNGD_E_BADPNG;
		goto abort;
	}

	if(dib_bpp==2) dib_bpp=4;

	//////// Read the image density

	p2d_read_density(p2d,png_ptr,info_ptr);

	//////// Calculate the size of the DIB, and allocate memory for it.

	// DIB scanlines are padded to 4-byte boundaries.
	dib_bytesperrow= (((width * dib_bpp)+31)/32)*4;

	p2d->bitssize = height*dib_bytesperrow;

	p2d->dibsize=sizeof(BITMAPINFOHEADER) + 4*p2d->palette_entries + p2d->bitssize;

	lpdib = (unsigned char*)calloc(p2d->dibsize,1);

	if(!lpdib) { rv=PNGD_E_NOMEM; goto abort; }
	p2d->pdib = (LPBITMAPINFOHEADER)lpdib;

	////////

	// there is some redundancy here...
	palette_offs=sizeof(BITMAPINFOHEADER);
	p2d->bits_offs   =sizeof(BITMAPINFOHEADER) + 4*p2d->palette_entries;
	dib_palette= &lpdib[palette_offs];
	p2d->palette= (RGBQUAD*)dib_palette;
	dib_bits   = &lpdib[p2d->bits_offs];
	p2d->pbits = (VOID*)dib_bits;

	//////// Copy the PNG palette to the DIB palette,
	//////// or create the DIB palette.

	p2d_read_or_create_palette(p2d,png_ptr,info_ptr);

	//////// Allocate row_pointers, which point to each row in the DIB we allocated.

	row_pointers=(unsigned char**)malloc(height*sizeof(unsigned char*));
	if(!row_pointers) { rv=PNGD_E_NOMEM; goto abort; }

	for(j=0;j<(int)height;j++) {
		row_pointers[height-1-j]= &dib_bits[j*dib_bytesperrow];
	}

	//////// Read the PNG image into our DIB memory structure.

	png_read_image(png_ptr, row_pointers);

	////////

	// special handling for this bit depth, since it doesn't exist in DIBs
	// expand 2bpp to 4bpp
	if(p2d->png_bit_depth==2) {
		if(!p2d_convert_2bit_to_4bit(p2d,width,height,row_pointers)) {
			rv=PNGD_E_NOMEM; goto abort;
		}
	}

	////////

	free((void*)row_pointers);
	row_pointers=NULL;

	png_read_end(png_ptr, info_ptr);

	png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
	png_ptr=NULL;

	// fill in the DIB header fields
	p2d->pdib->biSize=          sizeof(BITMAPINFOHEADER);
	p2d->pdib->biWidth=         width;
	p2d->pdib->biHeight=        height;
	p2d->pdib->biPlanes=        1;
	p2d->pdib->biBitCount=      dib_bpp;
	p2d->pdib->biCompression=   BI_RGB;
	// biSizeImage can also be 0 in uncompressed bitmaps
	p2d->pdib->biSizeImage=     height*dib_bytesperrow;

	if(p2d->res_valid) {
		p2d->pdib->biXPelsPerMeter= p2d->res_x;
		p2d->pdib->biYPelsPerMeter= p2d->res_y;
	}
	else {
		p2d->pdib->biXPelsPerMeter= 72;
		p2d->pdib->biYPelsPerMeter= 72;
	}

	p2d->pdib->biClrUsed=       p2d->palette_entries;
	p2d->pdib->biClrImportant=  0;

	if(p2d->has_bkgd || (p2d->use_custom_bg_flag)) {
		// return the background color if one was used
		p2d->bgcolor.red   = p2d->bkgd_color.red;
		p2d->bgcolor.green = p2d->bkgd_color.green;
		p2d->bgcolor.blue  = p2d->bkgd_color.blue;
		p2d->bgcolor_returned=1;
	}

	return PNGD_E_SUCCESS;

abort:

	if(png_ptr) png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
	if(row_pointers) free((void*)row_pointers);
	if(lpdib) {
		pngdib_p2d_free_dib((PNGDIB*)p2d,NULL);
	}

	// If we don't have an error message yet, use a
	// default one based on the code
	if(!lstrlen(p2d->errmsg)) {
		pngd_get_error_message(rv,p2d->errmsg,PNGDIB_ERRMSG_MAX);
	}

	return rv;
}

void pngdib_p2d_free_dib(PNGDIB *p2d, BITMAPINFOHEADER* pdib)
{
	if(!p2d) {
		if(pdib) free((void*)pdib);
		return;
	}

	if(!pdib) {
		// DIB not explicitly given; use the one from the PNGDIB object.
		// (this is the normal case)
		pdib = p2d->pdib;
		p2d->pdib = NULL;
	}
	if(pdib) {
		free((void*)pdib);
	}
}

PNGDIB* pngdib_init(void)
{
	struct p2d_struct *p2d;

	p2d = (struct p2d_struct *)calloc(sizeof(struct p2d_struct),1);

	// initialize common fields:
	if(p2d) {
		p2d->errmsg = (TCHAR*)calloc(PNGDIB_ERRMSG_MAX,sizeof(TCHAR));
	}

	return p2d;
}

int pngdib_done(PNGDIB *p2d)
{
	if(!p2d) return 0;

	if(p2d->errmsg) free(p2d->errmsg);

	free(p2d);
	return 1;
}

TCHAR* pngdib_get_error_msg(PNGDIB *p2d)
{
	return p2d->errmsg;
}

void pngdib_set_userdata(PNGDIB* p2d, void* userdata)
{
	p2d->userdata = userdata;
}

void* pngdib_get_userdata(PNGDIB* p2d)
{
	return p2d->userdata;
}

void pngdib_p2d_set_png_read_fn(PNGDIB *p2d, pngdib_read_cb_type readfunc)
{
	p2d->read_cb = readfunc;
}

void pngdib_p2d_set_use_file_bg(PNGDIB *p2d, int flag)
{
	p2d->use_file_bg_flag = flag;
}

void pngdib_p2d_set_custom_bg(PNGDIB *p2d, unsigned char r,
								  unsigned char g, unsigned char b)
{
	p2d->bgcolor.red = r;
	p2d->bgcolor.green = g;
	p2d->bgcolor.blue = b;
	p2d->use_custom_bg_flag = 1;
}

void pngdib_p2d_set_gamma_correction(PNGDIB *p2d, int flag, double screen_gamma)
{
	p2d->screen_gamma = screen_gamma;
	p2d->gamma_correction = flag;
}

int pngdib_p2d_get_dib(PNGDIB *p2d,
   BITMAPINFOHEADER **ppdib, int *pdibsize)
{
	*ppdib = p2d->pdib;
	if(pdibsize) *pdibsize = p2d->dibsize;
	return 1;
}	

int pngdib_p2d_get_dibbits(PNGDIB *p2d, void **ppbits, int *pbitsoffset, int *pbitssize)
{
	*ppbits = p2d->pbits;
	if(pbitsoffset) *pbitsoffset = p2d->bits_offs;
	if(pbitssize) *pbitssize = p2d->bitssize;
	return 1;
}

int pngdib_p2d_get_density(PNGDIB *p2d, int *pres_x, int *pres_y, int *pres_units)
{
	if(p2d->res_valid) {
		*pres_x = p2d->res_x;
		*pres_y = p2d->res_y;
		*pres_units = p2d->res_units;
		return 1;
	}
	*pres_x = 1;
	*pres_y = 1;
	*pres_units = 0;
	return 0;
}

int pngdib_p2d_get_bgcolor(PNGDIB *p2d, unsigned char *pr, unsigned char *pg, unsigned char *pb)
{
	if(p2d->bgcolor_returned) {
		*pr = p2d->bgcolor.red;
		*pg = p2d->bgcolor.green;
		*pb = p2d->bgcolor.blue;
		return 1;
	}
	return 0;
}

void pngdib_get_libpng_version(TCHAR *buf, int buflen)
{
#ifdef UNICODE
	StringCchPrintf(buf,buflen,_T("%S"),png_get_libpng_ver(NULL));
#else
	StringCchPrintf(buf,buflen,"%s",png_get_libpng_ver(NULL));
#endif
}

#endif TWPNG_SUPPORT_VIEWER
