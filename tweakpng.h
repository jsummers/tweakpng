// tweakpng.h

#ifndef TWEAKPNG_H
#define TWEAKPNG_H

// Symbols, characters, etc., that are different when Unicode is disabled.
#ifdef UNICODE
#define SYM_COPYRIGHT   _T("\xa9")
#define SYM_TIMES       _T("\xd7")
#define SYM_MICROMETERS _T("\x3bcm")
#define SYM_MIDDOT      _T("\xb7")
#define SYM_ENDASH      _T("\x2013")
#define SYM_HORZBAR     _T("\x2015")
#define SYM_LSQUO       _T("\x2018")
#define SYM_RSQUO       _T("\x2019")
#define SYM_LDQUO       _T("\x201c")
#define SYM_RDQUO       _T("\x201d")
#else
#define SYM_COPYRIGHT   "(c)"
#define SYM_TIMES       "x"
#define SYM_MICROMETERS "micrometers"
#define SYM_MIDDOT      "-"
#define SYM_ENDASH      "-"
#define SYM_HORZBAR     "---"
#define SYM_LSQUO       "'"
#define SYM_RSQUO       "'"
#define SYM_LDQUO       "\""
#define SYM_RDQUO       "\""
#endif

#define TWEAKPNG_VER_STRING      _T("1.4.5")
#define TWEAKPNG_COPYRIGHT_DATE  _T("1999") SYM_ENDASH _T("2012")
#define TWEAKPNG_HOMEPAGE        _T("http://entropymine.com/jason/tweakpng/")

#define ID_STBAR      19000
//#define ID_IMGVIEWER     19100
#define ID_UPDATEVIEWER  19101

#define IMG_UNKNOWN 0
#define IMG_PNG 1
#define IMG_MNG 2
#define IMG_JNG 3


#define CHUNK_UNKNOWN    0
#define CHUNK_IHDR  1
#define CHUNK_IEND  2
#define CHUNK_IDAT  3
#define CHUNK_PLTE  4
#define CHUNK_bKGD  5
#define CHUNK_cHRM  6
#define CHUNK_gAMA  7
#define CHUNK_hIST  8
#define CHUNK_pHYs  9
#define CHUNK_sBIT  10
#define CHUNK_tEXt  11
#define CHUNK_tIME  12
#define CHUNK_tRNS  13
#define CHUNK_zTXt  14

// v1.2 chunks:
#define CHUNK_sRGB  15
#define CHUNK_iCCP  16
#define CHUNK_iTXt  17
#define CHUNK_sPLT  18

// v1.3
#define CHUNK_sTER  19

// v1.4
#define CHUNK_dSIG  20

// MNG-specific chunks
#define CHUNK_MHDR 200
#define CHUNK_LOOP 201
#define CHUNK_ENDL 202
#define CHUNK_DEFI 203
#define CHUNK_JHDR 204
#define CHUNK_BASI 205
#define CHUNK_CLON 206
#define CHUNK_DHDR 207
#define CHUNK_PAST 208
#define CHUNK_DISC 209
#define CHUNK_BACK 210
#define CHUNK_FRAM 211
#define CHUNK_MOVE 212
#define CHUNK_CLIP 213
#define CHUNK_SHOW 214
#define CHUNK_TERM 215
#define CHUNK_SAVE 216
#define CHUNK_SEEK 217
#define CHUNK_MEND 218

#define CHUNK_eXPI 230
#define CHUNK_fPRI 231
#define CHUNK_nEED 232
#define CHUNK_pHYg 233
#define CHUNK_JDAT 234
#define CHUNK_JSEP 235
#define CHUNK_PROM 236
#define CHUNK_IPNG 237
#define CHUNK_PPLT 238
#define CHUNK_IJNG 239
#define CHUNK_DROP 240
#define CHUNK_DBYK 241
#define CHUNK_ORDR 242

// APNG-specific chunks
#define CHUNK_acTL 300
#define CHUNK_fcTL 301
#define CHUNK_fdAT 302

// Other known private chunks
#define CHUNK_CgBI 310

// registered PNG extensions:
#define CHUNK_oFFs  501
#define CHUNK_pCAL  502
#define CHUNK_sCAL  503
#define CHUNK_gIFg  504
#define CHUNK_gIFx  505
#define CHUNK_gIFt  506   /* deprecated */
#define CHUNK_fRAc  507


