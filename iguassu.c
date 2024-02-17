#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/X.h>
/* #include <X11/keysym.h> */
/* #include <X11/Xproto.h> */
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
	short int hidden;
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
	KeyCode fkey;
	KeyCode rkey;
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

/* This may look like a bad pratice but this avoids things like setting the
 * focus to an already-destroyed window and crashing because of that. I swear I
 * know what I'm doing. */
int error_handler(Display *dpy, XErrorEvent *e)
{
	return 0;
}

Client *find_window(Client *c, Window win)
{
	if (c == NULL)
		return NULL;
	if (c->id == win)
		return c;
	return find_window(c->next, win);
}

Client *find_previous_window(Client *c, Window win)
{
	if (c == NULL || c->next == NULL)
		return NULL;
	if (c->next->id == win)
		return c;
	return find_previous_window(c->next, win);
}

Client *get_current(Client *c)
{
	if (c == NULL)
		return NULL;
	if (!c->hidden)
		return c;
	return get_current(c->next);
}

int n_hidden(Client *c)
{
	if (c == NULL)
		return 0;
	return c->hidden + n_hidden(c->next);
}

int n_cli(Client *c)
{
	if (c == NULL)
		return 0;
	return 1 + n_cli(c->next);
}

void restore_focus(Iguassu *i)
{
	Client *c = i->clients;
	int first = 1;
	while (c != NULL) {
		if (!c->hidden) {
			XMapWindow(i->dpy, c->id);
			if (first) {
				XRaiseWindow(i->dpy, c->id);
				XSetInputFocus(i->dpy, c->id, RevertToParent, CurrentTime);
				XUngrabButton(i->dpy, AnyButton, AnyModifier, c->id);
				first = 0;
				goto next;
			}
		}

		XGrabButton(i->dpy,
			AnyButton,
			AnyModifier,
			c->id,
			False,
			ButtonPressMask,
			GrabModeAsync,
			GrabModeSync,
			None,
			None);

next:
		c = c->next;
	}
}

void focus(Iguassu *i, Window win)
{
	Client *c = find_window(i->clients, win);
	if (c == NULL)
		return;
	c->hidden = 0;
	Client *p = find_previous_window(i->clients, win);
	if (p != NULL) {
		p->next = c->next;
		c->next = i->clients;
		i->clients = c;
	}

	restore_focus(i);
}

