// viewer.cpp - part of TweakPNG
/*
    Copyright (C) 1999-2011 Jason Summers

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

#include "tweakpng.h"

#include "pngtodib.h"
#include <strsafe.h>

#include "resource.h"

extern Viewer *g_viewer;
//extern struct windowprefs_struct window_prefs;
extern struct globals_struct globals;


void twpng_get_libpng_version(TCHAR *buf, int buflen)
{
	p2d_get_libpng_version(buf,buflen);
}

void Viewer::GlobalInit()
{
	WNDCLASS wc;

	ZeroMemory(&wc,sizeof(WNDCLASS));
	wc.style = CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS;
	wc.lpfnWndProc = (WNDPROC)WndProcViewer;
	wc.hInstance = globals.hInst;
	wc.hIcon = LoadIcon(globals.hInst,_T("ICONVIEWER"));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = NULL; //(HBRUSH)GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName =  _T("MENUVIEWER");
	wc.lpszClassName = _T("TPNGVIEWER");
	RegisterClass(&wc);

	globals.viewer_imgpos_x=globals.vborder;
	globals.viewer_imgpos_y=globals.vborder;
}

void Viewer::GlobalDestroy()
{
	if(globals.viewer_p2d_globals) {
		p2d_destroy_globals((p2d_globals_struct*)globals.viewer_p2d_globals);
		globals.viewer_p2d_globals=NULL;
	}
}

// the constructor creates the window
Viewer::Viewer(HWND parent)
{
	DWORD styles;

	m_hwndViewer=NULL;
	m_dib = NULL;
	m_bits = NULL;
	m_errorflag=0;

	styles = WS_OVERLAPPEDWINDOW;
	styles &= ~WS_MINIMIZEBOX;
	m_hwndViewer=CreateWindow(
		_T("TPNGVIEWER"),_T(""),styles,
		CW_USEDEFAULT,  // horizontal position
		CW_USEDEFAULT,	// vertical position
		CW_USEDEFAULT,	// width
		CW_USEDEFAULT,  // height
		parent,NULL,globals.hInst,NULL);
	if(!m_hwndViewer) return;

	twpng_SetWindowPos(m_hwndViewer,&globals.window_prefs.viewer);
	SetViewerWindowTitle();
	ShowWindow(m_hwndViewer,globals.window_prefs.viewer.max?SW_SHOWMAXIMIZED:SW_SHOWNOACTIVATE);
	GetClientRect(m_hwndViewer,&m_clientrect);

	// Allowing files to be droppped into the viewer window is sort of a hack,
	// but it's convenient. We have to be careful not to react to a dropped
	// file if the main application has a dialog box open that could be in the
	// middle of editing a file.
	DragAcceptFiles(m_hwndViewer,TRUE);

	m_dragging=0;
	m_imgpos_x = globals.viewer_imgpos_x;
	m_imgpos_y = globals.viewer_imgpos_y;
}

Viewer::~Viewer()
{
	FreeImage();
	globals.viewer_imgpos_x = m_imgpos_x;
	globals.viewer_imgpos_y = m_imgpos_y;
}

void Viewer::SetViewerWindowTitle()
{
	if(!m_hwndViewer) return;
	if(globals.use_gamma) {
		::SetWindowText(m_hwndViewer,_T("TweakPNG Image Viewer"));
	}
	else {
		::SetWindowText(m_hwndViewer,_T("TweakPNG Image Viewer [gamma off]"));
	}
}

// sets m_strechedwidth based on m_adjwidth and globals.vsize, etc.
void Viewer::CalcStretchedSize()
{
	if(!m_dib) return;
	switch(globals.vsize) {
	case TWPNG_VS_FIT:
		{
			int xavail, yavail;
			double ratio1,ratio2;
			xavail=m_clientrect.right-globals.vborder*2;
			yavail=m_clientrect.bottom-globals.vborder*2;
			if(xavail<1) xavail=1;
			if(yavail<1) yavail=1;
			ratio1 = (double)yavail/(double)xavail;
			ratio2 = (double)m_adjheight/(double)m_adjwidth;
			if(ratio1>ratio2) {
				m_stretchedwidth = xavail;
				m_stretchedheight = (int)(0.5+((double)m_adjheight)*((double)xavail)/(double)m_adjwidth);
			}
			else {
				m_stretchedwidth = (int)(0.5+((double)m_adjwidth)*((double)yavail)/(double)m_adjheight);
				m_stretchedheight = yavail;
			}
		}
		break;
	case TWPNG_VS_1_8:
		m_stretchedwidth = (int)(0.5+((double)m_adjwidth)/8.0);
		m_stretchedheight = (int)(0.5+((double)m_adjheight)/8.0);
		break;
	case TWPNG_VS_1_4:
		m_stretchedwidth = (int)(0.5+((double)m_adjwidth)/4.0);
		m_stretchedheight = (int)(0.5+((double)m_adjheight)/4.0);
		break;
	case TWPNG_VS_1_2:
		m_stretchedwidth = (1+m_adjwidth)/2;
		m_stretchedheight = (1+m_adjheight)/2;
		break;
	case TWPNG_VS_2:
		m_stretchedwidth = m_adjwidth*2;
		m_stretchedheight = m_adjheight*2;
		break;
	case TWPNG_VS_4:
		m_stretchedwidth = m_adjwidth*4;
		m_stretchedheight = m_adjheight*4;
		break;
	case TWPNG_VS_8:
		m_stretchedwidth = m_adjwidth*8;
		m_stretchedheight = m_adjheight*8;
		break;
	default:
		m_stretchedwidth = m_adjwidth;
		m_stretchedheight = m_adjheight;
	}
	if(m_stretchedwidth<1) m_stretchedwidth=1;
	if(m_stretchedheight<1) m_stretchedheight=1;

}

struct viewer_read_ctx {
	Png *png;
};

static int my_read_fn(void *userdata, void *buf, int len)
{
	DWORD ret;
	struct viewer_read_ctx *read_ctx = (struct viewer_read_ctx *)userdata;
	ret = read_ctx->png->stream_file_read((unsigned char*)buf,len);
	return ret;
}

// process image
void Viewer::Update(Png *png1)
{
	struct p2d_globals_struct *p2dg = NULL;
	P2D *p2d = NULL;
	int rv;
	int dens_x, dens_y, dens_units;
	unsigned char tmpr,tmpg,tmpb;
	HCURSOR hcur=NULL;
	int cursor_flag=0;
	struct viewer_read_ctx read_ctx;

	m_errorflag=0;
	FreeImage();

	if(!png1) goto abort;

	if(png1->m_imgtype==IMG_MNG) {
		m_errorflag=1;
		StringCchCopy(m_errormsg,200,_T("Viewer doesn't support MNG files."));
		goto abort;
	}
	else if(png1->m_imgtype==IMG_JNG) {
		m_errorflag=1;
		StringCchCopy(m_errormsg,200,_T("Viewer doesn't support JNG files."));
		goto abort;
	}

	hcur=SetCursor(LoadCursor(NULL,IDC_WAIT));
	cursor_flag=1;

	if(!globals.viewer_p2d_globals) {
		globals.viewer_p2d_globals = (void*)p2d_create_globals();
	}
	p2d = p2d_init((p2d_globals_struct*)globals.viewer_p2d_globals);

	png1->stream_file_start();
	p2d_set_png_read_fn(p2d,my_read_fn);

	read_ctx.png = png1;
	p2d_set_userdata(p2d,(void*)&read_ctx);

	p2d_enable_color_correction(p2d, globals.use_gamma?1:0);

	p2d_set_use_file_bg(p2d,globals.use_imagebg?1:0);
	if(globals.use_custombg) {
		p2d_set_custom_bg(p2d,GetRValue(globals.custombgcolor),
			GetGValue(globals.custombgcolor),GetBValue(globals.custombgcolor));
	}

	rv=p2d_run(p2d);
	if(rv!=PNGD_E_SUCCESS) {
		lstrcpyn(m_errormsg,p2d_get_error_msg(p2d),200);
		m_errorflag=1;
		goto abort;
	}
	rv=p2d_get_dib(p2d,&m_dib,NULL);
	rv=p2d_get_dibbits(p2d, &m_bits, NULL, NULL);

	m_adjwidth = m_dib->biWidth;
	m_adjheight = m_dib->biHeight;
	rv=p2d_get_density(p2d, &dens_x, &dens_y, &dens_units);
	if(rv) {
		if(dens_x!=dens_y && dens_x>0 && dens_y>0 &&
			10*dens_x>dens_y && 10*dens_y>dens_x)
		{
			if(dens_x>dens_y) {
				m_adjheight = (int) (0.5+ (double)m_adjheight * ((double)dens_x/(double)dens_y) );
			}
			else {
				m_adjwidth = (int) (0.5+ (double)m_adjwidth * ((double)dens_y/(double)dens_x) );
			}
		}
	}

	CalcStretchedSize();

	m_imghasbgcolor=0;
	if(p2d_get_bgcolor(p2d,&tmpr,&tmpg,&tmpb)) {
		m_imghasbgcolor=1;
		m_imgbgcolor = RGB(tmpr,tmpg,tmpb);
	}

abort:
	if(p2d) p2d_done(p2d);

	if(m_hwndViewer) {
		InvalidateRect(m_hwndViewer,NULL,TRUE);
	}

	if(cursor_flag) SetCursor(hcur);
}

void Viewer::FreeImage()
{
	if(m_dib) {
		p2d_free_dib(NULL,m_dib);
		m_dib=NULL;
		m_bits=NULL;
		m_errorflag=0;
	}
}

void Viewer::Close()
{
	if(m_hwndViewer) {
		DestroyWindow(m_hwndViewer);
	}
}

// make sure scroll position is within acceptable limits
void Viewer::GoodScrollPos()
{
	int b;
	b=globals.vborder;

	if(m_stretchedwidth+2*b <= m_clientrect.right) {
		//imgpos must be between 0 and (windowwidth-imgwidth)
		if(m_imgpos_x<b) m_imgpos_x=b;
		if(m_imgpos_x > (m_clientrect.right-m_stretchedwidth-b))
			m_imgpos_x = m_clientrect.right-m_stretchedwidth-b;
	}
	else {
		// must be between -(imgwidth-windowwidth) and 0
		if(m_imgpos_x < (m_clientrect.right-m_stretchedwidth-b))
			m_imgpos_x = m_clientrect.right-m_stretchedwidth-b;
		if(m_imgpos_x>b) m_imgpos_x=b;
	}

	if(m_stretchedheight+2*b <= m_clientrect.bottom) {
		//imgpos must be between 0 and (windowwidth-imgwidth)
		if(m_imgpos_y<b) m_imgpos_y=b;
		if(m_imgpos_y > (m_clientrect.bottom-m_stretchedheight-b))
			m_imgpos_y = m_clientrect.bottom-m_stretchedheight-b;
	}
	else {
		// must be between -(imgwidth-windowwidth) and 0
		if(m_imgpos_y < (m_clientrect.bottom-m_stretchedheight-b))
			m_imgpos_y = m_clientrect.bottom-m_stretchedheight-b;
		if(m_imgpos_y>0) m_imgpos_y=b;
	}
}


LRESULT CALLBACK Viewer::WndProcViewer(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WORD id;
	POINTS pts;
	Viewer *v;

	if(!g_viewer) goto exit1;
	v=g_viewer;

	switch(msg) {
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
		{
			PAINTSTRUCT paintStruct;
			HDC hdc;
			HBRUSH hbr1;
			int rv;

			hdc = BeginPaint(hwnd, &paintStruct);

			if(v->m_dib) {

				v->GoodScrollPos();

				// paint the image
				SetStretchBltMode(hdc,COLORONCOLOR);
				rv=StretchDIBits(hdc,
					v->m_imgpos_x,v->m_imgpos_y,v->m_stretchedwidth,v->m_stretchedheight,
					0,0,v->m_dib->biWidth,v->m_dib->biHeight,
					v->m_bits,(LPBITMAPINFO)v->m_dib,
					DIB_RGB_COLORS,SRCCOPY);
				if(rv==GDI_ERROR || rv<1) {
					RECT r1;
					// Failure probably means we've exceeded the video system's
					// stretching limits. Draw a black rectangle instead.
					r1.left=v->m_imgpos_x; r1.top=v->m_imgpos_y;
					r1.right=v->m_imgpos_x+v->m_stretchedwidth;
					r1.bottom=v->m_imgpos_y+v->m_stretchedheight;
					FillRect(hdc,&r1,(HBRUSH)GetStockObject(BLACK_BRUSH));
				}

				// paint the background
				ExcludeClipRect(hdc,
					v->m_imgpos_x, v->m_imgpos_y,
					v->m_imgpos_x+v->m_stretchedwidth, v->m_imgpos_y+v->m_stretchedheight);

				if(globals.window_bgcolor==TWPNG_WBG_SYSDEFAULT) {
					FillRect(hdc,&v->m_clientrect,GetSysColorBrush(COLOR_WINDOW));
				}
				else {
					
					if(globals.window_bgcolor==TWPNG_WBG_SAMEASIMAGE && v->m_imghasbgcolor)
						hbr1 = CreateSolidBrush(v->m_imgbgcolor);
					else
						hbr1 = CreateSolidBrush(globals.custombgcolor);
					FillRect(hdc,&v->m_clientrect,hbr1);
					DeleteObject(hbr1);
				}
			}
			else {
				FillRect(hdc,&v->m_clientrect,GetSysColorBrush(COLOR_WINDOW));

				if(v->m_errorflag) {
					SetTextColor(hdc,RGB(128,0,0));
					SetBkColor(hdc,RGB(255,255,255));
					SetBkMode(hdc,OPAQUE);
					SelectObject(hdc,GetStockObject(ANSI_VAR_FONT));
					DrawText(hdc,v->m_errormsg,-1,&v->m_clientrect,
						DT_NOPREFIX|DT_WORDBREAK);
				}
			}

			EndPaint(hwnd, &paintStruct);
			return 0;
		}

	case WM_INITMENU:
		{
			HMENU m;
			m=(HMENU)wParam;
			CheckMenuItem(m,ID_GAMMACORRECT,MF_BYCOMMAND|
				(globals.use_gamma?MF_CHECKED:MF_UNCHECKED));
			CheckMenuItem(m,ID_BG_BKGD,MF_BYCOMMAND|
				(globals.use_imagebg?MF_CHECKED:MF_UNCHECKED));
			CheckMenuItem(m,ID_BG_CUSTOM,MF_BYCOMMAND|
				(globals.use_custombg?MF_CHECKED:MF_UNCHECKED));

			CheckMenuItem(m,ID_WBG_SAMEASIMAGE,MF_BYCOMMAND|
				(globals.window_bgcolor==TWPNG_WBG_SAMEASIMAGE?MF_CHECKED:MF_UNCHECKED));
			CheckMenuItem(m,ID_WBG_SYSDEFAULT,MF_BYCOMMAND|
				(globals.window_bgcolor==TWPNG_WBG_SYSDEFAULT?MF_CHECKED:MF_UNCHECKED));
			CheckMenuItem(m,ID_WBG_CUSTOM,MF_BYCOMMAND|
				(globals.window_bgcolor==TWPNG_WBG_CUSTOM?MF_CHECKED:MF_UNCHECKED));
			CheckMenuItem(m,ID_ZOOM_FIT,MF_BYCOMMAND|(globals.vsize==TWPNG_VS_FIT?MF_CHECKED:MF_UNCHECKED));
			CheckMenuItem(m,ID_ZOOM_1_8,MF_BYCOMMAND|(globals.vsize==TWPNG_VS_1_8?MF_CHECKED:MF_UNCHECKED));
			CheckMenuItem(m,ID_ZOOM_1_4,MF_BYCOMMAND|(globals.vsize==TWPNG_VS_1_4?MF_CHECKED:MF_UNCHECKED));
			CheckMenuItem(m,ID_ZOOM_1_2,MF_BYCOMMAND|(globals.vsize==TWPNG_VS_1_2?MF_CHECKED:MF_UNCHECKED));
			CheckMenuItem(m,ID_ZOOM_1,MF_BYCOMMAND|(globals.vsize==TWPNG_VS_1?MF_CHECKED:MF_UNCHECKED));
			CheckMenuItem(m,ID_ZOOM_2,MF_BYCOMMAND|(globals.vsize==TWPNG_VS_2?MF_CHECKED:MF_UNCHECKED));
			CheckMenuItem(m,ID_ZOOM_4,MF_BYCOMMAND|(globals.vsize==TWPNG_VS_4?MF_CHECKED:MF_UNCHECKED));
			//CheckMenuItem(m,ID_ZOOM_8,MF_BYCOMMAND|(globals.vsize==TWPNG_VS_8?MF_CHECKED:MF_UNCHECKED));
			return 0;
		}

	case WM_DESTROY:
		twpng_StoreWindowPos(hwnd,&globals.window_prefs.viewer);
		delete g_viewer;
		g_viewer=NULL;
		SetFocus(globals.hwndMain);
		return 0;
		
	case WM_SIZE:
		GetClientRect(hwnd,&v->m_clientrect);
		if(globals.vsize==TWPNG_VS_FIT) v->CalcStretchedSize();
		return 0;

	case WM_DROPFILES:
		if(globals.dlgs_open<1)
			DroppedFiles((HDROP)wParam);
		return 0;

	case WM_LBUTTONDBLCLK:
		if(globals.vsize==TWPNG_VS_1 || globals.vsize==TWPNG_VS_FIT) {
			if(globals.vsize==TWPNG_VS_1)
				globals.vsize=TWPNG_VS_FIT;
			else
				globals.vsize=TWPNG_VS_1;
			v->CalcStretchedSize();
			InvalidateRect(hwnd,NULL,0);
		}
		return 0;

	case WM_LBUTTONDOWN:
		if(v->m_dragging) return 0;
		if(!v->m_dib) return 0;
		SetCapture(hwnd);
		SetCursor(globals.hcurDrag2);
		pts = MAKEPOINTS(lParam);
		v->m_dragstart_x = v->m_imgpos_x - pts.x;
		v->m_dragstart_y = v->m_imgpos_y - pts.y;
		v->m_dragging = 1;
		return 0;

	case WM_LBUTTONUP:
		SetCapture(NULL);
		v->m_dragging=0;
		return 0;


	case WM_MOUSEMOVE:
		if(v->m_dragging) {
			pts = MAKEPOINTS(lParam);
			v->m_imgpos_x = v->m_dragstart_x + pts.x;
			v->m_imgpos_y = v->m_dragstart_y + pts.y;
			InvalidateRect(hwnd,NULL,TRUE);
		}
		return 0;

	case WM_CLOSE: // The user clicked the [x] for the window.
		globals.autoopen_viewer=0;
		break;  // DefWindowProc will send a WM_DESTROY message

	case WM_COMMAND:
		id=LOWORD(wParam);
		switch(id) {
		case ID_CLOSE:
			globals.autoopen_viewer=0;
			DestroyWindow(hwnd);
			return 0;
		case ID_GAMMACORRECT:
			globals.use_gamma = !globals.use_gamma;
			v->SetViewerWindowTitle();
			update_viewer();
			return 0;
		case ID_BG_CUSTOM:
			globals.use_custombg = !globals.use_custombg;
			update_viewer();
			return 0;
		case ID_BG_BKGD:
			globals.use_imagebg = !globals.use_imagebg;
			update_viewer();
			return 0;
		case ID_WBG_SAMEASIMAGE:
			globals.window_bgcolor = TWPNG_WBG_SAMEASIMAGE;
			InvalidateRect(hwnd,NULL,0);
			break;
		case ID_WBG_SYSDEFAULT:
			globals.window_bgcolor = TWPNG_WBG_SYSDEFAULT;
			InvalidateRect(hwnd,NULL,0);
			break;
		case ID_WBG_CUSTOM:
			globals.window_bgcolor = TWPNG_WBG_CUSTOM;
			InvalidateRect(hwnd,NULL,0);
			break;

		case ID_SETBG:
			{
				unsigned char tmpr,tmpg,tmpb;
				tmpr=GetRValue(globals.custombgcolor);
				tmpg=GetGValue(globals.custombgcolor);
				tmpb=GetBValue(globals.custombgcolor);
				if(choose_color_dialog(hwnd,&tmpr,&tmpg,&tmpb)) {
					globals.custombgcolor = RGB(tmpr,tmpg,tmpb);
					update_viewer();
				}
				return 0;
			}

		case ID_ZOOM_FIT: globals.vsize=TWPNG_VS_FIT; goto rezoom;
		case ID_ZOOM_1_8: globals.vsize=TWPNG_VS_1_8; goto rezoom;
		case ID_ZOOM_1_4: globals.vsize=TWPNG_VS_1_4; goto rezoom;
		case ID_ZOOM_1_2: globals.vsize=TWPNG_VS_1_2; goto rezoom;
		case ID_ZOOM_2: globals.vsize=TWPNG_VS_2; goto rezoom;
		case ID_ZOOM_4: globals.vsize=TWPNG_VS_4; goto rezoom;
		//case ID_ZOOM_8: globals.vsize=TWPNG_VS_8; goto rezoom;
		case ID_ZOOM_1: globals.vsize=TWPNG_VS_1;
rezoom:
			v->CalcStretchedSize();
			InvalidateRect(hwnd,NULL,0);
			break;

		}
	}

exit1:
	return DefWindowProc(hwnd, msg, wParam, lParam);

}

#endif TWPNG_SUPPORT_VIEWER
