#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/X.h>
#include <ctype.h>
#include <assert.h>
#include <unistd.h>
#include "drw.h"

/*
 * Copyright (C) 2023  Gabriel de Brito
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

typedef struct Client {
	char *name;
	Window id;
	int mapped;
	struct Client *next;
} Client;

typedef struct Cursors {
	Cursor left_ptr;
	Cursor crosshair;
	Cursor fleur;
	Cursor sizing;
} Cursors;

typedef struct Iguassu {
	Client *clients;
	Drw *menu_drw;
	Clr *menu_color;
	Clr *menu_color_f;
	Fnt *menu_font;
	Window menu_win;
	Window swipe_win;
	Display *dpy;
	int screen;
	int sw;
	int sh;
	Window root;
	int wnumber;
	Cursors cursors;
} Iguassu;

#include "config.h"

/* Not configurable because obvious. If you change this anyway, go to `void
 * main_menu(Iguassu *i, int x, int y)` and change the function according.
 * The function draw_main_menu may also be changed. */
static const char *main_menu_items[] = {
	"New",
	"Reshape",
	"Move",
	"Delete",
	"Hide",
};
#define MENU_NEW 0
#define MENU_RESHAPE 1
#define MENU_MOVE 2
#define MENU_DELETE 3
#define MENU_HIDE 4

/* Some functions have a dependency in handle_event, so we declare it here. */
void handle_event(Iguassu *i, XEvent *ev);

Client *find_window(Client *c, Window win)
{
	if (c == NULL)
		return NULL;
	if (c->id == win)
		return c;
	return find_window(c->next, win);
}

Window select_win(Iguassu *i)
{
	XEvent ev;
	Window sel = None;
	XGrabPointer(
		i->dpy,
		i->root,
		True,
		ButtonPressMask,
		GrabModeAsync,
		GrabModeAsync,
		None,
		i->cursors.crosshair,
		CurrentTime);

	for (int exit = 0; !exit;) {
		XSync(i->dpy, False);
		XNextEvent(i->dpy, &ev);

		switch (ev.type) {
		case ButtonPress:
			if (ev.xbutton.button != Button2 && ev.xbutton.button != Button1)
				sel = ev.xbutton.subwindow;
			exit = 1;
			break;
		default:
			handle_event(i, &ev);
		}
	}

	XUngrabPointer(i->dpy, CurrentTime);

	return sel;
}

void move_client(Iguassu *i, Client *c)
{
	XEvent ev;
	int x, y, width, height;
	int delta_x, delta_y;
	int _dumb;

	XGetGeometry(i->dpy, c->id, &_dumb, &x, &y, &width, &height, &_dumb, &_dumb);
	XMoveResizeWindow(i->dpy, i->swipe_win, x, y, width, height);
	XMapWindow(i->dpy, i->swipe_win);
	XRaiseWindow(i->dpy, i->swipe_win);

	XQueryPointer(i->dpy, i->swipe_win, &_dumb, &_dumb, &_dumb, &_dumb,
		&delta_x, &delta_y, &_dumb);

	XGrabPointer(
		i->dpy,
		i->root,
		True,
		PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
		GrabModeAsync,
		GrabModeAsync,
		None,
		i->cursors.fleur,
		CurrentTime);

	for (int exit = 0; !exit;) {
		XSync(i->dpy, False);
		XNextEvent(i->dpy, &ev);

		switch (ev.type) {
		case MotionNotify:
			x = ev.xbutton.x - delta_x;
			y = ev.xbutton.y - delta_y;
			XMoveWindow(i->dpy, i->swipe_win, x, y);
			break;
		case ButtonPress:
			goto clean;
		case ButtonRelease:
			exit = 1;
		default:
			handle_event(i, &ev);
		}
	}

	XMoveWindow(i->dpy, c->id, x, y);

clean:
	XUnmapWindow(i->dpy, i->swipe_win);
	XUngrabPointer(i->dpy, CurrentTime);
}