#define MSG_S 1 // severe
#define MSG_E 0 // error
#define MSG_W 2 // warning
#define MSG_I 3 // information


#define CRCCOMPL(c) ((c)^0xffffffff)
#define CRCINIT (CRCCOMPL(0))


struct windowpos_struct {
	int x;
	int y;
	int w;
	int h;
	int max;
};


struct windowprefs_struct {
	// don't change or rearrange the existing members of this
	// structure, or saved settings will be screwed up
	int version;
	DWORD column_width[5];
	struct windowpos_struct main;
	struct windowpos_struct text;
	struct windowpos_struct plte;
	struct windowpos_struct viewer;
};

#define MAX_TOOL_NAME   80
#define MAX_TOOL_CMD    128
#define MAX_TOOL_PARAMS 80
#define TWPNG_NUMTOOLS 6
typedef struct tools_t_struct {
	TCHAR name[MAX_TOOL_NAME];
	TCHAR cmd[MAX_TOOL_CMD];
	TCHAR params[MAX_TOOL_PARAMS];
} tools_t;

struct globals_struct {
	int zlib_available;
	int unicode_supported;
	HINSTANCE hInst;
	HWND hwndMain;
	int dlgs_open; // Tracks whether a modal dialog is open.
	int compression_level;
	int use_gamma;
	COLORREF custombgcolor;
	int use_custombg;
	int use_imagebg;
	int vborder; // viewer border width

#define TWPNG_VS_FIT 1
#define TWPNG_VS_1_8 2
#define TWPNG_VS_1_4 3
#define TWPNG_VS_1_2 4
#define TWPNG_VS_1   5
#define TWPNG_VS_2   6
#define TWPNG_VS_4   7
#define TWPNG_VS_8   8
	int vsize;

#define TWPNG_WBG_SAMEASIMAGE 0
#define TWPNG_WBG_SYSDEFAULT  1
#define TWPNG_WBG_CUSTOM      2
	int window_bgcolor;

	COLORREF custcolors[16];
	int autoopen_viewer;
	HCURSOR hcurDrag2;
	int viewer_imgpos_x, viewer_imgpos_y;
	struct windowprefs_struct window_prefs;
	HWND hwndMainList;
	HWND hwndStBar;
	int stbar_height;
	int timer_set;
	UINT pngchunk_cf;    // registered clipboard format

	const TCHAR *twpng_homepage;
	const TCHAR *twpng_reg_key;

	TCHAR file_from_cmdline[MAX_PATH];
	TCHAR last_open_dir[MAX_PATH];
	TCHAR home_dir[MAX_PATH];   // dir that tweakpng.exe is in
	TCHAR orig_dir[MAX_PATH];   // current dir when program started
	DWORD crc_table[256];       // table of CRCs of all 8-bit messages

	tools_t tools[TWPNG_NUMTOOLS];

	void *viewer_p2d_globals; // Used by the viewer
};


DWORD update_crc(DWORD crc, unsigned char *buf, int len);
void write_int32(unsigned char *buf, DWORD x);
DWORD read_int32(unsigned char *x);
int read_int16(unsigned char *x);
void write_int16(unsigned char *buf, int x);
int GetLVSelection(HWND hwnd);
void SetLVSelection(HWND hwnd, int pos, int num);
int get_name_from_id(char *name, int x);
void mesg(int severity, const TCHAR *fmt, ...);
int choose_color_dialog(HWND hwnd, unsigned char *redp,
						unsigned char *greenp, unsigned char *bluep);
void DroppedFiles(HDROP hDrop);
void GetPosInParent(HWND hwnd,RECT *rc);
void update_status_bar_and_viewer();
int can_edit_chunk_type(int ct);
void twpng_SetLVSelection(HWND hwnd, int pos1, int num);

