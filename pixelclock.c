/* -*- tab-width: 8; c-basic-offset: 8; indent-tabs-mode: t -*-
 * vim:ts=8
 *
 * $Id: pixelclock.c,v 1.8 2009/03/09 06:35:26 jcs Exp $
 *
 * pixelclock
 * a different way of looking at time
 *
 * Copyright (c) 2025 Toby Slight <tslight@pm.me>
 * Copyright (c) 2005,2008-2009 joshua stein <jcs@jcs.org>
 * Copyright (c) 2005 Federico G. Schwindt
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <err.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

/* default clock size */
#define DEFSIZE 4

/* default position is along the right side */
#define DEFPOS 'r'

/* always on top of other windows */
#define RAISE 1

/* default font & time format of dialog window */
#define DiagFont "monospace:bold:size=18"
#define TimeFormat "%H:%M %A %d %B %Y"

/* so our window manager knows us */
char* win_name = "pixelclock";

/* default hours to highlight (3,6,9am 12,15,18,21pm) */
const float defhours[7] = { 3.0, 6.0, 9.0, 12.0, 15.0, 18.0, 21.0 };

struct xinfo {
	Display* dpy;
	int dpy_width, dpy_height;
	int screen;
	Window win;
	int size;
	char position;
	GC gc;
	Colormap win_colormap;
	unsigned long yellow;
	unsigned long magenta;
	unsigned long green;
	unsigned long black;
} x;

const struct option longopts[] = {
	{ "display",	required_argument,	NULL,	'd' },
	{ "size",	required_argument,	NULL,	's' },
	{ "left",	no_argument,		NULL,	'l' },
	{ "right",	no_argument,		NULL,	'r' },
	{ "top",	no_argument,		NULL,	't' },
	{ "bottom",	no_argument,		NULL,	'b' },
	{ "help",	no_argument,		NULL,	'h' },

	{ NULL,		0,			NULL,	0 }
};

extern char *__progname;

/* dialog window */
Window winstat = -1;

long	getcolor(const char *);
void	handler(int sig);
void	init_x(const char *);
void	usage(void);
void    showdiagbox(void);
void    disposediagbox(void);

