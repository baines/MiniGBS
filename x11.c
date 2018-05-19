#include "minigbs.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xlibint.h>
#include <dlfcn.h>

#define X11_WIN_W 854
#define X11_WIN_H 480

unsigned long typedef ulong;
unsigned int  typedef uint;

static struct x11_funcs {
	Display*   (*open_dpy)    (const char*);
	Window     (*simple_win)  (Display*, Window, int, int, uint, uint, uint, ulong, ulong);
	int        (*map_window)  (Display*, Window);
	int        (*unmap_window)(Display*, Window);
	int        (*store_name)  (Display*, Window, const char*);
	Bool       (*query_ext)   (Display*, const char*, int*, int*, int*);
	XExtCodes* (*init_ext)    (Display*, const char*);
	GC         (*create_gc)   (Display*, Drawable, ulong, XGCValues*);
	Pixmap     (*create_pix)  (Display*, Drawable, uint, uint, uint);
	int        (*fill_rect)   (Display*, Drawable, GC, int, int, uint, uint);
	int        (*draw_lines)  (Display*, Drawable, GC, XPoint*, int, int);
	int        (*flush)       (Display*);
	int        (*sel_input)   (Display*, Window, long);
	int        (*pending)     (Display*);
	int        (*next_event)  (Display*, XEvent*);
	KeySym     (*lookup_key)  (XKeyEvent*, int);
	Atom       (*intern_atom) (Display*, const char*, Bool);
	Status     (*set_protos)  (Display*, Window, Atom*, int);
	void*      (*get_request) (Display*, uint8_t, size_t);
} x11;

static const char* x11_func_names[] = {
	"XOpenDisplay" , "XCreateSimpleWindow", "XMapWindow"    , "XUnmapWindow",
	"XStoreName"   , "XQueryExtension"    , "XInitExtension", "XCreateGC",
	"XCreatePixmap", "XFillRectangle"     , "XDrawLines"    , "XFlush",
	"XSelectInput" , "XPending"           , "XNextEvent"    , "XLookupKeysym",
	"XInternAtom"  , "XSetWMProtocols"    , "_XGetRequest"  ,
};

struct present_pixmap_req {
    uint8_t  type;
    uint8_t  subtype;
    uint16_t length;
    uint32_t window;
    uint32_t pixmap;
	uint32_t crap[15];
};

static void*    xlib;
static Display* x11_dpy;
static Window   x11_win;
static Pixmap   x11_pix;
static GC       x11_gc;
static GC       x11_gc2;
static int      x11_present_op;
static bool     x11_visible;
static Atom     x11_atom_wm_delete;

int x11_init(void){
	xlib = dlopen("libX11.so", RTLD_NOW | RTLD_LOCAL);
	if(!xlib || dlerror()){
		xlib = NULL;
		return -1;
	}

	int i = 0;
	for(void** fn = (void**)&x11; fn != (void**)(&x11+1); ++fn, ++i){
		*fn = dlsym(xlib, x11_func_names[i]);

		const char* msg = dlerror();
		if(!*fn || msg){
			dlclose(xlib);
			xlib = NULL;
			return -1;
		}
	}

	x11_dpy = x11.open_dpy(NULL);
	x11_win = x11.simple_win(x11_dpy, RootWindow(x11_dpy, 0), 0, 0, X11_WIN_W, X11_WIN_H, 0, 0, BlackPixel(x11_dpy, 0));

	int op, ev, err;
	x11.query_ext(x11_dpy, "Present", &op, &ev, &err);
	x11_present_op = x11.init_ext(x11_dpy, "Present")->major_opcode;
	x11.flush(x11_dpy);

	x11_pix = x11.create_pix(x11_dpy, x11_win, X11_WIN_W, X11_WIN_H, 24);
	x11.flush(x11_dpy);

	XGCValues gcv = {
		.foreground = WhitePixel(x11_dpy, 0),
		.background = BlackPixel(x11_dpy, 0),
		.line_width = 2,
	};

	x11_gc = x11.create_gc(x11_dpy, x11_pix, GCForeground | GCBackground | GCLineWidth, &gcv);
	gcv.foreground = BlackPixel(x11_dpy, 0);
	gcv.background = WhitePixel(x11_dpy, 0);
	x11_gc2 = x11.create_gc(x11_dpy, x11_pix, GCForeground | GCBackground | GCLineWidth, &gcv);

	x11.store_name(x11_dpy, x11_win, "MiniGBS Oscilloscope");
	x11.sel_input(x11_dpy, x11_win, KeyPressMask);

    x11_atom_wm_delete = x11.intern_atom(x11_dpy, "WM_DELETE_WINDOW", 0);
    x11.set_protos(x11_dpy, x11_win, &x11_atom_wm_delete, 1);

	return ConnectionNumber(x11_dpy);
}

int x11_action(bool* have_more_events){
	if(!x11_win || !x11.pending(x11_dpy)){
		*have_more_events = false;
		return -1;
	}

	XEvent ev;
	x11.next_event(x11_dpy, &ev);

	if(ev.type == KeyPress){
		KeySym sym = x11.lookup_key(&ev.xkey, 0);

		switch(sym){
			case XK_Escape:
			case XK_q:
				return 'o';
			case XK_BackSpace:
				return KEY_BACKSPACE;
			case XK_Up:
				return KEY_UP;
			case XK_Down:
				return KEY_DOWN;
			case XK_Left:
				return KEY_LEFT;
			case XK_Right:
				return KEY_RIGHT;
			case XK_Return:
				return '\n';
			case XK_c:
				if(ev.xkey.state & ControlMask){
					return 'q';
				}
			default:
				return sym;
		}
	}

	if(ev.type == ClientMessage && ev.xclient.data.l[0] == x11_atom_wm_delete){
		x11.unmap_window(x11_dpy, x11_win);
		x11_visible = false;
	}

	return -1;
}

void x11_draw_begin(void){
	if(!x11_win || !x11_visible) return;
	x11.fill_rect(x11_dpy, x11_pix, x11_gc2, 0, 0, X11_WIN_W, X11_WIN_H);
}

void x11_draw_lines(int16_t* points, size_t n){
	if(!x11_win || !x11_visible) return;
	x11.draw_lines(x11_dpy, x11_pix, x11_gc, (XPoint*)points, n/2, CoordModeOrigin);
}

void x11_draw_end(void){
	if(!x11_win || !x11_visible) return;

	struct present_pixmap_req* req = x11.get_request(x11_dpy, x11_present_op, sizeof(*req));
	memset(req->crap, 0, sizeof(req->crap));
	req->subtype = 1;
	req->window = x11_win;
	req->pixmap = x11_pix;

	if(x11_dpy->synchandler)
		x11_dpy->synchandler(x11_dpy);

	x11.flush(x11_dpy);
}

void x11_toggle(void){
	if(!x11_win) return;

	if(x11_visible){
		x11.unmap_window(x11_dpy, x11_win);
	} else {
		x11.map_window(x11_dpy, x11_win);
	}

	x11_visible = !x11_visible;
	x11.flush(x11_dpy);
}
