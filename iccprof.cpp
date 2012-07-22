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

void Chunk::init_iCCP_dlg(HWND hwnd)
{
	struct keyword_info_struct kw;

	get_keyword_info(&kw);

	SendDlgItemMessage(hwnd,IDC_EDIT1,EM_LIMITTEXT,79,0);
	SetDlgItemText(hwnd,IDC_EDIT1,kw.keyword);

	SendDlgItemMessage(hwnd,IDC_EDIT2,EM_REPLACESEL,(WPARAM)FALSE,
		(LPARAM)_T("Not implemented\r\n"));
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
