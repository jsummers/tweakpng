// pngtodib.h

#define PNGDIB_DEFAULT_SCREEN_GAMMA   2.20000
#define PNGDIB_DEFAULT_FILE_GAMMA     0.45455


// error codes returned by pngdib_*_run()

#define PNGD_E_SUCCESS   0
#define PNGD_E_ERROR     1   // unspecified error 
#define PNGD_E_NOMEM     3   // could not alloc memory
#define PNGD_E_LIBPNG    5   // libpng error (corrupt PNG?)
#define PNGD_E_BADPNG    7   // corrupt or unsupported PNG
#define PNGD_E_READ      8   // couldn't read PNG file

// Return 'nbytes' on success, < nbytes on failure.
typedef	int   (*pngdib_read_cb_type)(void *userdata, void *buf, int nbytes);

struct p2d_struct;
#define PNGDIB struct p2d_struct

void pngdib_p2d_set_png_read_fn(PNGDIB *p2d, pngdib_read_cb_type readfunc);
void pngdib_p2d_set_use_file_bg(PNGDIB *p2d, int flag);
void pngdib_p2d_set_custom_bg(PNGDIB *p2d, unsigned char r,
								  unsigned char g, unsigned char b);
void pngdib_p2d_set_gamma_correction(PNGDIB *p2d, int flag, double screen_gamma);
int  pngdib_p2d_run(PNGDIB *p2d);
int  pngdib_p2d_get_dib(PNGDIB *p2d, BITMAPINFOHEADER **ppdib, int *pdibsize);
int  pngdib_p2d_get_dibbits(PNGDIB *p2d, void **ppbits, int *pbitsoffset, int *pbitssize);
int  pngdib_p2d_get_density(PNGDIB *p2d, int *pres_x, int *pres_y, int *pres_units);
int  pngdib_p2d_get_bgcolor(PNGDIB *p2d, unsigned char *pr, unsigned char *pg, unsigned char *pb);
void pngdib_p2d_free_dib(PNGDIB *p2d, BITMAPINFOHEADER *pdib);

PNGDIB* pngdib_init(void);

int   pngdib_done(PNGDIB *p2d);

void   pngdib_set_userdata(PNGDIB *p2d, void *userdata);
void*  pngdib_get_userdata(PNGDIB *p2d);
TCHAR* pngdib_get_error_msg(PNGDIB *p2d);

void pngdib_get_libpng_version(TCHAR *buf, int buflen);
