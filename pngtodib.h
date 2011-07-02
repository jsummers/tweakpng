// pngtodib.h


// error codes returned by p2d_run()

#define PNGD_E_SUCCESS   0
#define PNGD_E_ERROR     1   // unspecified error 
#define PNGD_E_NOMEM     3   // could not alloc memory
#define PNGD_E_LIBPNG    5   // libpng error (corrupt PNG?)
#define PNGD_E_BADPNG    7   // corrupt or unsupported PNG
#define PNGD_E_READ      8   // couldn't read PNG file

#define p2d_byte unsigned char

struct p2d_globals_struct;

struct p2d_struct;
#define P2D struct p2d_struct

// Return 'nbytes' on success, < nbytes on failure.
typedef	int   (*p2d_read_cb_type)(void *userdata, void *buf, int nbytes);

void p2d_set_png_read_fn(P2D *p2d, p2d_read_cb_type readfunc);
void p2d_set_use_file_bg(P2D *p2d, int flag);
void p2d_set_custom_bg(P2D *p2d, p2d_byte r, p2d_byte g, p2d_byte b);
void p2d_enable_color_correction(P2D *p2d, int flag);
int  p2d_run(P2D *p2d);
int  p2d_get_dib(P2D *p2d, BITMAPINFOHEADER **ppdib);
int  p2d_get_dibbits(P2D *p2d, void **ppbits);
int  p2d_get_density(P2D *p2d, int *pres_x, int *pres_y, int *pres_units);
int  p2d_get_bgcolor(P2D *p2d, p2d_byte *pr, p2d_byte *pg, p2d_byte *pb);
void p2d_free_dib(BITMAPINFOHEADER *pdib);

P2D* p2d_init(struct p2d_globals_struct *g);

int   p2d_done(P2D *p2d);

struct p2d_globals_struct *p2d_create_globals(void);
void p2d_destroy_globals(struct p2d_globals_struct *g);

void   p2d_set_userdata(P2D *p2d, void *userdata);
void*  p2d_get_userdata(P2D *p2d);
TCHAR* p2d_get_error_msg(P2D *p2d);

void p2d_get_libpng_version(TCHAR *buf, int buflen);