int
main(int argc, char* argv[])
{
	char *display = NULL, *p;
	int c, i, y;
	int hourtick, lastpos = -1, newpos = 0;
	struct timeval tv[2];
	time_t now;
	struct tm *t;

	float *hihours;
	int nhihours;

	XEvent event;

	bzero(&x, sizeof(struct xinfo));
	x.size = DEFSIZE;
	x.position = 0;

	while ((c = getopt_long_only(argc, argv, "", longopts, NULL)) != -1) {
		switch (c) {
		case 'd':
			display = optarg;
			break;

		case 'b':
		case 't':
		case 'l':
		case 'r':
			if (x.position)
				errx(1, "only one of -top, -bottom, -left, "
				     "-right allowed");
				/* NOTREACHED */

			x.position = c;
			break;

		case 's':
			x.size = strtol(optarg, &p, 10);
			if (*p || x.size < 1)
				errx(1, "illegal value -- %s", optarg);
				/* NOTREACHED */
			break;
		case 'h':
			usage();
		default:
			usage();
			/* NOTREACHED */
		}
	}

	if (!x.position)
		x.position = DEFPOS;

	argc -= optind;
	argv += optind;

	if (argc == 0) {
		/* use default times */
		nhihours = sizeof(defhours) / sizeof(defhours[0]);
		if ((hihours = alloca(sizeof(defhours))) == NULL)
			err(1, NULL);

		for (i = 0; i < nhihours; i++)
			hihours[i] = defhours[i];
	} else {
		/* get times from args */
		nhihours = argc;
		if ((hihours = alloca(nhihours * sizeof(float))) == NULL)
			err(1, NULL);

		for (i = 0; i < argc; ++i) {
			int h, m;
			char *p = argv[i];

			/* parse times like 14:12 */
			h = atoi(p);
			if ((p = strchr(p, ':')) == NULL)
				errx(1, "invalid time %s", argv[i]);
			m = atoi(p + 1);

			if (h > 23 || h < 0 || m > 59 || m < 0)
				errx(1, "Invalid time %s", argv[i]);

			hihours[i] = h + (m / 60.0);
		}
	}

	init_x(display);

	signal(SIGINT, handler);
	signal(SIGTERM, handler);

	/* each hour will be this many pixels away */
	hourtick = ((x.position == 'b' || x.position == 't') ? x.dpy_width :
		    x.dpy_height) / 24;

	for (;;) {
		fd_set fds; // Set of file descriptors for select()
		int xfd = ConnectionNumber(x.dpy);
		FD_ZERO(&fds);     // Clear the set of file descriptors
		FD_SET(xfd, &fds); // Add the X server connection to the set
		tv[0].tv_sec = 1;     // Set timeout to 1 second
		tv[0].tv_usec = 0;    // No microseconds

		// Wait for either an X event or a 1-second timeout
		int ret = select(xfd + 1, &fds, NULL, NULL, &tv[0]);

		if (ret > 0) { // At least one X event
			while (XPending(x.dpy)) {
				XNextEvent(x.dpy, &event);
				if (event.type == EnterNotify) {
					showdiagbox();
				} else if (event.type == LeaveNotify) {
					disposediagbox();
				} else if (event.type == LeaveNotify) {
					disposediagbox();
				} else if (event.type == VisibilityNotify) {
					if (RAISE) XRaiseWindow(x.dpy, x.win);
				} else if (event.type == Expose) {
					lastpos = -1;
				}
			}
		}

		if (gettimeofday(&tv[0], NULL))
			errx(1, "gettimeofday");
			/* NOTREACHED */

		now = tv[0].tv_sec;
		if ((t = localtime(&now)) == NULL)
			errx(1, "localtime");
			/* NOTREACHED */

		newpos = (hourtick * t->tm_hour) +
			(float)(((float)t->tm_min / 60.0) * hourtick) - 3;

		/* only redraw if our time changed enough to move the box or if
		 * we were just exposed */
		if ((newpos != lastpos) || (event.type == Expose)) {
			XClearWindow(x.dpy, x.win);

			/* draw the current time */
			XSetForeground(x.dpy, x.gc, x.yellow);
			if (x.position == 'b' || x.position == 't')
				XFillRectangle(x.dpy, x.win, x.gc,
					       newpos, 0, 6, x.size);
			else
				XFillRectangle(x.dpy, x.win, x.gc,
					       0, newpos, x.size, 6);

			/* draw the hour ticks */
			XSetForeground(x.dpy, x.gc, x.magenta);
			for (y = 1; y <= 23; y++)
				if (x.position == 'b' || x.position == 't')
					XFillRectangle(x.dpy, x.win, x.gc,
						       (y * hourtick), 0, 2, x.size);
				else
					XFillRectangle(x.dpy, x.win, x.gc,
						       0, (y * hourtick), x.size, 2);

			/* highlight requested times */
			XSetForeground(x.dpy, x.gc, x.green);
			for (i = 0; i < nhihours; i++)
				if (x.position == 'b' || x.position == 't')
					XFillRectangle(x.dpy, x.win, x.gc,
						       (hihours[i] * hourtick), 0,
						       2, x.size);
				else
					XFillRectangle(x.dpy, x.win, x.gc,
						       0, (hihours[i] * hourtick),
						       x.size, 2);

			lastpos = newpos;

			XFlush(x.dpy);
		}
	}

	exit(1);
}