LRESULT CALLBACK WndProcEditPal(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

int twpng_StoreWindowPos(HWND hwnd, struct windowpos_struct *q);
void twpng_SetWindowPos(HWND hwnd, const struct windowpos_struct *q);

void update_viewer();

int convert_tchar_to_latin1(const TCHAR *src, int srclen,
								   char **pdst, int *pdstlen);
int convert_latin1_to_tchar(const char *src, int srclen,
								   TCHAR **pdst, int *pdstlen);
#ifdef UNICODE
int convert_utf8_to_utf16(const void *src, int srclen,
								 WCHAR **pdst, int *pdstlen);
int convert_utf16_to_utf8(const WCHAR *src, int srclen,
								 char **pdst, int *pdstlen);
#endif

#ifdef TWPNG_HAVE_ZLIB
int twpng_uncompress_data(unsigned char **dataoutp, unsigned char *datain, int inlen);
int twpng_compress_data(unsigned char **dataoutp, unsigned char*datain, int inlen);
#endif

#ifdef TWPNG_SUPPORT_VIEWER
void twpng_get_libpng_version(TCHAR *buf, int buflen);
#endif

class Png;
class Chunk;

struct text_info_struct {
	int processed;
	int is_compressed;
	TCHAR *text; // not necessarily NUL terminated?
	int text_size_in_tchars; // not including trailing NUL (is there a trailing NUL?)
	TCHAR *keyword;
	TCHAR *language;
	TCHAR *translated_keyword;
};

struct keyword_info_struct {
	int keyword_len;
	TCHAR keyword[80];
};

struct sCAL_data;

struct textdlgmetrics {
	int border_buttonoffset;
	int border_editx;
	int border_edity;
	int border_btn1y;
	int border_btn2y;
};

struct edit_chunk_ctx {
	Chunk *ch;
	struct textdlgmetrics tdm;
};

int ImportICCProfileByFilename(Png *png, const TCHAR *fn);
int ImportICCProfile(Png *png);

class Chunk {
public:
	Chunk();
	~Chunk();

	static INT_PTR CALLBACK DlgProcEditChunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	void after_init();

	int write_to_file(HANDLE fh, int exp);
	void get_text_descr(TCHAR *buf, int buflen);    // buf must be 1000 chars or more
	void get_text_descr_generic(TCHAR *buf, int buflen);    // buf must be 1000 chars or more
	int get_chunk_type_id(); // convert type into an integer
	int copy_to_memory(unsigned char *m);
	DWORD copy_segment_to_memory(unsigned char *buf, DWORD offset, DWORD len);
	int init_from_memory(unsigned char *buf, int);
	void set_chunktype_tchar_from_ascii();
	void free_text_info();
	int get_keyword_info(struct keyword_info_struct *kw);


	DWORD calc_crc();  // calculates CRC, does not modify it

	int edit();  // generic edit; calls the right edit_*() function
	int can_edit();  // Can this chunk normally be edited?

	void chunkmodified();

	int is_critical();
	int is_public();
	int is_safe_to_copy();

	unsigned char *data;
	DWORD length;     /* length of the DATA field */
	DWORD m_crc;
	char m_chunktype_ascii[5];
	TCHAR m_chunktype_tchar[5];
	int m_chunktype_id;
	Png *m_parentpng;
	int get_text_info();
	int set_text_info(const TCHAR *keyword,
		const TCHAR *language, const TCHAR *translated_keyword,
		const TCHAR *indata, int is_compressed, int is_international);

	int m_index; // sometimes used to hold the position in the parent chunk array
	int m_flag;  // sometimes used to remember if a chunk was selected

	// used in handling text chunks
	struct text_info_struct m_text_info;

private:

	void describe_IHDR(TCHAR *buf, int buflen);
	void describe_JHDR(TCHAR *buf, int buflen);
	void describe_MHDR(TCHAR *buf, int buflen);
	void describe_IEND(TCHAR *buf, int buflen);
	void describe_bKGD(TCHAR *buf, int buflen);
	void describe_PLTE(TCHAR *buf, int buflen);
	void describe_tRNS(TCHAR *buf, int buflen);
	void describe_gAMA(TCHAR *buf, int buflen);
	void describe_sBIT(TCHAR *buf, int buflen);
	void describe_cHRM(TCHAR *buf, int buflen);
	void describe_pHYs(TCHAR *buf, int buflen);
	void describe_tIME(TCHAR *buf, int buflen);
	void describe_hIST(TCHAR *buf, int buflen);
	void describe_text(TCHAR *buf, int buflen, int id);
	void describe_sRGB(TCHAR *buf, int buflen);
	void describe_sTER(TCHAR *buf, int buflen);
	void describe_acTL(TCHAR *buf, int buflen);
	void describe_fcTL(TCHAR *buf, int buflen);
	void describe_fdAT(TCHAR *buf, int buflen);
	void describe_oFFs(TCHAR *buf, int buflen);
	void describe_sCAL(TCHAR *buf, int buflen);
	void describe_keyword_chunk(TCHAR *buf, int buflen, const TCHAR *prefix);

	int edit_plte_info();
#define TWPNG_FLAG_ASCIIFLOATINGPOINT 0x1
	int read_text_field(int offset, TCHAR *buf, int buflen, unsigned int flags);
	void get_sCAL_data(struct sCAL_data *d);
	void set_sCAL_data(const struct sCAL_data *d);
	void init_sCAL_dlg(HWND hwnd);
	void process_sCAL_dlg(HWND hwnd);
	void init_iCCP_dlg(struct edit_chunk_ctx *ecctx, HWND hwnd);
	void size_iCCP_dlg(struct edit_chunk_ctx *ecctx, HWND hwnd);
	void process_iCCP_dlg(struct edit_chunk_ctx *ecctx, HWND hwnd);

	int has_valid_length();
	void msg_invalid_length(TCHAR *buf, int buflen, const TCHAR *name);
	int msg_if_invalid_length(TCHAR *buf, int buflen, const TCHAR *name);
};


class Png {

public:
	Png(const TCHAR *load_fn, const TCHAR *save_fn);
	Png();

	~Png();

	int m_valid;

	int write_file(const TCHAR *fn);
	int write_to_mem(unsigned char **pmem, int *plen);
	void stream_file_start();
	DWORD stream_file_read(unsigned char *buf, DWORD bytes);
	void fill_listbox(HWND hwnd);
	void delete_chunk(int);
	void move_chunk(int,int);

	void modified(); // only call if something really changed. also calls updatestbar
	
	void set_signature();
	int check_validity(int msgmode);
	void edit_chunk(int);
	int split_idat(int cn, int size, int repeat);
	void insert_chunks(int pos, int num, int init);
	void new_chunk(int chunktype_id);
	Chunk *find_first_chunk(int chunktype_id, int *index);
	DWORD get_file_size();
	

	int m_imgtype;
	int m_num_chunks;
	DWORD m_pngfilesize;   // used when loading from disk

	//info from the header
	DWORD m_width;
	DWORD m_height;
	unsigned char m_bitdepth;
	unsigned char m_colortype;
	//unsigned char m_compression;
	//unsigned char m_compressionfilter;
	//unsigned char m_interlace;

	int create_display_window();

	Chunk **chunk;

	TCHAR m_filename[MAX_PATH];
	int m_named;
	int m_dirty; // has file been modified?

private:
	int m_chunks_alloc;    /* alloc'd length of the chunk array */

	int m_stream_phase; // 0=reading file signature, 1=reading chunks
	int m_stream_curchunk;  // used by stream_file_read
	DWORD m_stream_curpos_in_curchunk; // position in the current chunk, or in the file signature


	void update_row(HWND hwnd,int n);

	unsigned char signature[8];

	void init_new_chunk(int);

	int read_signature(HANDLE fh);
	int read_next_chunk(HANDLE fh);

};


class Viewer {
public:
	Viewer(HWND parent, const TCHAR *current_filename);
	~Viewer();

	static void GlobalInit();
	static void GlobalDestroy();
	void Close();
	void Update(Png *png); // Set png==NULL to clear viewer.
	void SetCurrentFileName(const TCHAR *fn);
	void UpdateViewerWindowTitle();
	HWND m_hwndViewer;

private:
	LPBITMAPINFOHEADER m_dib;
	void *m_bits;

	int m_imghasbgcolor;
	COLORREF m_imgbgcolor;

	int m_errorflag; // 0 = no error
	TCHAR m_errormsg[200];
	TCHAR m_filename_base[200];

	static LRESULT CALLBACK WndProcViewer(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void FreeImage();
	int m_adjwidth, m_adjheight; // after adjusting for non-square pixels
	int m_stretchedwidth, m_stretchedheight;
	int m_imgpos_x, m_imgpos_y;
	int m_dragging;
	int m_dragstart_x, m_dragstart_y;
	void GoodScrollPos();
	void CalcStretchedSize();
	BOOL HandleKeyDown(HWND hwnd, WPARAM wParam, LPARAM lParam);
	RECT m_clientrect;
};

#endif
