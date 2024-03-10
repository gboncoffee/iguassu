#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/X.h>
#include <X11/Xlib-xcb.h>
#include <xcb/res.h>
#include <ctype.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#define DRW_IMPLEMENTATION
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
	pid_t pid;
	struct Client *next;
} Client;

typedef struct Container {
	Client *clients;
	short int allow_config_req;
	short int hidden;
	struct Container *next;
	struct Container *prev;
} Container;

typedef struct Cursors {
	Cursor left_ptr;
	Cursor crosshair;
	Cursor fleur;
	Cursor sizing;
} Cursors;

typedef struct Iguassu {
	Container *containers;
	Drw *menu_drw;
	Clr *menu_color;
	Clr *menu_color_f;
	Fnt *menu_font;
	Window menu_win;
	Window swipe_win;
	Display *dpy;
	xcb_connection_t *xcb_con;
	int screen;
	int sw;
	int sh;
	Window root;
	int wnumber;
	Cursors cursors;
	KeyCode fkey;
	KeyCode rkey;
	KeyCode akey;
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

void child_handler(int _a)
{
	wait(NULL);
}

Client *find_window_in_container(Container *con, Window win)
{
	for (Client *c = con->clients; c != NULL; c = c->next)
		if (c->id == win)
			return c;
	return NULL;
}

Client *find_window(Iguassu *i, Window win)
{
	Client *cli;
	for (Container *c = i->containers; c != NULL; c = c->next)
		if ((cli = find_window_in_container(c, win)) != NULL)
			return cli;
	return NULL;
}

Container *find_container(Iguassu *i, Window win)
{
	Client *cli;
	for (Container *c = i->containers; c != NULL; c = c->next)
		if ((cli = find_window_in_container(c, win)) != NULL)
			return c;
	return NULL;
}

Container *get_current(Iguassu *i)
{
	for (Container *c = i->containers; c != NULL; c = c->next)
		if (!c->hidden)
			return c;
	return NULL;
}

int n_hidden(Iguassu *i)
{
	int n = 0;
	for (Container *c = i->containers; c != NULL; c = c->next)
		if (c->hidden)
			n++;
	return n;
}

int n_cont(Iguassu *i)
{
	int n = 0;
	for (Container *c = i->containers; c != NULL; c = c->next)
		n++;
	return n;
}

int n_cli(Iguassu *i)
{
	int n = 0;
	for (Container *c = i->containers; c != NULL; c = c->next)
		for (Client *l = c->clients; l != NULL; l = l->next)
			n++;
	return n;
}

void restore_focus(Iguassu *i)
{
	Client *c;
	int first = 1;
	for (Container *con = i->containers; con != NULL; con = con->next) {
		c = con->clients;

		if (!con->hidden) {
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

			for (c = c->next; c != NULL; c = c->next)
				XUnmapWindow(i->dpy, c->id);
			c = con->clients;
			XMapWindow(i->dpy, c->id);

			if (first) {
				XRaiseWindow(i->dpy, c->id);
				XSetInputFocus(i->dpy, c->id, RevertToParent, CurrentTime);
				XUngrabButton(i->dpy, AnyButton, AnyModifier, c->id);
				first = 0;
			}
		} else {
			for (; c != NULL; c = c->next)
				XUnmapWindow(i->dpy, c->id);
		}
	}
}

void focus_container(Iguassu *i, Container *c)
{
	if (c == NULL)
		return;
	c->hidden = 0;

	if (i->containers != c) {
		if (c->prev != NULL)
			c->prev->next = c->next;
		if (c->next != NULL)
			c->next->prev = c->prev;
		c->prev = NULL;
		c->next = i->containers;
		if (i->containers != NULL)
			i->containers->prev = c;
		i->containers = c;
	}

	restore_focus(i);
}

#define focus_window(i, win) focus_container((i), find_container((i), (win)))

void focus_by_idx(Iguassu *i, int n)
{
	for (Container *c = i->containers; c != NULL; c = c->next) {
		if (!n) {
			focus_container(i, c);
			break;
		}
		n--;
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

void move_container(Iguassu *i, Container *c)
{
	XEvent ev;
	int x, y;
	unsigned int width, height;
	int delta_x, delta_y;
	int _dumb;
	unsigned int _dumbu;
	Window _dumbw;

	XGetGeometry(i->dpy, c->clients->id, &_dumbw, &x, &y, &width, &height, &_dumbu, &_dumbu);
	XMoveResizeWindow(i->dpy, i->swipe_win, x, y, width, height);
	XMapRaised(i->dpy, i->swipe_win);

	XQueryPointer(i->dpy, i->swipe_win, &_dumbw, &_dumbw, &_dumb, &_dumb,
		&delta_x, &delta_y, &_dumbu);

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

	for (Client *cli = c->clients; cli != NULL; cli = cli->next)
		XMoveWindow(i->dpy, cli->id, x, y);

	focus_container(i, c);

clean:
	XUnmapWindow(i->dpy, i->swipe_win);
	XUngrabPointer(i->dpy, CurrentTime);
}

void reshape_container(Iguassu *i, Container *c)
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

	for (Client *cli = c->clients; cli != NULL; cli = cli->next)
		XMoveResizeWindow(i->dpy, cli->id, x, y, w, h);

clean:
	XUnmapWindow(i->dpy, i->swipe_win);
	XUngrabPointer(i->dpy, CurrentTime);
}

void fullscreen_container(Iguassu *i, Container *c)
{
	XEvent ev;
	XKeyEvent e;
	XSetWindowBorderWidth(i->dpy, c->clients->id, 0);
	XMoveResizeWindow(i->dpy, c->clients->id, 0, 0, i->sw, i->sh);

	for (;;) {
		XSync(i->dpy, False);
		XNextEvent(i->dpy, &ev);

		if (ev.type == KeyPress) {
			e = ev.xkey;

			if (e.state == MODMASK && i->fkey == e.keycode) {
				XSetWindowBorderWidth(i->dpy, c->clients->id, BORDER_WIDTH);
				reshape_container(i, c);
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
	Client *c = find_window(i, e->window);
	if (c != NULL) {
		if (c->name != NULL)
			XFree(c->name);
		if (XGetWMName(i->dpy, c->id, &prop))
			c->name = (char*) prop.value;
		else
			c->name = NULL;
	}
}

void redraw_client(Iguassu *i, Client *c)
{
	Window _dumb;
	int x, y;
	unsigned int w, h, _dumbi;

	if (c != NULL) {
		XSync(i->dpy, False);
		XGetGeometry(i->dpy, c->id, &_dumb, &x, &y, &w, &h, &_dumbi, &_dumbi);
		XSync(i->dpy, False);
		XMoveResizeWindow(i->dpy, c->id, x, y, w-1, h);
		XSync(i->dpy, False);
		XMoveResizeWindow(i->dpy, c->id, x, y, w, h);
		XSync(i->dpy, False);
	}
}

int managed(Iguassu *i, Window win)
{
	return (find_window(i, win) != NULL);
}

void new_container(Iguassu *i, Window win, char *name, pid_t pid, short int allow_config_req, short int hidden)
{
	Container *c = malloc(sizeof(Container));
	assert(c != NULL && "Buy more ram lol");
	c->clients = malloc(sizeof(Client));
	assert(c->clients != NULL && "Buy more ram lol");

	c->prev = NULL;
	c->next = i->containers;
	if (i->containers != NULL)
		i->containers->prev = c;
	i->containers = c;

	c->allow_config_req = allow_config_req;
	c->hidden = hidden;

	c->clients->id = win;
	c->clients->name = name;
	c->clients->pid = pid;
	c->clients->next = NULL;
}

pid_t get_parent_pid(pid_t p)
{
	unsigned int v = 0;

	/* TODO: this is Linux-only. */
	FILE *f;
	char buf[256];
	snprintf(buf, sizeof(buf) - 1, "/proc/%u/stat", (unsigned) p);

	if (!(f = fopen(buf, "r")))
		return 0;

	fscanf(f, "%*u %*s %*c %u", &v);
	fclose(f);

	return (pid_t) v;
}

short int is_desc_process(pid_t p, pid_t c)
{
	while (p != c && c != 0)
		c = get_parent_pid(c);

	return (short int) c;
}

pid_t get_window_pid(Iguassu *i, Window w)
{
	int result = 0;

	xcb_res_client_id_spec_t spec = {0};
	spec.client = w;
	spec.mask = XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID;

	xcb_generic_error_t *e = NULL;
	xcb_res_query_client_ids_cookie_t c = xcb_res_query_client_ids(i->xcb_con, 1, &spec);
	xcb_res_query_client_ids_reply_t *r = xcb_res_query_client_ids_reply(i->xcb_con, c, &e);

	if (!r)
		return (pid_t) 0;

	xcb_res_client_id_value_iterator_t it = xcb_res_query_client_ids_ids_iterator(r);
	for (; it.rem; xcb_res_client_id_value_next(&it)) {
		spec = it.data->spec;
		if (spec.mask & XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID) {
			uint32_t *t = xcb_res_client_id_value_value(it.data);
			result = *t;
			break;
		}
	}

	free(r);

	if (result == (pid_t) - 1)
		result = 0;
	return result;
}

int try_manage_from_new(Iguassu *i, Window win, pid_t pid, char *name)
{
	if (pid == 0)
		return 0;

	for (Container *c = i->containers; c != NULL; c = c->next) {
		if (c->clients->pid == pid && c->clients->id == None) {
			c->clients->id = win;
			c->clients->name = name;
			reshape_container(i, c);
			focus_container(i, c);
			return 1;
		}
	}

	return 0;
}

int try_manage_on_container(Iguassu *i, Window win, pid_t pid, char *name)
{
	Client *new_client;
	int x, y;
	unsigned int width, height, _dumbu;
	Window _dumbw;

	for (Container *c = i->containers; c != NULL; c = c->next) {
		if (is_desc_process(c->clients->pid, pid) && c->clients->id != None) {
			new_client = malloc(sizeof(Client));
			assert(new_client != NULL && "Buy more ram lol");

			XGetGeometry(i->dpy, c->clients->id, &_dumbw, &x, &y, &width, &height, &_dumbu, &_dumbu);
			XMoveResizeWindow(i->dpy, win, x, y, width, height);
			new_client->next = c->clients;
			new_client->pid = pid;
			new_client->name = name;
			new_client->id = win;
			c->clients = new_client;

			focus_container(i, c);

			return 1;
		}
	}

	return 0;
}

void manage_new(Iguassu *i, Window win, pid_t pid, char *name)
{
	new_container(i, win, name, pid, 1, 0);
	restore_focus(i);
}

void manage(Iguassu *i, Window win)
{
	XTextProperty prop;
	pid_t pid = get_window_pid(i, win);
	char *name;

	if (XGetWMName(i->dpy, win, &prop))
		name = (char*) prop.value;
	else
		name = NULL;

	XGrabButton(i->dpy,
		AnyButton,
		AnyModifier,
		win,
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

	if (try_manage_from_new(i, win, pid, name))
		return;
	if (try_manage_on_container(i, win, pid, name))
		return;
	manage_new(i, win, pid, name);
}

void map_requested(Iguassu *i, XEvent *ev)
{
	XWindowAttributes wa;
	XMapRequestEvent *e = &ev->xmaprequest;

	if (!XGetWindowAttributes(i->dpy, e->window, &wa)
		|| wa.override_redirect
		|| managed(i, e->window))

		return;
	manage(i, e->window);
}

void remove_null_container(Iguassu *i, Container *c)
{
	assert(c->clients == NULL);

	if (c->prev != NULL)
		c->prev->next = c->next;
	if (c->next != NULL)
		c->next->prev = c->prev;
	if (i->containers == c)
		i->containers = c->next;
	free(c);
}

void unmanage(Iguassu *i, Container *c, Window win)
{
	Client *prev = NULL;
	for (Client *cli = c->clients; cli != NULL; cli = cli->next) {
		if (cli->id == win) {
			if (prev != NULL)
				prev->next = cli->next;
			else if (c->clients == cli)
				c->clients = cli->next;
			if (cli->name != NULL)
				XFree(cli->name);
			free(cli);
			break;
		}

		prev = cli;
	}

	if (c->clients == NULL)
		remove_null_container(i, c);

	restore_focus(i);
}

void destroy_notify(Iguassu *i, XEvent *ev)
{
	XDestroyWindowEvent *e = &ev->xdestroywindow;
	Container *c = find_container(i, e->window);
	if (c != NULL)
		unmanage(i, c, e->window);
}

void hide(Iguassu *i, Window win)
{
	Container *c = find_container(i, win);
	if (c != NULL) {
		c->hidden = 1;
		restore_focus(i);
	}
}

void unhide_by_idx(Iguassu *i, int n)
{
	for (Container *c = i->containers; c != NULL; c = c->next) {
		if (c->hidden) {
			n--;
			if (n == 0) {
				c->hidden = 0;
				focus_container(i, c);
				return;
			}
		}
	}
}

int draw_main_menu(Iguassu *i, int x, int y, int cur_x, int cur_y, int w, int h, int n_hid)
{
	int j, in_menu;
	int r = -1;

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

	for (Container *c = i->containers; c != NULL; c = c->next) {
		if (c->hidden) {
			if (in_menu && cur_x >= h * j && cur_x < h * (j + 1)) {
				drw_setscheme(i->menu_drw, i->menu_color_f);
				r = j;
			} else {
				drw_setscheme(i->menu_drw, i->menu_color);
			}

			if (c->clients != NULL && c->clients->name != NULL)
				drw_text(i->menu_drw, 0, h * j, w, h, 0, c->clients->name, 0);

			j++;
		}
	}

	drw_map(i->menu_drw, i->menu_win, 0, 0, w, h * (5 + n_hid));

	return r;
}

int draw_container_menu(Iguassu *i, int x, int y, int cur_x, int cur_y, int w, int h, int nc)
{
	int j, in_menu;
	int r = -1;

	drw_rect(i->menu_drw, 0, 0, w, h * nc, 1, 0);

	in_menu = cur_x >= 0 && cur_y >= 0 && cur_x <= h * nc && cur_y <= w;
	j = 0;
	for (Container *c = i->containers; c != NULL; c = c->next) {
		if (in_menu && cur_x >= h * j && cur_x < h * (j + 1)) {
			drw_setscheme(i->menu_drw, i->menu_color_f);
			r = j;
		} else {
			drw_setscheme(i->menu_drw, i->menu_color);
		}

		if (c->clients != NULL && c->clients->name != NULL)
			drw_text(i->menu_drw, 0, h * j, w, h, 0, c->clients->name, 0);

		j++;
	}

	drw_map(i->menu_drw, i->menu_win, 0, 0, w, h * nc);

	return r;
}

void main_menu(Iguassu *i, int x, int y)
{
	unsigned int w, h;
	int sel, pid, win, n_hid;
	Container *c;
	XEvent ev;

	n_hid = n_hidden(i);
	XMapRaised(i->dpy, i->menu_win);

	drw_font_getexts(i->menu_font, MENU_LENGTH, sizeof(MENU_LENGTH), &w, &h);

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
			n_hid = n_hidden(i);
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
		new_container(i, None, NULL, pid, 0, 1);
		break;
	case MENU_RESHAPE:
		win = select_win(i);
		if (win != None) {
			if ((c = find_container(i, win)) != NULL) {
				reshape_container(i, c);
				restore_focus(i);
			}
		}
		break;
	case MENU_MOVE:
		win = select_win(i);
		if (win != None)
			if ((c = find_container(i, win)) != NULL)
				move_container(i, c);
		break;
	case MENU_DELETE:
		win = select_win(i);
		if (win != None)
			if ((c = find_container(i, win)) != NULL)
				for (Client *cli = c->clients; cli != NULL; cli = cli->next)
					XKillClient(i->dpy, cli->id);
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

void container_menu(Iguassu *i, int x, int y)
{
	int sel, nc;
	unsigned int w, h;
	XEvent ev;

	nc = n_cont(i);
	if (nc < 1)
		return;

	XMapRaised(i->dpy, i->menu_win);

	/* This keeps our menu consistent. */
	drw_font_getexts(i->menu_font, MENU_LENGTH, sizeof(MENU_LENGTH), &w, &h);

	x = x - (w / 2);
	XMoveResizeWindow(i->dpy, i->menu_win, x, y, w, h * nc);
	drw_resize(i->menu_drw, w, h * nc);
	sel = draw_container_menu(i, x, y, x, y, w, h, nc);

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
			nc = n_cont(i);
			if (nc < 1)
				goto clean;
			sel = draw_container_menu(
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
		else if (ev.button == Button1)
			container_menu(i, ev.x_root, ev.y_root);
	} else {
		focus_window(i, ev.window);
	}
}

void key_press(Iguassu *i, XEvent *e)
{
	XKeyEvent *ev = &e->xkey;
	Container *c;

	if (ev->state == MODMASK) {
		if (i->fkey == ev->keycode) {
			if ((c = get_current(i)) != NULL)
				fullscreen_container(i, c);
		} else if (i->rkey == ev->keycode) {
			if ((c = get_current(i)) != NULL)
				reshape_container(i, c);
		} else if (i->akey == ev->keycode) {
			if ((c = get_current(i)) != NULL)
				redraw_client(i, c->clients);
		}
	}
}

void configure_request(Iguassu *i, XEvent *ev)
{
	Window _dumb;
	int x, y;
	unsigned int w, h, _dumbi;
	XConfigureRequestEvent *e = &ev->xconfigurerequest;
	Container *c = find_container(i, e->window);
	if (c == NULL || !c->allow_config_req)
		return;

	XGetGeometry(i->dpy, e->window, &_dumb, &x, &y, &w, &h, &_dumbi, &_dumbi);

	x = e->value_mask & CWX ? e->x : x;
	y = e->value_mask & CWY ? e->y : y;
	w = e->value_mask & CWWidth ? e->width : w;
	h = e->value_mask & CWHeight ? e->height : h;

	XMoveResizeWindow(i->dpy, e->window, x, y, w, h);
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
	case ConfigureRequest:
		configure_request(i, ev);
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
				manage(i, wins[j]);
		}
		if (wins)
			XFree(wins);
	}
}

int main(void)
{
	Iguassu iguassu;

	if (!(iguassu.dpy = XOpenDisplay(NULL)))
		return 1;
	if (!(iguassu.xcb_con = XGetXCBConnection(iguassu.dpy)))
		return 1;

	iguassu.screen = DefaultScreen(iguassu.dpy);
	iguassu.sw = DisplayWidth(iguassu.dpy, iguassu.screen);
	iguassu.sh = DisplayHeight(iguassu.dpy, iguassu.screen);
	iguassu.root = RootWindow(iguassu.dpy, iguassu.screen);

	/* I spend some time debugging stuff segfaulting because I didn't zeroed
	 * this pointer from the beggining. */
	iguassu.containers = NULL;

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
	if (iguassu.menu_drw == NULL)
		return 1;
	iguassu.menu_font = drw_fontset_create(iguassu.menu_drw, font, 1);
	if (iguassu.menu_font == NULL)
		return 1;
	iguassu.menu_color = drw_scm_create(iguassu.menu_drw, menu_color, 2);
	if (iguassu.menu_color == NULL)
		return 1;
	iguassu.menu_color_f = drw_scm_create(iguassu.menu_drw, menu_color_f, 2);
	if (iguassu.menu_color_f == NULL)
		return 1;

	drw_setscheme(iguassu.menu_drw, iguassu.menu_color);

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
	iguassu.akey = XKeysymToKeycode(iguassu.dpy, REDRAW_KEY);
	XGrabKey(iguassu.dpy,
		iguassu.akey,
		MODMASK,
		iguassu.root,
		True,
		GrabModeAsync,
		GrabModeAsync);

	XSetErrorHandler(error_handler);
	signal(SIGCHLD, child_handler);

	scan(&iguassu);
	main_loop(&iguassu);

	return 0;
}