void reshape_client(Iguassu *i, Client *c)
{
	XEvent ev;
	int fx, fy, x, y, w, h;
	int reshaping = 0;

	XGrabPointer(
		i->dpy,
		i->root,
		True,
		PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
		GrabModeAsync,
		GrabModeAsync,
		None,
		i->cursors.sizing,
		CurrentTime);

	for (int exit = 0; !exit;) {
		XSync(i->dpy, False);
		XNextEvent(i->dpy, &ev);

		switch (ev.type) {
		case MotionNotify:
			if (reshaping) {
				if (ev.xbutton.x_root < fx) {
					w = fx - ev.xbutton.x_root + 1;
					x = ev.xbutton.x_root;
				} else {
					w = ev.xbutton.x_root - fx + 1;
					x = fx;
				}
				if (ev.xbutton.y_root < fy) {
					h = fy - ev.xbutton.y_root + 1;
					y = ev.xbutton.y_root;
				} else {
					h = ev.xbutton.y_root - fy + 1;
					y = fy;
				}

				XMoveResizeWindow(i->dpy, i->swipe_win, x, y, w, h);
			}
			break;
		case ButtonRelease:
			/* Avoid events BEFORE actually reshaping. */
			if (reshaping)
				exit = 1;
			break;
		case ButtonPress:
			/* NOOOOO YOU SHOULDNT USE GOTO NOOOOOO *cries in
			 * high-level language* */
			if (ev.xbutton.button == Button2 || ev.xbutton.button == Button1)
				goto clean;
			fx = ev.xbutton.x_root;
			fy = ev.xbutton.y_root;
			reshaping = 1;
			XMoveResizeWindow(i->dpy, i->swipe_win, x, y, 1, 1);
			XMapWindow(i->dpy, i->swipe_win);
			XRaiseWindow(i->dpy, i->swipe_win);
			break;
		default:
			handle_event(i, &ev);
		}
	}

	if (w < MIN_WINDOW_SIZE)
		w = MIN_WINDOW_SIZE;
	if (h < MIN_WINDOW_SIZE)
		h = MIN_WINDOW_SIZE;

	XMoveResizeWindow(i->dpy, c->id, x, y, w, h);

clean:
	XUnmapWindow(i->dpy, i->swipe_win);
	XUngrabPointer(i->dpy, CurrentTime);
}

int managed(Iguassu *i, Window win)
{
	return (find_window(i->clients, win) != NULL);
}

void manage(Iguassu *i, Window win, XWindowAttributes *wa)
{
	XTextProperty prop;

	Client *new_client = malloc(sizeof(Client));
	assert((new_client != NULL) || "Buy more ram lol");

	new_client->id = win;
	new_client->next = i->clients;
	i->clients = new_client;

	if (XGetWMName(i->dpy, win, &prop))
		new_client->name = prop.value;
	else
		new_client->name = NULL;

	XSelectInput(i->dpy,
		win,
		PointerMotionMask
		| PropertyChangeMask);

	XSetWindowBorder(i->dpy, win, BORDER_COLOR);
	XSetWindowBorderWidth(i->dpy, win, BORDER_WIDTH);
	XMapWindow(i->dpy, win);
	XSetInputFocus(i->dpy, win, RevertToParent, CurrentTime);
	XRaiseWindow(i->dpy, win);
}

void map_requested(Iguassu *i, XEvent *ev)
{
	XWindowAttributes wa;
	XMapRequestEvent *e = &ev->xmaprequest;

	if (!XGetWindowAttributes(i->dpy, e->window, &wa))
		return;
	if (wa.override_redirect)
		return;
	if (e->window == i->menu_win || e->window == i->swipe_win)
		return;
	if (!managed(i, e->window))
		manage(i, e->window, &wa);
}

int draw_main_menu(Iguassu *i, int x, int y, int cur_x, int cur_y, int w, int h)
{
	int j, in_menu;
	int r = -1;

	drw_rect(i->menu_drw, 0, 0, w, h * 5, 1, 0);

	in_menu = cur_x >= 0 && cur_y >= 0 && cur_x <= h * 5 && cur_y <= w;
	for (j = 0; j < 5; j++) {
		if (in_menu && cur_x >= h * j && cur_x < h * (j + 1)) {
			drw_setscheme(i->menu_drw, i->menu_color_f);
			r = j;
		} else {
			drw_setscheme(i->menu_drw, i->menu_color);
		}
		drw_text(i->menu_drw, 0, h * j, w, h, 0, main_menu_items[j], 0);
	}

	drw_map(i->menu_drw, i->menu_win, 0, 0, w, h * 5);

	return r;
}

void main_menu(Iguassu *i, int x, int y)
{
	int w, h, sel, pid, win;
	Client *cli;
	XEvent ev;

	XMapRaised(i->dpy, i->menu_win);

	drw_font_getexts(i->menu_font, "Reshape", 7, &w, &h);

	x = x - (w / 2);
	XMoveResizeWindow(i->dpy, i->menu_win, x, y, w, h * 5);
	drw_resize(i->menu_drw, w, h * 5);
	sel = draw_main_menu(i, x, y, x, y, w, h);

	XGrabPointer(i->dpy,
		i->menu_win,
		False,
		PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
		GrabModeAsync,
		GrabModeAsync,
		i->menu_win,
		i->cursors.left_ptr,
		CurrentTime);

	for (int exit = 0; !exit;) {
		XSync(i->dpy, False);
		XNextEvent(i->dpy, &ev);

		switch (ev.type) {
		case ButtonPress:
		case ButtonRelease:
			exit = 1;
			break;
		case MotionNotify:
			sel = draw_main_menu(
				i,
				x,
				y,
				ev.xmotion.y,
				ev.xmotion.x,
				w,
				h);
			break;
		default:
			handle_event(i, &ev);
		}
	}

	XUnmapWindow(i->dpy, i->menu_win);
	XUngrabPointer(i->dpy, CurrentTime);

	switch (sel) {
	case MENU_NEW:
		pid = fork();
		assert(pid != -1 && "wtf cannot fork lol");
		if (pid == 0) {
			execlp(TERMINAL, TERMINAL, NULL);
			exit(1);
		}
		break;
	case MENU_RESHAPE:
		win = select_win(i);
		if (win != None) {
			if ((cli = find_window(i->clients, win)) != NULL)
				reshape_client(i, cli);
		}
		break;
	case MENU_MOVE:
		win = select_win(i);
		if (win != None) {
			if ((cli = find_window(i->clients, win)) != NULL)
				move_client(i, cli);
		}
		break;
	case MENU_DELETE:
		break;
	case MENU_HIDE:
		break;
	}
}