void
init_x(const char *display)
{
	int rc;
	int left = 0, top = 0, width = 0, height = 0;
	XGCValues values;
	XSetWindowAttributes attributes;
	XTextProperty win_name_prop;

	if (!(x.dpy = XOpenDisplay(display)))
		errx(1, "unable to open display %s", XDisplayName(display));
		/* NOTREACHED */

	x.screen = DefaultScreen(x.dpy);

	x.dpy_width = DisplayWidth(x.dpy, x.screen);
	x.dpy_height = DisplayHeight(x.dpy, x.screen);

	x.win_colormap = DefaultColormap(x.dpy, DefaultScreen(x.dpy));
	/* Minimize calls to getcolor */
	x.black   = getcolor("black");
	x.yellow  = getcolor("yellow");
	x.magenta = getcolor("magenta");
	x.green   = getcolor("green");

	switch (x.position) {
	case 'b':
		left = 0;
		height = x.size;
		top = x.dpy_height - height;
		width = x.dpy_width;
		break;
	case 't':
		left = 0;
		top = 0;
		height = x.size;
		width = x.dpy_width;
		break;
	case 'l':
		left = 0;
		top = 0;
		height = x.dpy_height;
		width = x.size;
		break;
	case 'r':
		width = x.size;
		left = x.dpy_width - width;
		top = 0;
		height = x.dpy_height;
		break;
	}

	x.win = XCreateSimpleWindow(x.dpy, RootWindow(x.dpy, x.screen),
				    left, top, width, height,
				    0, x.black, x.black);

	if (!(rc = XStringListToTextProperty(&win_name, 1, &win_name_prop)))
		errx(1, "XStringListToTextProperty");
		/* NOTREACHED */

	XSetWMName(x.dpy, x.win, &win_name_prop);

	/* remove all window manager decorations and force our position/size */
	/* XXX: apparently this is not very nice */
	attributes.override_redirect = True;
	XChangeWindowAttributes(x.dpy, x.win, CWOverrideRedirect, &attributes);

	if (!(x.gc = XCreateGC(x.dpy, x.win, 0, &values)))
		errx(1, "XCreateGC");
		/* NOTREACHED */

	XMapWindow(x.dpy, x.win);

	/* we want to know when we're exposed and when the mouse enters or
	   leaves the window */
	XSelectInput(x.dpy, x.win, ExposureMask|
				   EnterWindowMask|
				   LeaveWindowMask|
				   VisibilityChangeMask);

	XFlush(x.dpy);
	XSync(x.dpy, False);
}

/* Logic stolen and adapted from xbattbar... */
void showdiagbox(void) {
	XSetWindowAttributes att;
	int boxw, boxh;
	char diagmsg[64];
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	XftDraw *xftdraw = NULL;
	XftFont *xftfont;
	XftColor xftcolor;
	XGlyphInfo extents;

	strftime(diagmsg, sizeof(diagmsg), TimeFormat, t);
	xftfont = XftFontOpenName(x.dpy, x.screen, DiagFont);
	if (!xftfont)
		errx(1, "XftFontOpenName failed for %s", DiagFont);
	// Get width and height of message
	XftTextExtentsUtf8(x.dpy, xftfont, (FcChar8 *)diagmsg, strlen(diagmsg),
			   &extents);

	boxw = extents.width + 20;
	boxh = extents.height + 20;

	if(winstat != -1) disposediagbox();
	winstat = XCreateSimpleWindow(x.dpy, DefaultRootWindow(x.dpy),
				      (x.dpy_width-boxw)/2,
				      (x.dpy_height-boxh)/2,
				      boxw, boxh,
				      1,         // width
				      x.magenta, //border
				      x.black);  // background

	att.override_redirect = True;
	XChangeWindowAttributes(x.dpy, winstat, CWOverrideRedirect, &att);
	XMapWindow(x.dpy, winstat);

	xftdraw = XftDrawCreate(x.dpy,
				winstat,
				DefaultVisual(x.dpy, x.screen),
				x.win_colormap);
	XRenderColor render_color = { 0x0000,   // red
				      0xffff,   // green
				      0x0000,   // blue
				      0xffff }; // opacity
	XftColorAllocValue(x.dpy, DefaultVisual(x.dpy, x.screen),
			   x.win_colormap, &render_color, &xftcolor);
	XftDrawStringUtf8(xftdraw, &xftcolor, xftfont, 10, 30, (FcChar8 *)diagmsg,
			  strlen(diagmsg));

	// Free Xft resources
	XftDrawDestroy(xftdraw);
	XftFontClose(x.dpy, xftfont);
	XftColorFree(x.dpy, DefaultVisual(x.dpy, x.screen),
		     x.win_colormap, &xftcolor);
}

void disposediagbox(void) {
	if ( winstat != -1 ) {
		XDestroyWindow(x.dpy, winstat);
		winstat = -1;
	}
}

long
getcolor(const char *color)
{
	int rc;

	XColor tcolor;

	if (!(rc = XAllocNamedColor(x.dpy, x.win_colormap, color, &tcolor,
				    &tcolor)))
		errx(1, "can't allocate %s", color);

	return tcolor.pixel;
}

void
handler(int sig)
{
	XCloseDisplay(x.dpy);

	exit(0);
	/* NOTREACHED */
}

void
usage(void)
{
	fprintf(stderr, "usage: %s %s\n", __progname,
		"[-display host:dpy] "
		"[-help] "
		"[-left|-right|-top|-bottom] "
		"[-size <pixels>] "
		"[time1 time2 ... <HH:MM>]");
	exit(1);
}
