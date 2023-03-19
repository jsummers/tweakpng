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
#include <malloc.h>
#include <math.h>

#include <png.h>

#include "pngtodib.h"
#include <strsafe.h>

#define P2D_ERRMSG_MAX 200

// Color correction methods
#define P2D_CC_GAMMA 1
#define P2D_CC_SRGB  2

// Names of RGB samples
#define P2D_SM_R 0 // red
#define P2D_SM_G 1 // green
#define P2D_SM_B 2 // blue

struct p2d_globals_struct {
#if 0
	p2d_byte *linear_to_srgb255_table;
	p2d_byte *identity255_tablex;
#endif
	int x;
};

struct p2d_color255_struct {
	p2d_byte sm255[3]; // Indexed by P2D_SM_*
};

struct p2d_color_fltpt_struct {
	double sm[3]; // Indexed by P2D_SM_*
};

// Colorspace descriptor
struct p2d_csdescr_struct {
	int type; // P2D_CC_*
	double file_gamma; // Used if .type==P2D_CC_GAMMA
};

struct p2d_struct {
	void *userdata;
	struct p2d_globals_struct *g; // Data that can be shared with multiple instances.

	TCHAR *errmsg;

	p2d_read_cb_type read_cb;

	png_uint_32 width, height;

	int use_file_bg_flag;

	struct p2d_color255_struct bkgd_color_custom_srgb; // sRGB color space, 0-255
	struct p2d_color_fltpt_struct bkgd_color_custom_linear; // linear, 0.0-1.0
	int use_custom_bg_flag;

	struct p2d_color255_struct bkgd_color_applied_src;
	struct p2d_color255_struct bkgd_color_applied_srgb;
	struct p2d_color_fltpt_struct bkgd_color_applied_linear;
	int bkgd_color_applied_flag; // Is bkgd_color_applied_* valid?

	int color_correction_enabled;
	int handle_trans; // Do we need to apply a background color?

	BITMAPINFOHEADER*  dib_header;
	RGBQUAD*   dib_palette;
	p2d_byte*  dib_bits;
	size_t     dib_size;
	int        res_x,res_y;
	int        res_units;
	int        res_valid;  // are res_x, res_y, res_units valid?

	png_structp png_ptr;
	png_infop info_ptr;

	int is_grayscale;
	int pngf_bit_depth;
	int color_type;

	// Color information about the source image. The destination image is always sRGB.
	struct p2d_csdescr_struct csdescr;

	int pngf_palette_entries;
	png_colorp pngf_palette;

	p2d_byte **dib_row_pointers;

	double *src255_to_linear_table;
	p2d_byte *src255_to_srgb255_table;
#if 0
	const p2d_byte *linear_to_srgb255_table;
#endif

	int dib_palette_entries;
	int need_gray_palette;
	int color_correct_gray_palette;
};

struct errstruct {
	jmp_buf *jbufp;
	TCHAR *errmsg;
};