void button_press(Iguassu *i, XEvent *e)
{
	XButtonEvent ev = e->xbutton;

	if (ev.window == i->root) {
		if (ev.button == Button3)
			main_menu(i, ev.x, ev.y);
		else
			printf("other menu\n");
	} else {
		printf("focus change\n");
	}
}

void handle_event(Iguassu *i, XEvent *ev)
{
	switch (ev->type) {
	case ButtonPress:
		button_press(i, ev);
		break;
	case KeyPress:
		printf("received keypress\n");
		break;
	case MapRequest:
		map_requested(i, ev);
		break;
	case DestroyNotify:
		printf("received destroy notify\n");
		break;
	case UnmapNotify:
		printf("received unmap notify\n");
		break;
	case PropertyNotify:
		printf("received property notify\n");
		break;
	default:
		printf("received event %d\n", ev->type);
	}
}

void main_loop(Iguassu *i)
{
	XEvent ev;

	for (;;) {
		XSync(i->dpy, False);
		XNextEvent(i->dpy, &ev);
		handle_event(i, &ev);
	}
}

int main(void)
{
	Iguassu iguassu;
	XSetWindowAttributes swa;

	if (!(iguassu.dpy = XOpenDisplay(NULL)))
		return 1;

	iguassu.screen = DefaultScreen(iguassu.dpy);
	iguassu.sw = DisplayWidth(iguassu.dpy, iguassu.screen);
	iguassu.sh = DisplayHeight(iguassu.dpy, iguassu.screen);
	iguassu.root = RootWindow(iguassu.dpy, iguassu.screen);

	/* I spend some time debugging stuff segfaulting because I didn't zeroed
	 * this pointer from the beggining. */
	iguassu.clients = NULL;

	/* Register to get the events. */
	long mask = SubstructureRedirectMask
		| SubstructureNotifyMask
		| ButtonPressMask
		| ButtonReleaseMask
		| StructureNotifyMask
		| PropertyChangeMask;

	XSelectInput(iguassu.dpy, iguassu.root, mask);

	/* Create the menu. */
	iguassu.menu_drw = drw_create(iguassu.dpy, iguassu.screen, iguassu.root, 10, 10);
	iguassu.menu_font = drw_fontset_create(iguassu.menu_drw, font, 1);
	iguassu.menu_color = drw_scm_create(iguassu.menu_drw, menu_color, 2);
	iguassu.menu_color_f = drw_scm_create(iguassu.menu_drw, menu_color_f, 2);

	swa.override_redirect = True;
	swa.background_pixel = iguassu.menu_color[ColBg].pixel;
	swa.event_mask = ExposureMask | KeyPressMask | VisibilityChangeMask;
	iguassu.menu_win = XCreateSimpleWindow(
		iguassu.dpy,
		iguassu.root,
		0,
		0,
		10,
		10,
		BORDER_WIDTH,
		MENU_BORDER_COLOR,
		MENU_BACKGROUND_COLOR);

	/* And the swipe. */
	iguassu.swipe_win = XCreateSimpleWindow(
		iguassu.dpy,
		iguassu.root,
		0,
		0,
		10,
		10,
		BORDER_WIDTH,
		SWIPE_BORDER_COLOR,
		SWIPE_BACKGROUND);

	/* Create the cursors. */
	iguassu.cursors.left_ptr = XCreateFontCursor(iguassu.dpy, 68);
	iguassu.cursors.crosshair = XCreateFontCursor(iguassu.dpy, 34);
	iguassu.cursors.fleur = XCreateFontCursor(iguassu.dpy, 52);
	iguassu.cursors.sizing = XCreateFontCursor(iguassu.dpy, 120);

#ifdef BACKGROUND
	XSetWindowBackground(iguassu.dpy, iguassu.root, BACKGROUND);
	XClearWindow(iguassu.dpy, iguassu.root);
#endif
	XDefineCursor(iguassu.dpy, iguassu.root, iguassu.cursors.left_ptr);

	main_loop(&iguassu);

	return 0;
}