void focus_by_idx(Iguassu *i, int n)
{
	Client *c = i->clients;

	while (c != NULL) {
		if (n == 0) {
			focus(i, c->id);
			return;
		}
		n--;

		c = c->next;
	}
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
	Window _dumbw;

	XGetGeometry(i->dpy, c->id, &_dumbw, &x, &y, &width, &height, &_dumb, &_dumb);
	XMoveResizeWindow(i->dpy, i->swipe_win, x, y, width, height);
	XMapRaised(i->dpy, i->swipe_win);

	XQueryPointer(i->dpy, i->swipe_win, &_dumbw, &_dumbw, &_dumb, &_dumb,
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
	focus(i, c->id);

clean:
	XUnmapWindow(i->dpy, i->swipe_win);
	XUngrabPointer(i->dpy, CurrentTime);
}

void reshape_client(Iguassu *i, Client *c)
{
	XEvent ev;
	int fx, fy, x, y;
	int reshaping = 0;
	int w = MIN_WINDOW_SIZE;
	int h = MIN_WINDOW_SIZE;

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
					w = fx - ev.xbutton.x_root;
					x = ev.xbutton.x_root;
				} else {
					w = ev.xbutton.x_root - fx + 1;
					x = fx;
				}
				if (ev.xbutton.y_root < fy) {
					h = fy - ev.xbutton.y_root;
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
			x = fx;
			y = fy;
			reshaping = 1;
			XMoveResizeWindow(i->dpy, i->swipe_win, x, y, 1, 1);
			XMapRaised(i->dpy, i->swipe_win);
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
	focus(i, c->id);

clean:
	XUnmapWindow(i->dpy, i->swipe_win);
	XUngrabPointer(i->dpy, CurrentTime);
}

void fullscreen_client(Iguassu *i, Client *c)
{
	XEvent ev;
	XKeyEvent e;
	XSetWindowBorderWidth(i->dpy, c->id, 0);
	XMoveResizeWindow(i->dpy, c->id, 0, 0, i->sw, i->sh);

	for (;;) {
		XSync(i->dpy, False);
		XNextEvent(i->dpy, &ev);

		if (ev.type == KeyPress) {
			e = ev.xkey;

			if (e.state == MODMASK && i->fkey == e.keycode) {
				XSetWindowBorderWidth(i->dpy, c->id, BORDER_WIDTH);
				reshape_client(i, c);
				return;
			}
		} else {
			handle_event(i, &ev);
		}
	}
}

void property_change(Iguassu *i, XEvent *ev)
{
	XTextProperty prop;
	XPropertyEvent *e = &ev->xproperty;
	Client *c = find_window(i->clients, e->window);
	if (c != NULL) {
		if (c->name != NULL)
			XFree(c->name);
		if (XGetWMName(i->dpy, c->id, &prop))
			c->name = prop.value;
		else
			c->name = NULL;
	}
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

	new_client->hidden = 0;
	new_client->id = win;
	new_client->next = i->clients;
	i->clients = new_client;

	if (XGetWMName(i->dpy, win, &prop))
		new_client->name = prop.value;
	else
		new_client->name = NULL;

	XGrabButton(i->dpy,
		AnyButton,
		AnyModifier,
		new_client->id,
		False,
		ButtonPressMask,
		GrabModeAsync,
		GrabModeSync,
		None,
		None);

	XSelectInput(i->dpy,
		win,
		PointerMotionMask
		| PropertyChangeMask);

	XSetWindowBorder(i->dpy, win, BORDER_COLOR);
	XSetWindowBorderWidth(i->dpy, win, BORDER_WIDTH);
	restore_focus(i);
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

void unmanage(Iguassu *i, Client *c)
{
	Client *p = find_previous_window(i->clients, c->id);
	if (p != NULL)
		p->next = c->next;
	else if (i->clients == c)
		i->clients = c->next;
	if (c->name != NULL)
		XFree(c->name);
	free(c);

	restore_focus(i);
}

void destroy_notify(Iguassu *i, XEvent *ev)
{
	XDestroyWindowEvent *e = &ev->xdestroywindow;
	Client *c = find_window(i->clients, e->window);
	if (c != NULL)
		unmanage(i, c);
}

void hide(Iguassu *i, Window win)
{
	Client *c = find_window(i->clients, win);
	if (c != NULL) {
		c->hidden = 1;
		restore_focus(i);
		XUnmapWindow(i->dpy, c->id);
	}
}

void unhide_by_idx(Iguassu *i, int n)
{
	Client *c = i->clients;
	while (c != NULL) {
		if (c->hidden) {
			n--;
			if (n == 0) {
				c->hidden = 0;
				focus(i, c->id);
				return;
			}
		}

		c = c->next;
	}
}

int draw_main_menu(Iguassu *i, int x, int y, int cur_x, int cur_y, int w, int h, int n_hid)
{
	int j, in_menu;
	int r = -1;
	Client *c = i->clients;

	drw_rect(i->menu_drw, 0, 0, w, h * (5 + n_hid), 1, 0);

	in_menu = cur_x >= 0 && cur_y >= 0 && cur_x <= h * (5 + n_hid) && cur_y <= w;
	for (j = 0; j < 5; j++) {
		if (in_menu && cur_x >= h * j && cur_x < h * (j + 1)) {
			drw_setscheme(i->menu_drw, i->menu_color_f);
			r = j;
		} else {
			drw_setscheme(i->menu_drw, i->menu_color);
		}
		drw_text(i->menu_drw, 0, h * j, w, h, 0, main_menu_items[j], 0);
	}

	while (c != NULL) {
		if (c->hidden) {
			if (in_menu && cur_x >= h * j && cur_x < h * (j + 1)) {
				drw_setscheme(i->menu_drw, i->menu_color_f);
				r = j;
			} else {
				drw_setscheme(i->menu_drw, i->menu_color);
			}

			if (c->name != NULL)
				drw_text(i->menu_drw, 0, h * j, w, h, 0, c->name, 0);

			j++;
		}

		c = c->next;
	}

	drw_map(i->menu_drw, i->menu_win, 0, 0, w, h * (5 + n_hid));

	return r;
}

int draw_client_menu(Iguassu *i, int x, int y, int cur_x, int cur_y, int w, int h, int nc)
{
	int j, in_menu;
	int r = -1;
	Client *c = i->clients;

	drw_rect(i->menu_drw, 0, 0, w, h * nc, 1, 0);

	in_menu = cur_x >= 0 && cur_y >= 0 && cur_x <= h * nc && cur_y <= w;
	j = 0;
	while (c != NULL) {
		if (in_menu && cur_x >= h * j && cur_x < h * (j + 1)) {
			drw_setscheme(i->menu_drw, i->menu_color_f);
			r = j;
		} else {
			drw_setscheme(i->menu_drw, i->menu_color);
		}

		if (c->name != NULL)
			drw_text(i->menu_drw, 0, h * j, w, h, 0, c->name, 0);

		j++;

		c = c->next;
	}

	drw_map(i->menu_drw, i->menu_win, 0, 0, w, h * nc);

	return r;
}

void main_menu(Iguassu *i, int x, int y)
{
	int w, h, sel, pid, win, n_hid;
	Client *cli;
	XEvent ev;

	n_hid = n_hidden(i->clients);
	XMapRaised(i->dpy, i->menu_win);

	drw_font_getexts(i->menu_font, MENU_LENGTH, 7, &w, &h);

	x = x - (w / 2);
	XMoveResizeWindow(i->dpy, i->menu_win, x, y, w, h * (5 + n_hid));
	drw_resize(i->menu_drw, w, h * (5 + n_hid));
	sel = draw_main_menu(i, x, y, x, y, w, h, n_hid);

	XGrabPointer(i->dpy,
		i->menu_win,
		False,
		PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
		GrabModeAsync,
		GrabModeAsync,
		None,
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
			n_hid = n_hidden(i->clients);
			sel = draw_main_menu(
				i,
				x,
				y,
				ev.xmotion.y,
				ev.xmotion.x,
				w,
				h,
				n_hid);
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
		win = select_win(i);
		if (win != None)
			XKillClient(i->dpy, win);
		break;
	case MENU_HIDE:
		win = select_win(i);
		if (win != None)
			hide(i, win);
		break;
	default:
		if (sel >= 5) {
			sel = sel - 4;
			unhide_by_idx(i, sel);
		}
	}
}

void client_menu(Iguassu *i, int x, int y)
{
	int w, h, sel, win, nc;
	Client *cli;
	XEvent ev;

	nc = n_cli(i->clients);
	if (nc < 1)
		return;

	XMapRaised(i->dpy, i->menu_win);

	/* This keeps our menu consistent. */
	drw_font_getexts(i->menu_font, MENU_LENGTH, 7, &w, &h);

	x = x - (w / 2);
	XMoveResizeWindow(i->dpy, i->menu_win, x, y, w, h * nc);
	drw_resize(i->menu_drw, w, h * nc);
	sel = draw_client_menu(i, x, y, x, y, w, h, nc);

	XGrabPointer(i->dpy,
		i->menu_win,
		False,
		PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
		GrabModeAsync,
		GrabModeAsync,
		None,
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
			nc = n_cli(i->clients);
			if (nc < 1)
				goto clean;
			sel = draw_client_menu(
				i,
				x,
				y,
				ev.xmotion.y,
				ev.xmotion.x,
				w,
				h,
				nc);
			break;
		default:
			handle_event(i, &ev);
		}
	}

clean:
	XUnmapWindow(i->dpy, i->menu_win);
	XUngrabPointer(i->dpy, CurrentTime);

	if (sel > -1)
		focus_by_idx(i, sel);
}

void button_press(Iguassu *i, XEvent *e)
{
	XButtonEvent ev = e->xbutton;

	if (ev.window == i->root) {
		if (ev.button == Button3)
			main_menu(i, ev.x_root, ev.y_root);
		else
			client_menu(i, ev.x_root, ev.y_root);
	} else {
		focus(i, ev.window);
	}
}

void key_press(Iguassu *i, XEvent *e)
{
	XKeyEvent *ev = &e->xkey;
	Client *c;

	if (ev->state == MODMASK) {
		if (i->fkey == ev->keycode) {
			if ((c = get_current(i->clients)) != NULL)
				fullscreen_client(i, c);
		} else if (i->rkey == ev->keycode) {
			if ((c = get_current(i->clients)) != NULL)
				reshape_client(i, c);
		}
	}
}

void handle_event(Iguassu *i, XEvent *ev)
{
	switch (ev->type) {
	case ButtonPress:
		button_press(i, ev);
		break;
	case KeyPress:
		key_press(i, ev);
		break;
	case MapRequest:
		map_requested(i, ev);
		break;
	case DestroyNotify:
		destroy_notify(i, ev);
		break;
	case PropertyNotify:
		property_change(i, ev);
		break;
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

void scan(Iguassu *i)
{
	unsigned int j, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if (XQueryTree(i->dpy, i->root, &d1, &d2, &wins, &num)) {
		for (j = 0; j < num; j++) {
			if (!XGetWindowAttributes(i->dpy, wins[j], &wa))
				continue;
			if (wa.override_redirect || wa.map_state != IsViewable)
				continue;
			if (wins[j] == i->menu_win || wins[j] == i->swipe_win)
				continue;
			if (!managed(i, wins[j]))
				manage(i, wins[j], &wa);
		}
		if (wins)
			XFree(wins);
	}
}

int main(void)
{
	Iguassu iguassu;
	XSetWindowAttributes swa;
	KeyCode code;

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

	/* Grab keys. */
	iguassu.fkey = XKeysymToKeycode(iguassu.dpy, FULLSCREEN_KEY);
	XGrabKey(iguassu.dpy,
		iguassu.fkey,
		MODMASK,
		iguassu.root,
		True,
		GrabModeAsync,
		GrabModeAsync);
	iguassu.rkey = XKeysymToKeycode(iguassu.dpy, RESHAPE_KEY);
	XGrabKey(iguassu.dpy,
		iguassu.rkey,
		MODMASK,
		iguassu.root,
		True,
		GrabModeAsync,
		GrabModeAsync);

	XSetErrorHandler(error_handler);

	scan(&iguassu);
	main_loop(&iguassu);

	return 0;
}