static void pngd_get_error_message(int errcode, TCHAR *e, int e_len)
{
	switch(errcode) {
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
	StringCchPrintf(errinfop->errmsg,P2D_ERRMSG_MAX,_T("[libpng] %S"),err_msg);
#else
	StringCchPrintf(errinfop->errmsg,P2D_ERRMSG_MAX,"[libpng] %s",err_msg);
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

// Reads pHYs chunk, sets p2d->res_*.
static void p2d_read_density(P2D *p2d)
{
	int has_phys;
	int res_unit_type;
	png_uint_32 res_x, res_y;

	has_phys=png_get_valid(p2d->png_ptr,p2d->info_ptr,PNG_INFO_pHYs);
	if(!has_phys) return;

	png_get_pHYs(p2d->png_ptr,p2d->info_ptr,&res_x,&res_y,&res_unit_type);
	if(res_x<1 || res_y<1) return;

	p2d->res_x=res_x;
	p2d->res_y=res_y;
	p2d->res_units=res_unit_type;
	p2d->res_valid=1;
}

static void p2d_read_bgcolor(P2D *p2d)
{
	png_color_16p bg_colorp;
	p2d_byte tmpcolor;
	int has_bkgd;
	int k;

	if(!p2d->use_file_bg_flag) {
		// Using the background from the file is disabled.
		return;
	}

	has_bkgd = png_get_bKGD(p2d->png_ptr, p2d->info_ptr, &bg_colorp);
	if(!has_bkgd) {
		return;
	}

	if(p2d->is_grayscale) {
		if(p2d->pngf_bit_depth<8) {
			tmpcolor = (p2d_byte) ( (bg_colorp->gray*255)/( (1<<p2d->pngf_bit_depth)-1 ) );
		}
		else if(p2d->pngf_bit_depth==16) {
			tmpcolor = (p2d_byte)(bg_colorp->gray>>8);
		}
		else {
			tmpcolor = (p2d_byte)(bg_colorp->gray);
		}

		p2d->bkgd_color_applied_src.sm255[P2D_SM_R] = p2d->bkgd_color_applied_src.sm255[P2D_SM_G] =
			p2d->bkgd_color_applied_src.sm255[P2D_SM_B] = tmpcolor;
		p2d->bkgd_color_applied_flag = 1;
	}
	else if(p2d->pngf_bit_depth<=8) { // RGB[A]8 or palette
		p2d->bkgd_color_applied_src.sm255[P2D_SM_R] =(p2d_byte)(bg_colorp->red);
		p2d->bkgd_color_applied_src.sm255[P2D_SM_G] =(p2d_byte)(bg_colorp->green);
		p2d->bkgd_color_applied_src.sm255[P2D_SM_B] =(p2d_byte)(bg_colorp->blue);
		p2d->bkgd_color_applied_flag = 1;
	}
	else {
		p2d->bkgd_color_applied_src.sm255[P2D_SM_R] =(p2d_byte)(bg_colorp->red>>8);
		p2d->bkgd_color_applied_src.sm255[P2D_SM_G] =(p2d_byte)(bg_colorp->green>>8);
		p2d->bkgd_color_applied_src.sm255[P2D_SM_B] =(p2d_byte)(bg_colorp->blue>>8);
		p2d->bkgd_color_applied_flag = 1;
	}

	if(!p2d->bkgd_color_applied_flag) return;

	for(k=0;k<3;k++) {
		p2d->bkgd_color_applied_linear.sm[k] = p2d->src255_to_linear_table[p2d->bkgd_color_applied_src.sm255[k]];
		p2d->bkgd_color_applied_srgb.sm255[k] = p2d->src255_to_srgb255_table[p2d->bkgd_color_applied_src.sm255[k]];
	}
}

static void p2d_read_gamma(P2D *p2d)
{
	int intent;
	struct p2d_csdescr_struct *cs;

	if(!p2d->color_correction_enabled) return;
	cs = &p2d->csdescr;

	if (png_get_sRGB(p2d->png_ptr, p2d->info_ptr, &intent)) {
		cs->type = P2D_CC_SRGB;
		cs->file_gamma = 0.45455;
	}
	else if(png_get_gAMA(p2d->png_ptr, p2d->info_ptr, &cs->file_gamma)) {
		if(cs->file_gamma<0.01) cs->file_gamma=0.01;
		if(cs->file_gamma>10.0) cs->file_gamma=10.0;
		cs->type = P2D_CC_GAMMA;
	}
	else {
		// Assume unlabeled images are sRGB.
		cs->type = P2D_CC_SRGB;
		cs->file_gamma = 0.45455;
	}
}

// Create a color-corrected grayscale palette
static void p2d_make_gray_palette(P2D *p2d)
{
	int i;
	p2d_byte v;
	int mult;

	switch(p2d->dib_palette_entries) {
	case 2: mult=255; break;
	case 4: mult=85; break;
	case 16: mult=17; break;
	default: mult=1;
	}

	for(i=0;i<p2d->dib_palette_entries;i++) {
		if(p2d->color_correct_gray_palette)
			v=p2d->src255_to_srgb255_table[i*mult];
		else
			v=i*mult;
		p2d->dib_palette[i].rgbRed = v;
		p2d->dib_palette[i].rgbGreen = v;
		p2d->dib_palette[i].rgbBlue = v;
	}
}

// Expand 2bpp to 4bpp
static int p2d_convert_2bit_to_4bit(P2D *p2d)
{
	p2d_byte *tmprow;
	int i,j;

	tmprow = (p2d_byte*)malloc((p2d->width+3)/4 );
	if(!tmprow) { return 0; }

	for(j=0;j<(int)p2d->height;j++) {
		CopyMemory(tmprow, p2d->dib_row_pointers[j], (p2d->width+3)/4 );
		ZeroMemory(p2d->dib_row_pointers[j], (p2d->width+1)/2 );

		for(i=0;i<(int)p2d->width;i++) {
			p2d->dib_row_pointers[j][i/2] |= 
				( ((tmprow[i/4] >> (2*(3-i%4)) ) & 0x03)<< (4*(1-i%2)) );
		}
	}
	free((void*)tmprow);
	return 1;
}

static double gamma_to_linear_sample(double v, double gamma)
{
	return pow(v,gamma);
}

static double srgb_to_linear_sample(double v_srgb)
{
	if(v_srgb<=0.04045) {
		return v_srgb/12.92;
	}
	else {
		return pow( (v_srgb+0.055)/(1.055) , 2.4);
	}
}

static double linear_to_srgb_sample(double v_linear)
{
	if(v_linear <= 0.0031308) {
		return 12.92*v_linear;
	}
	return 1.055*pow(v_linear,1.0/2.4) - 0.055;
}

static int p2d_make_global_tables(struct p2d_globals_struct *g)
{
#if 0
	int n;
	double val;

	if(!g->linear_to_srgb255_table) {
		g->linear_to_srgb255_table = (p2d_byte*)malloc(256*sizeof(p2d_byte));
		if(!g->linear_to_srgb255_table) return 0;

		for(n=0;n<256;n++) {
			val = linear_to_srgb_sample(((double)n)/255.0);
			g->linear_to_srgb255_table[n] = (p2d_byte)(0.5+val*255.0);
		}
	}

	if(!g->identity255_table) {
		g->identity255_table = (p2d_byte*)malloc(256*sizeof(p2d_byte));
		if(!g->identity255_table) return 0;

		for(n=0;n<256;n++) {
			g->identity255_table[n] = n;
		}
	}
#endif
	return 1;
}

static int p2d_make_color_correction_tables(P2D *p2d)
{
	int n;
	double val_src;
	double val_linear;
	double val_dst;

	// TODO: We should only make the tables that we'll actually use.
	p2d_make_global_tables(p2d->g);
#if 0
	if(p2d->color_correction_enabled) {
		p2d->linear_to_srgb255_table = p2d->g->linear_to_srgb255_table;
	}
	else {
		p2d->linear_to_srgb255_table = p2d->g->identity255_table;
	}
#endif

	p2d->src255_to_linear_table = (double*)malloc(256*sizeof(double));
	if(!p2d->src255_to_linear_table) return 0;

	p2d->src255_to_srgb255_table = (p2d_byte*)malloc(256*sizeof(p2d_byte));
	if(!p2d->src255_to_srgb255_table) return 0;

	for(n=0;n<256;n++) {
		val_src = ((double)n)/255.0;

		if(p2d->color_correction_enabled) {
			if(p2d->csdescr.type==P2D_CC_SRGB) {
				val_linear = srgb_to_linear_sample(val_src);
			}
			else if(p2d->csdescr.type==P2D_CC_GAMMA) {
				val_linear =  gamma_to_linear_sample(val_src,1.0/p2d->csdescr.file_gamma);
			}
			else {
				val_linear = val_src;
			}

			// TODO: This is only needed if there is partial transparency.
			p2d->src255_to_linear_table[n] = val_linear;
			
			val_dst = linear_to_srgb_sample(val_linear);
			p2d->src255_to_srgb255_table[n] = (p2d_byte)(0.5+val_dst*255.0);
		}
		else {
			// "dummy" tables
			p2d->src255_to_linear_table[n] = val_src;
			p2d->src255_to_srgb255_table[n] = n;
		}
	}
	return 1;
}

// Handle cases where an RGB PNG image can be read directly into the DIB image
// buffer.
static int decode_strategy_rgb(P2D *p2d, int binarytrns)
{
	size_t i, j;
	int k;
	struct p2d_color255_struct trns_key_color;
	struct p2d_color255_struct c;
	int is_bkgd_pixel;
	png_bytep trans_alpha;
	png_color_16p trans_color;
	int num_trans;

	if(binarytrns) {
		// Read the transparency key color from the PNG file.
		png_get_tRNS(p2d->png_ptr, p2d->info_ptr, &trans_alpha, &num_trans, &trans_color);
		trns_key_color.sm255[P2D_SM_R] = (p2d_byte)trans_color->red;
		trns_key_color.sm255[P2D_SM_G] = (p2d_byte)trans_color->green;
		trns_key_color.sm255[P2D_SM_B] = (p2d_byte)trans_color->blue;
	}

	png_set_bgr(p2d->png_ptr);

	png_read_image(p2d->png_ptr, p2d->dib_row_pointers);

	// Do color correction + application of binary transparency, if necessary.
	// With no transparency, sRGB source images don't need color correction.
	if(p2d->csdescr.type==P2D_CC_GAMMA || binarytrns) {
		for(j=0;j<p2d->height;j++) {
			for(i=0;i<p2d->width;i++) {
				for(k=0;k<3;k++) {
					c.sm255[2-k] = p2d->dib_row_pointers[j][i*3+k];
				}
				is_bkgd_pixel=0;
				if(binarytrns) {
					if(c.sm255[P2D_SM_R]==trns_key_color.sm255[P2D_SM_R] &&
					   c.sm255[P2D_SM_G]==trns_key_color.sm255[P2D_SM_G] &&
					   c.sm255[P2D_SM_B]==trns_key_color.sm255[P2D_SM_B])
					{
						is_bkgd_pixel=1;
					}
				}
				if(is_bkgd_pixel) {
					for(k=0;k<3;k++) {
						p2d->dib_row_pointers[j][i*3+k] = p2d->bkgd_color_applied_srgb.sm255[2-k];
					}
				}
				else {
					for(k=0;k<3;k++) {
						p2d->dib_row_pointers[j][i*3+k] = p2d->src255_to_srgb255_table[c.sm255[2-k]];
					}
				}
			}
		}
	}
	return 1;
}

static int decode_strategy_rgba(P2D *p2d)
{
	size_t i, j;
	p2d_byte *pngimage = NULL;
	p2d_byte **pngrowpointers = NULL;
	double lsample; // linear sample value;
	double bsample; // linear sample, composited with background color
	double a; // alpha value
	p2d_byte a_int;
	int k;

	pngimage = (p2d_byte*)malloc(4*p2d->width*p2d->height);
	if(!pngimage) goto done;
	pngrowpointers = (p2d_byte**)malloc(p2d->height*sizeof(p2d_byte*));
	if(!pngrowpointers) goto done;
	for(j=0;j<p2d->height;j++) {
		pngrowpointers[j] = &pngimage[j*4*p2d->width];
	}

	png_read_image(p2d->png_ptr, pngrowpointers);

	for(j=0;j<p2d->height;j++) {
		for(i=0;i<p2d->width;i++) {
			a_int = pngrowpointers[j][i*4+3];

			if(a_int==255) {
				// Fast path if fully opaque
				for(k=0;k<3;k++) {
					p2d->dib_row_pointers[j][i*3+2-k] = p2d->src255_to_srgb255_table[pngrowpointers[j][i*4+k]];
				}
			}
			else if(a_int==0) {
				// Fast path if fully transparent
				for(k=0;k<3;k++) {
					p2d->dib_row_pointers[j][i*3+2-k] = p2d->bkgd_color_applied_srgb.sm255[k];
				}
			}
			else {
				a = ((double)a_int)/255.0;

				for(k=0;k<3;k++) {
					lsample = p2d->src255_to_linear_table[pngrowpointers[j][i*4+k]];
					bsample = a*lsample + (1.0-a)*p2d->bkgd_color_applied_linear.sm[k];
					// Fast, low quality:
					//p2d->dib_row_pointers[j][i*3+2-k] = p2d->linear_to_srgb255_table[(p2d_byte)(0.5+bsample*255.0)];
					// Slow, high quality:
					if(p2d->color_correction_enabled)
						p2d->dib_row_pointers[j][i*3+2-k] = (p2d_byte)(0.5+255.0*linear_to_srgb_sample(bsample));
					else
						p2d->dib_row_pointers[j][i*3+2-k] = (p2d_byte)(0.5+255.0*bsample);
				}
			}
		}
	}

done:
	if(pngimage) free(pngimage);
	if(pngrowpointers) free(pngrowpointers);
	return 1;
}

// gray+alpha
static int decode_strategy_graya(P2D *p2d, int tocolor)
{
	size_t i, j;
	p2d_byte *pngimage = NULL;
	p2d_byte **pngrowpointers = NULL;
	double lsample; // linear gray sample
	double bsample; // linear sample, composited with background color
	double a; // alpha value
	int k;
	p2d_byte a_int, gray_int;

	pngimage = (p2d_byte*)malloc(2*p2d->width*p2d->height);
	if(!pngimage) goto done;
	pngrowpointers = (p2d_byte**)malloc(p2d->height*sizeof(p2d_byte*));
	if(!pngrowpointers) goto done;
	for(j=0;j<p2d->height;j++) {
		pngrowpointers[j] = &pngimage[j*2*p2d->width];
	}

	png_read_image(p2d->png_ptr, pngrowpointers);

	for(j=0;j<p2d->height;j++) {
		for(i=0;i<p2d->width;i++) {
			gray_int = pngrowpointers[j][i*2+0];
			a_int    = pngrowpointers[j][i*2+1];

			if(a_int==255) {
				// Fully opaque
				if(tocolor) {
					for(k=0;k<3;k++) {
						p2d->dib_row_pointers[j][i*3+2-k] = p2d->src255_to_srgb255_table[gray_int];
					}
				}
				else {
					p2d->dib_row_pointers[j][i] = p2d->src255_to_srgb255_table[gray_int];
				}
			}
			else if(a_int==0) {
				// Fully transparent
				if(tocolor) {
					for(k=0;k<3;k++) {
						p2d->dib_row_pointers[j][i*3+2-k] = p2d->bkgd_color_applied_srgb.sm255[k];
					}
				}
				else {
					p2d->dib_row_pointers[j][i] = p2d->bkgd_color_applied_srgb.sm255[P2D_SM_R];
				}
			}
			else {
				// Partially transparent
				lsample = p2d->src255_to_linear_table[gray_int];
				a = ((double)a_int)/255.0;

				if(tocolor) {
					for(k=0;k<3;k++) {
						bsample = a*lsample + (1.0-a)*p2d->bkgd_color_applied_linear.sm[k];
						// Fast, low quality:
						//p2d->dib_row_pointers[j][i*3+2-k] = p2d->linear_to_srgb255_table[(p2d_byte)(0.5+bsample*255.0)];
						// Slow, high quality:
						if(p2d->color_correction_enabled)
							p2d->dib_row_pointers[j][i*3+2-k] = (p2d_byte)(0.5+255.0*linear_to_srgb_sample(bsample));
						else
							p2d->dib_row_pointers[j][i*3+2-k] = (p2d_byte)(0.5+255.0*bsample);
					}
				}
				else {
					bsample = a*lsample + (1.0-a)*p2d->bkgd_color_applied_linear.sm[P2D_SM_R];
					// Fast, low quality:
					//p2d->dib_row_pointers[j][i] = p2d->linear_to_srgb255_table[(p2d_byte)(0.5+bsample*255.0)];
					// Slow, high quality:
					if(p2d->color_correction_enabled)
						p2d->dib_row_pointers[j][i] = (p2d_byte)(0.5+255.0*linear_to_srgb_sample(bsample));
					else
						p2d->dib_row_pointers[j][i] = (p2d_byte)(0.5+255.0*bsample);
				}
			}
		}
	}

done:
	if(pngimage) free(pngimage);
	if(pngrowpointers) free(pngrowpointers);
	return 1;
}

static int decode_strategy_palette(P2D *p2d)
{
	int i;
	int retval=0;
	png_bytep trans_alpha = 0;
	png_color_16p trans_color;
	int num_trans;
	struct p2d_color_fltpt_struct lcolor;
	double trns_alpha_1;
	int k;

	// Copy the PNG palette to the DIB palette
	if(p2d->pngf_palette_entries != p2d->dib_palette_entries) return 0;

	num_trans=0;
	if(p2d->handle_trans) {
		png_get_tRNS(p2d->png_ptr, p2d->info_ptr, &trans_alpha, &num_trans, &trans_color);
	}
	// Copy the PNG palette to the DIB palette, handling color correction
	// and transparency in the process.
	for(i=0;i<p2d->dib_palette_entries;i++) {
		if(i>=num_trans || trans_alpha[i]==255) { // Fully opaque
			p2d->dib_palette[i].rgbRed   = p2d->src255_to_srgb255_table[p2d->pngf_palette[i].red];
			p2d->dib_palette[i].rgbGreen = p2d->src255_to_srgb255_table[p2d->pngf_palette[i].green];
			p2d->dib_palette[i].rgbBlue  = p2d->src255_to_srgb255_table[p2d->pngf_palette[i].blue];
		}
		else if(trans_alpha[i]==0) { // Fully transparent
			p2d->dib_palette[i].rgbRed   = p2d->bkgd_color_applied_srgb.sm255[P2D_SM_R];
			p2d->dib_palette[i].rgbGreen = p2d->bkgd_color_applied_srgb.sm255[P2D_SM_G];
			p2d->dib_palette[i].rgbBlue  = p2d->bkgd_color_applied_srgb.sm255[P2D_SM_B];
		}
		else { // Partially transparent
			lcolor.sm[P2D_SM_R] = p2d->src255_to_linear_table[p2d->pngf_palette[i].red];
			lcolor.sm[P2D_SM_G] = p2d->src255_to_linear_table[p2d->pngf_palette[i].green];
			lcolor.sm[P2D_SM_B] = p2d->src255_to_linear_table[p2d->pngf_palette[i].blue];

			// Apply background color
			trns_alpha_1 = ((double)trans_alpha[i])/255.0;
			for(k=0;k<3;k++) {
				lcolor.sm[k] = trns_alpha_1*lcolor.sm[k] + (1.0-trns_alpha_1)*p2d->bkgd_color_applied_linear.sm[k];
			}

			if(p2d->color_correction_enabled) {
				p2d->dib_palette[i].rgbRed   = (p2d_byte)(0.5+255.0*linear_to_srgb_sample(lcolor.sm[P2D_SM_R]));
				p2d->dib_palette[i].rgbGreen = (p2d_byte)(0.5+255.0*linear_to_srgb_sample(lcolor.sm[P2D_SM_G]));
				p2d->dib_palette[i].rgbBlue  = (p2d_byte)(0.5+255.0*linear_to_srgb_sample(lcolor.sm[P2D_SM_B]));
			}
			else {
				p2d->dib_palette[i].rgbRed   = (p2d_byte)(0.5+255.0*lcolor.sm[P2D_SM_R]);
				p2d->dib_palette[i].rgbGreen = (p2d_byte)(0.5+255.0*lcolor.sm[P2D_SM_G]);
				p2d->dib_palette[i].rgbBlue  = (p2d_byte)(0.5+255.0*lcolor.sm[P2D_SM_B]);
			}
		}
	}

	// Directly read the image into the DIB.
	png_read_image(p2d->png_ptr, p2d->dib_row_pointers);

	if(p2d->pngf_bit_depth==2) {
		// Special handling for this bit depth, since it doesn't exist in DIBs.
		if(!p2d_convert_2bit_to_4bit(p2d)) goto done;
	}

	retval=1;
done:
	return retval;
}

// Read a grayscale image, of a type that can be converted to a paletted DIB.
// It's assumed that a (color corrected) grayscale palette has already been
// generated.
// This strategy could be used for:
//   - All 1, 2, 4-bit grayscale
//   - 8-bit grayscale with no transparency, or binary transparency
//   - 16-bit grayscale with no transparency
// It cannot be used for:
//   - Any image with an alpha channel
//   - 16-bit grayscale with binary transparency
static int decode_strategy_gray_to_pal(P2D *p2d)
{
	int retval=0;
	png_bytep trans_alpha;
	png_color_16p trans_color;
	int num_trans;

	if(p2d->handle_trans) {
		// Binary transparency is handled by changing one of the palette colors
		// to the background color.
		png_get_tRNS(p2d->png_ptr, p2d->info_ptr, &trans_alpha, &num_trans, &trans_color);
		if(trans_color->gray>=0 && trans_color->gray<p2d->dib_palette_entries) {
			p2d->dib_palette[trans_color->gray].rgbRed   = p2d->bkgd_color_applied_srgb.sm255[P2D_SM_R];
			p2d->dib_palette[trans_color->gray].rgbGreen = p2d->bkgd_color_applied_srgb.sm255[P2D_SM_G];
			p2d->dib_palette[trans_color->gray].rgbBlue  = p2d->bkgd_color_applied_srgb.sm255[P2D_SM_B];
		}
	}

	// Directly read the image into the DIB.
	png_read_image(p2d->png_ptr, p2d->dib_row_pointers);

	if(p2d->pngf_bit_depth==2) {
		// Special handling for this bit depth, since it doesn't exist in DIBs.
		if(!p2d_convert_2bit_to_4bit(p2d)) goto done;
	}

	retval=1;
done:
	return retval;
}

int p2d_run(P2D *p2d)
{
	jmp_buf jbuf;
	struct errstruct errinfo;
	int interlace_type;
	size_t dib_palette_offs;
	size_t dib_bits_offs;
	size_t dib_bits_size;
	int dib_bpp, dib_bytesperrow;
	int j;
	int retval;
	enum p2d_strategy { P2D_ST_NONE, P2D_ST_RGB, P2D_ST_RGBA, P2D_ST_GRAYA,
	  P2D_ST_PALETTE, P2D_ST_GRAY_TO_PAL };
	enum p2d_strategy decode_strategy;
	int strategy_tocolor=0;
	int strategy_binarytrns=0;
	int bg_is_gray;
	int has_trns = 0;
	int k;

	retval=PNGD_E_ERROR;
	p2d->png_ptr=NULL;
	p2d->info_ptr=NULL;
	p2d->dib_row_pointers=NULL;
	decode_strategy= P2D_ST_NONE;

	StringCchCopy(p2d->errmsg,P2D_ERRMSG_MAX,_T(""));


	if(p2d->use_custom_bg_flag) {
		p2d->bkgd_color_applied_srgb = p2d->bkgd_color_custom_srgb; // struct copy
		p2d->bkgd_color_applied_flag = 1;
	}
	else {
		p2d->bkgd_color_applied_srgb.sm255[P2D_SM_R] = 255; // Should never get used. If the
		p2d->bkgd_color_applied_srgb.sm255[P2D_SM_G] = 128; // background turns orange, it's a bug.
		p2d->bkgd_color_applied_srgb.sm255[P2D_SM_B] =  0;
	}

	if(p2d->color_correction_enabled) {
		// Also store the custom background color in a linear colorspace, since that's
		// what we'll need if we have to apply it to the image.
		for(k=0;k<3;k++) {
			p2d->bkgd_color_applied_linear.sm[k] = srgb_to_linear_sample(((double)p2d->bkgd_color_applied_srgb.sm255[k])/255.0);
		}
	}
	else {
		for(k=0;k<3;k++) {
			p2d->bkgd_color_applied_linear.sm[k] = ((double)p2d->bkgd_color_applied_srgb.sm255[k])/255.0;
		}
	}


	// Set the user-defined pointer to point to our jmp_buf. This will
	// hopefully protect against potentially different sized jmp_buf's in
	// libpng, while still allowing this library to be threadsafe.
	errinfo.jbufp = &jbuf;
	errinfo.errmsg = p2d->errmsg;

	p2d->png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,(void*)(&errinfo),
		my_png_error_fn, my_png_warning_fn);
	if(!p2d->png_ptr) { retval=PNGD_E_NOMEM; goto done; }

	png_set_user_limits(p2d->png_ptr,100000,100000); // max image dimensions

#if PNG_LIBPNG_VER >= 10400
	// Number of ancillary chunks stored.
	// I don't think we need any of these, but there appears to be no
	// way to set the limit to 0. (0 is reserved to mean "unlimited".)
	// I'll just set it to an arbitrary low number.
	png_set_chunk_cache_max(p2d->png_ptr,50);
#endif

#if PNG_LIBPNG_VER >= 10401
	png_set_chunk_malloc_max(p2d->png_ptr,1000000);
#endif

	p2d->info_ptr = png_create_info_struct(p2d->png_ptr);
	if(!p2d->info_ptr) {
		//png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
		retval=PNGD_E_NOMEM; goto done;
	}

	if(setjmp(jbuf)) {
		// we'll get here if an error occurred in any of the following
		// png_ functions

		retval=PNGD_E_LIBPNG;
		goto done;
	}

	png_set_read_fn(p2d->png_ptr, (void*)p2d, my_png_read_fn_custom);

	png_read_info(p2d->png_ptr, p2d->info_ptr);

	png_get_IHDR(p2d->png_ptr, p2d->info_ptr, &p2d->width, &p2d->height, &p2d->pngf_bit_depth, &p2d->color_type,
		&interlace_type, NULL, NULL);

	p2d->is_grayscale = !(p2d->color_type&PNG_COLOR_MASK_COLOR);

	has_trns = png_get_valid(p2d->png_ptr,p2d->info_ptr,PNG_INFO_tRNS);

	p2d_read_gamma(p2d);

	if(!p2d_make_color_correction_tables(p2d)) goto done;

	p2d_read_bgcolor(p2d);

	p2d_read_density(p2d);

	if(p2d->bkgd_color_applied_flag) {
		if(has_trns || (p2d->color_type & PNG_COLOR_MASK_ALPHA)) {
			p2d->handle_trans=1;
		}
	}

	// Figure out if the background color is a shade of gray
	bg_is_gray=1;
	if(p2d->handle_trans) {
		if(p2d->bkgd_color_applied_srgb.sm255[P2D_SM_R]!=p2d->bkgd_color_applied_srgb.sm255[P2D_SM_G] ||
			p2d->bkgd_color_applied_srgb.sm255[P2D_SM_R]!=p2d->bkgd_color_applied_srgb.sm255[P2D_SM_B])
		{
			bg_is_gray=0;
		}
	}

	//////// Decide on DIB image type, etc.

	// This is inevitably a complicated part of the code, because we have to
	// cover a lot of different cases, which overlap in various ways.

	p2d->dib_palette_entries=0; // default

	if((p2d->color_type & PNG_COLOR_MASK_ALPHA) && !p2d->handle_trans) {
		// Image has an alpha channel, but we don't have a background color
		// to use. Have libpng strip the alpha channel.
		png_set_strip_alpha(p2d->png_ptr);
		p2d->color_type -= PNG_COLOR_MASK_ALPHA;
	}

	if(p2d->color_type==PNG_COLOR_TYPE_GRAY && p2d->pngf_bit_depth==16 && p2d->handle_trans) {
		// 16-bit grayscale w/binary transparency is an unusual case, so get it out
		// of the way first.
		png_set_tRNS_to_alpha(p2d->png_ptr);
		png_set_strip_16(p2d->png_ptr);
		if(bg_is_gray) {
			decode_strategy=P2D_ST_GRAYA; strategy_tocolor=0;
			dib_bpp=8;
			p2d->need_gray_palette=1; p2d->color_correct_gray_palette=0;
			p2d->dib_palette_entries=256;
		}
		else { // color background
			decode_strategy=P2D_ST_GRAYA; strategy_tocolor=1;
			dib_bpp=24;
		}
	}
	else if(p2d->color_type==PNG_COLOR_TYPE_GRAY) {
		// All other grayscale (no alpha channel) cases are handled here.
		decode_strategy=P2D_ST_GRAY_TO_PAL;

		if(p2d->pngf_bit_depth==2)
			dib_bpp=4;
		else if(p2d->pngf_bit_depth==16)
			dib_bpp=8;
		else
			dib_bpp=p2d->pngf_bit_depth;

		p2d->need_gray_palette=1; p2d->color_correct_gray_palette=1;
		if(p2d->pngf_bit_depth==16) {
			png_set_strip_16(p2d->png_ptr);
			p2d->dib_palette_entries = 256;
		}
		else {
			p2d->dib_palette_entries = 1 << p2d->pngf_bit_depth;
		}
	}
	else if(p2d->color_type==PNG_COLOR_TYPE_GRAY_ALPHA) {

		if(bg_is_gray) {
			// Applying a gray background.
			decode_strategy=P2D_ST_GRAYA; strategy_tocolor=0;
			dib_bpp=8;
			p2d->need_gray_palette=1; p2d->color_correct_gray_palette=0;
			p2d->dib_palette_entries=256;
		}
		else {
			// Applying a color background to a grayscale image.
			decode_strategy=P2D_ST_GRAYA; strategy_tocolor=1;
			dib_bpp=24;
		}

		if(p2d->pngf_bit_depth==16) {
			png_set_strip_16(p2d->png_ptr);
		}
	}
	else if(p2d->color_type==PNG_COLOR_TYPE_RGB && !p2d->handle_trans) {
		// RGB with no transparency.
		decode_strategy=P2D_ST_RGB;
		dib_bpp=24;
		if(p2d->pngf_bit_depth==16) {
			png_set_strip_16(p2d->png_ptr);
		}
	}
	else if(p2d->color_type==PNG_COLOR_TYPE_RGB) {
		// RGB with binary transparency.
		if(p2d->pngf_bit_depth==16) {
			// If 16-bit, have libpng convert to 8bit with an alpha channel.
			decode_strategy=P2D_ST_RGBA;
			dib_bpp=24;
			png_set_tRNS_to_alpha(p2d->png_ptr);
			png_set_strip_16(p2d->png_ptr);
		}
		else {
			// If 8-bit, handle everything ourselves.
			decode_strategy=P2D_ST_RGB; strategy_binarytrns=1;
			dib_bpp=24;
		}
	}
	else if(p2d->color_type==PNG_COLOR_TYPE_RGB_ALPHA) {
		decode_strategy=P2D_ST_RGBA;
		dib_bpp=24;
		if(p2d->pngf_bit_depth==16) {
			png_set_strip_16(p2d->png_ptr);
		}
	}
	else if(p2d->color_type==PNG_COLOR_TYPE_PALETTE) {
		png_get_PLTE(p2d->png_ptr,p2d->info_ptr,&p2d->pngf_palette,&p2d->pngf_palette_entries);

		if(p2d->pngf_palette_entries > (1<<p2d->pngf_bit_depth)) {
			// libpng <= 1.6.18 does not sanitize oversized PLTE chunks.
			// This way, we ignore any extra palette entries, instead of
			// creating a DIB with an oversized palette.
			p2d->pngf_palette_entries = 1<<p2d->pngf_bit_depth;
		}

		p2d->dib_palette_entries = p2d->pngf_palette_entries;
		if(p2d->pngf_bit_depth==2)
			dib_bpp=4;
		else
			dib_bpp=p2d->pngf_bit_depth;
		decode_strategy=P2D_ST_PALETTE;
	}

	if(decode_strategy==P2D_ST_NONE) {
		StringCchPrintf(p2d->errmsg,P2D_ERRMSG_MAX,_T("Viewer doesn't support this image type"));
		goto done;
	}


	//////// Calculate the size of the DIB, and allocate memory for it.

	// DIB scanlines are padded to 4-byte boundaries.
	dib_bytesperrow = (((p2d->width * dib_bpp)+31)/32)*4;

	dib_palette_offs = sizeof(BITMAPINFOHEADER);
	dib_bits_offs = dib_palette_offs + 4*p2d->dib_palette_entries;
	dib_bits_size = p2d->height*dib_bytesperrow;
	p2d->dib_size = dib_bits_offs + dib_bits_size;

	p2d->dib_header = (LPBITMAPINFOHEADER)calloc(1,p2d->dib_size);
	if(!p2d->dib_header) { retval=PNGD_E_NOMEM; goto done; }

	p2d->dib_palette = (RGBQUAD*)&((p2d_byte*)p2d->dib_header)[dib_palette_offs];
	p2d->dib_bits = &((p2d_byte*)p2d->dib_header)[dib_bits_offs];

	if(p2d->need_gray_palette) {
		p2d_make_gray_palette(p2d);
	}

	//////// Allocate dib_row_pointers, which point to each row in the DIB we allocated.

	p2d->dib_row_pointers=(p2d_byte**)malloc(p2d->height*sizeof(p2d_byte*));
	if(!p2d->dib_row_pointers) { retval=PNGD_E_NOMEM; goto done; }

	for(j=0;j<(int)p2d->height;j++) {
		p2d->dib_row_pointers[p2d->height-1-j]= &p2d->dib_bits[j*dib_bytesperrow];
	}

	//////// Read the PNG image into our DIB memory structure.

	switch(decode_strategy) {
	case P2D_ST_RGB:
		decode_strategy_rgb(p2d,strategy_binarytrns);
		break;
	case P2D_ST_RGBA:
		decode_strategy_rgba(p2d);
		break;
	case P2D_ST_GRAYA:
		decode_strategy_graya(p2d,strategy_tocolor);
		break;
	case P2D_ST_PALETTE:
		decode_strategy_palette(p2d);
		break;
	case P2D_ST_GRAY_TO_PAL:
		decode_strategy_gray_to_pal(p2d);
		break;
	default:
		retval=PNGD_E_ERROR; goto done;
	}

	png_read_end(p2d->png_ptr, p2d->info_ptr);

	// fill in the DIB header fields
	p2d->dib_header->biSize=          sizeof(BITMAPINFOHEADER);
	p2d->dib_header->biWidth=         p2d->width;
	p2d->dib_header->biHeight=        p2d->height;
	p2d->dib_header->biPlanes=        1;
	p2d->dib_header->biBitCount=      dib_bpp;
	p2d->dib_header->biCompression=   BI_RGB;
	// biSizeImage can also be 0 in uncompressed bitmaps
	p2d->dib_header->biSizeImage=     p2d->height*dib_bytesperrow;

	if(p2d->res_valid) {
		p2d->dib_header->biXPelsPerMeter= p2d->res_x;
		p2d->dib_header->biYPelsPerMeter= p2d->res_y;
	}
	else {
		p2d->dib_header->biXPelsPerMeter= 72;
		p2d->dib_header->biYPelsPerMeter= 72;
	}

	p2d->dib_header->biClrUsed=       p2d->dib_palette_entries;
	p2d->dib_header->biClrImportant=  0;

	retval = PNGD_E_SUCCESS;

done:

	if(p2d->src255_to_srgb255_table) free(p2d->src255_to_srgb255_table);
	if(p2d->src255_to_linear_table) free(p2d->src255_to_linear_table);

	if(p2d->png_ptr) png_destroy_read_struct(&p2d->png_ptr, &p2d->info_ptr, (png_infopp)NULL);
	if(p2d->dib_row_pointers) free((void*)p2d->dib_row_pointers);

	if(retval!=PNGD_E_SUCCESS) {
		if(p2d->dib_header) {
			p2d_free_dib(p2d->dib_header);
		}

		// If we don't have an error message yet, use a
		// default one based on the code
		if(!lstrlen(p2d->errmsg)) {
			pngd_get_error_message(retval,p2d->errmsg,P2D_ERRMSG_MAX);
		}
	}

	return retval;
}

void p2d_free_dib(BITMAPINFOHEADER* pdib)
{
	if(pdib) free((void*)pdib);
}

P2D* p2d_init(struct p2d_globals_struct *g)
{
	struct p2d_struct *p2d;

	if(!g) return NULL;
	p2d = (struct p2d_struct *)calloc(1,sizeof(struct p2d_struct));

	if(p2d) {
		p2d->g = g;
		p2d->errmsg = (TCHAR*)calloc(P2D_ERRMSG_MAX,sizeof(TCHAR));
	}

	return p2d;
}

int p2d_done(P2D *p2d)
{
	if(!p2d) return 0;

	if(p2d->errmsg) free(p2d->errmsg);

	free(p2d);
	return 1;
}

TCHAR* p2d_get_error_msg(P2D *p2d)
{
	return p2d->errmsg;
}

void p2d_set_userdata(P2D* p2d, void* userdata)
{
	p2d->userdata = userdata;
}

void* p2d_get_userdata(P2D* p2d)
{
	return p2d->userdata;
}

void p2d_set_png_read_fn(P2D *p2d, p2d_read_cb_type readfunc)
{
	p2d->read_cb = readfunc;
}

void p2d_set_use_file_bg(P2D *p2d, int flag)
{
	p2d->use_file_bg_flag = flag;
}

// Colors are given in sRGB color space.
void p2d_set_custom_bg(P2D *p2d, p2d_byte r, p2d_byte g, p2d_byte b)
{
	p2d->bkgd_color_custom_srgb.sm255[P2D_SM_R] = r;
	p2d->bkgd_color_custom_srgb.sm255[P2D_SM_G] = g;
	p2d->bkgd_color_custom_srgb.sm255[P2D_SM_B] = b;
	p2d->bkgd_color_custom_linear.sm[P2D_SM_R] = srgb_to_linear_sample(((double)r)/255.0);
	p2d->bkgd_color_custom_linear.sm[P2D_SM_G] = srgb_to_linear_sample(((double)g)/255.0);
	p2d->bkgd_color_custom_linear.sm[P2D_SM_B] = srgb_to_linear_sample(((double)b)/255.0);
	p2d->use_custom_bg_flag = 1;
}

void p2d_enable_color_correction(P2D *p2d, int flag)
{
	p2d->color_correction_enabled = flag;
}

int p2d_get_dib(P2D *p2d, BITMAPINFOHEADER **ppdib)
{
	*ppdib = p2d->dib_header;
	return 1;
}	

int p2d_get_dibbits(P2D *p2d, void **ppbits)
{
	*ppbits = p2d->dib_bits;
	return 1;
}

size_t p2d_get_dib_size(P2D *p2d)
{
	return p2d->dib_size;
}

int p2d_get_density(P2D *p2d, int *pres_x, int *pres_y, int *pres_units)
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

int p2d_get_bgcolor(P2D *p2d, p2d_byte *pr, p2d_byte *pg, p2d_byte *pb)
{
	if(p2d->bkgd_color_applied_flag) {
		*pr = p2d->bkgd_color_applied_srgb.sm255[P2D_SM_R];
		*pg = p2d->bkgd_color_applied_srgb.sm255[P2D_SM_G];
		*pb = p2d->bkgd_color_applied_srgb.sm255[P2D_SM_B];
		return 1;
	}
	return 0;
}

void p2d_get_libpng_version(TCHAR *buf, int buflen)
{
#ifdef UNICODE
	StringCchPrintf(buf,buflen,_T("%S"),png_get_libpng_ver(NULL));
#else
	StringCchPrintf(buf,buflen,"%s",png_get_libpng_ver(NULL));
#endif
}

struct p2d_globals_struct *p2d_create_globals()
{
	struct p2d_globals_struct *g;
	g = (struct p2d_globals_struct *)calloc(1,sizeof(struct p2d_globals_struct));
	return g;
}

void p2d_destroy_globals(struct p2d_globals_struct *g)
{
	if(g) {
#if 0
		if(g->linear_to_srgb255_table) free((void*)g->linear_to_srgb255_table);
		if(g->identity255_table) free((void*)g->identity255_table);
#endif
		free((void*)g);
	}
}

#endif TWPNG_SUPPORT_VIEWER
