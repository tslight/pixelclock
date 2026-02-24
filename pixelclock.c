/*
 * "A different way of looking at time."
 *
 * Refactor of pixelclock, inspired by my pixelbatt adventures...
 *
 * Copyright (c) 2025 Toby Slight <tslight@pm.me>
 * Copyright (c) 2005,2008-2009 joshua stein <jcs@jcs.org>
 * Copyright (c) 2005 Federico G. Schwindt
 *
 */
#include "pixelclock.h"

static void
usage(void)
{
	errx(1,
	     "usage:\n"
	     "[-size <pixels>]            Width of bar in pixels.\n"
	     "[-font <xftfont>]           Defaults to 'monospace:bold:size=18'.\n"
	     "[-display <host:dpy>]       Specify a display to use.\n"
	     "[-left|-right|-top|-bottom] Specify screen edge.\n"
	     "[time1 time2 ... <HH:MM>]   Specify times to highlight.");
}

static void
kill_popup(void)
{
	if (x.popup != None) {
		XUnmapWindow(x.dpy, x.popup); // keep the window for reuse
		XFlush(x.dpy);
	}
}

/* Logic stolen and adapted from xbattbar... */
static void
show_popup(void)
{
	char msg[64];
	XftDraw *xftdraw = NULL;
	XGlyphInfo extents;
	XSetWindowAttributes att;
	const int padw = 2, padh = 2;
	struct tm *t = localtime(&pc.now);

	strftime(msg, sizeof(msg), TIMEFMT, t);

	if (!x.font) { // cache to avoid calling on every popup
		x.font = XftFontOpenName(x.dpy, x.screen, font);
		if (!x.font)
			err(1, "XftFontOpenName failed for %s", font);
	}

	// Get width and height of message
	XftTextExtentsUtf8(x.dpy, x.font, (FcChar8 *)msg, (int)strlen(msg),
			   &extents);

	int boxw = extents.xOff +
		2 * padw; // offset better than width for some reason!
	int boxh = (x.font->ascent + x.font->descent) +
		2 * padh; // reliable line height
	/* clamp to screen size to avoid (unsigned) wrapping and BadValue */
	if (boxw > x.width)
		boxw = x.width - 2;
	if (boxh > x.height)
		boxh = x.height - 2;
	int left = (x.width - boxw) / 2;
	if (left < 0)
		left = 0;
	int top = (x.height - boxh) / 2;
	if (top < 0)
		top = 0;

	if (x.popup == None) {
		/* create once; resize/move on subsequent shows */
		x.popup = XCreateSimpleWindow(x.dpy, DefaultRootWindow(x.dpy),
					      left, top, (unsigned int)boxw, (unsigned int)boxh, 1,
					      x.magenta, x.black);
		att.override_redirect = True;
		XChangeWindowAttributes(x.dpy, x.popup, CWOverrideRedirect,
					&att);
	} else {
		XMoveResizeWindow(x.dpy, x.popup, left, top, (unsigned int)boxw,
				  (unsigned int)boxh);
	}

	XMapRaised(x.dpy, x.popup);

	xftdraw = XftDrawCreate(x.dpy, x.popup, DefaultVisual(x.dpy, x.screen),
				x.colormap);
	if (!xftdraw)
		err(1, "XftDrawCreate");

	static XRenderColor font_green = { 0x0000, 0xffff, 0x0000, 0xffff };

	if (!XftColorAllocValue(x.dpy, DefaultVisual(x.dpy, x.screen),
				x.colormap, &font_green, &x.fontcolor))
		err(1, "XftColorAllocValue");
	XftDrawStringUtf8(xftdraw, &x.fontcolor, x.font, padw,
			  padh + x.font->ascent, (FcChar8 *)msg, (int)strlen(msg));

	XftDrawDestroy(xftdraw); // Free Xft resources
	XftColorFree(x.dpy, DefaultVisual(x.dpy, x.screen), x.colormap,
		     &x.fontcolor);
	XFlush(x.dpy);
}

static void
redraw(void)
{
	int y;
	struct tm *t;

	if (gettimeofday(&pc.tv, NULL))
		errx(1, "gettimeofday");

	pc.now = pc.tv.tv_sec;

	if ((t = localtime(&pc.now)) == NULL)
		errx(1, "localtime");

	pc.newpos = (pc.hourtick * t->tm_hour) +
		(int)(((float)t->tm_min / 60.0) * pc.hourtick) - 3;

	/* only redraw if our time changed enough to move the box */
	if (pc.newpos != pc.lastpos) {
		XClearWindow(x.dpy, x.bar);

		/* draw the current time */
		XSetForeground(x.dpy, x.gc, x.yellow);
		if (x.position == 'b' || x.position == 't')
			XFillRectangle(x.dpy, x.bar, x.gc, pc.newpos, 0, 6,
				       x.size);
		else
			XFillRectangle(x.dpy, x.bar, x.gc, 0, pc.newpos, x.size,
				       6);

		/* draw the hour ticks */
		XSetForeground(x.dpy, x.gc, x.magenta);
		for (y = 1; y <= 23; y++)
			if (x.position == 'b' || x.position == 't')
				XFillRectangle(x.dpy, x.bar, x.gc,
					       (y * pc.hourtick), 0, 2, x.size);
			else
				XFillRectangle(x.dpy, x.bar, x.gc, 0,
					       (y * pc.hourtick), x.size, 2);

		/* highlight requested times */
		XSetForeground(x.dpy, x.gc, x.green);
		for (int i = 0; i < pc.nhihours; i++)
			if (x.position == 'b' || x.position == 't')
				XFillRectangle(x.dpy, x.bar, x.gc,
					       (int)(pc.hihours[i] *
						     (float)pc.hourtick),
					       0, 2, x.size);
			else
				XFillRectangle(x.dpy, x.bar, x.gc, 0,
					       (int)(pc.hihours[i] *
						     (float)pc.hourtick),
					       x.size, 2);

		pc.lastpos = pc.newpos;
		XFlush(x.dpy);
	}
}

static unsigned long
getcolor(const char *color)
{
	int rc;
	XColor tcolor;

	if (!(rc = XAllocNamedColor(x.dpy, x.colormap, color, &tcolor,
				    &tcolor)))
		err(1, "can't allocate %s", color);

	return tcolor.pixel;
}

static void
init_x(const char *display)
{
	int rc;
	int left = 0, top = 0, width = 0, height = 0;
	XGCValues values;
	XSetWindowAttributes attributes;
	XTextProperty progname_prop;

	if (!(x.dpy = XOpenDisplay(display)))
		err(1, "unable to open display %s", XDisplayName(display));

	if (ConnectionNumber(x.dpy) >= FD_SETSIZE)
		errx(1,
		     "X connection fd >= FD_SETSIZE; cannot use select() safely");

	x.screen = DefaultScreen(x.dpy);
	x.width = DisplayWidth(x.dpy, x.screen);
	x.height = DisplayHeight(x.dpy, x.screen);
	x.popup = None;
	x.colormap = DefaultColormap(x.dpy, DefaultScreen(x.dpy));
	x.black = getcolor("black");
	x.magenta = getcolor("magenta");
	x.green = getcolor("green");
	x.yellow = getcolor("yellow");
	x.red = getcolor("red");
	x.blue = getcolor("blue");
	x.olive = getcolor("olive drab");

	switch (x.position) {
	case 'b':
		left = 0;
		height = (int)x.size;
		top = x.height - height;
		width = x.width;
		break;
	case 't':
		left = 0;
		top = 0;
		height = (int)x.size;
		width = x.width;
		break;
	case 'l':
		left = 0;
		top = 0;
		height = x.height;
		width = (int)x.size;
		break;
	case 'r':
		width = (int)x.size;
		left = x.width - width;
		top = 0;
		height = x.height;
		break;
	}

	x.bar = XCreateSimpleWindow(x.dpy, RootWindow(x.dpy, x.screen), left,
				    top, (unsigned int)width, (unsigned int)height, 0, x.black,
				    x.black);

	if (!(rc = XStringListToTextProperty(&progname, 1, &progname_prop)))
		err(1, "XStringListToTextProperty");

	XSetWMName(x.dpy, x.bar, &progname_prop);
	if (progname_prop.value)
		XFree(progname_prop.value);

	attributes.override_redirect =
		True; // brute force position/size and decoration
	XChangeWindowAttributes(x.dpy, x.bar, CWOverrideRedirect, &attributes);

	if (!(x.gc = XCreateGC(x.dpy, x.bar, 0, &values)))
		err(1, "XCreateGC");

	XMapWindow(x.dpy, x.bar);

	XSelectInput(x.dpy, x.bar,
		     ExposureMask | EnterWindowMask | LeaveWindowMask |
		     VisibilityChangeMask);

	XFlush(x.dpy);
	XSync(x.dpy, False);
}

static void
handler(int sig)
{
	(void)sig;
	terminate = 1;
}

static void
safe_atoui(const char *a, unsigned *ui)
{
	if (!a || !ui)
		errx(1, "nothing passed to safe_atoui");
	char *end;
	errno = 0;
	unsigned long l = strtoul(a, &end, 10);
	if (end == a || *end != '\0')
		errx(1, "invalid integer: %s", a);
	if (a[0] == '-')
		errx(1, "unsigned only: %s", a);
	if (errno == ERANGE || l > UINT_MAX)
		err(1, "out of range: %s", a);
	*ui = (unsigned int)l;
}

int
main(int argc, char *argv[])
{
	progname = argv[0];
	char *display = NULL;
	struct sigaction sa;
	XEvent event;
	int c;

	pc.hourtick = -1;
	pc.lastpos = -1;
	pc.newpos = 0;

	memset(&x, 0, sizeof(struct xinfo));
	x.size = DEFSIZE;
	x.position = 0;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	sa.sa_flags = 0; // ensure syscalls like select can be interrupted
	if (sigaction(SIGINT, &sa, NULL) == -1 ||
	    sigaction(SIGTERM, &sa, NULL) == -1)
		err(1, "sigaction");

	while ((c = getopt_long_only(argc, argv, "", longopts, NULL)) != -1) {
		switch (c) {
		case 'd':
			display = optarg;
			break;
		case 'f':
			if (!optarg || optarg[0] == '\0')
				errx(1, "empty font name");
			if (strnlen(optarg, 1024) >= 1024)
				errx(1, "font name too long");
			font = optarg;
			break;
		case 'b':
		case 't':
		case 'l':
		case 'r':
			x.position = (char)c;
			break;
		case 's':
			safe_atoui(optarg, &x.size);
			break;
		default:
			usage();
		}
	}

	if (!x.position)
		x.position = DEFPOS;

	argc -= optind;
	argv += optind;

	if (argc == 0) {
		/* use default times */
		pc.nhihours = sizeof(defhours) / sizeof(defhours[0]);
		if ((pc.hihours = alloca(sizeof(defhours))) == NULL)
			err(1, NULL);

		for (int i = 0; i < pc.nhihours; i++)
			pc.hihours[i] = defhours[i];
	} else {
		/* get times from args */
		pc.nhihours = argc;
		if ((pc.hihours = alloca(
			     (uint)pc.nhihours * sizeof(float))) == NULL)
			err(1, NULL);

		for (int i = 0; i < argc; ++i) {
			int h, m;
			char *p = argv[i];

			/* parse times like 14:12 */
			h = atoi(p);
			if ((p = strchr(p, ':')) == NULL)
				errx(1, "invalid time %s", argv[i]);
			m = atoi(p + 1);

			if (h > 23 || h < 0 || m > 59 || m < 0)
				errx(1, "Invalid time %s", argv[i]);

			pc.hihours[i] = (float)(h + (m / 60.0));
		}
	}

	init_x(display);

	if (x.size > (uint)x.width - 1) {
		warnx("%d is bigger than the display! Falling back to %d pixels.",
		      x.size, x.width - 1);
		x.size = (uint)x.width - 1;
	}

	/* each hour will be this many pixels away */
	pc.hourtick = ((x.position == 'b' || x.position == 't') ?
		       x.width : x.height) / 24;

	redraw();

	for (;;) {
		fd_set fds; // Set of file descriptors for select()
		int xfd = ConnectionNumber(x.dpy);
		FD_ZERO(&fds);	   // Clear the set of file descriptors
		FD_SET(xfd, &fds); // Add the X server connection to the set

		pc.tv.tv_sec = 60;
		pc.tv.tv_usec = 0; // No microseconds

		// Wait for either an X event or timeout
		int ret = select(xfd + 1, &fds, NULL, NULL, &pc.tv);

		if (terminate)
			break; // check signal handler

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			err(1, "select");
		} else if (ret > 0) { // At least one X event
			while (XPending(x.dpy)) {
				XNextEvent(x.dpy, &event);
				if (event.type == EnterNotify) {
					show_popup();
				} else if (event.type == LeaveNotify) {
					kill_popup();
				} else if (event.type == Expose) {
					pc.lastpos = -1;
				}
			}
		}
		redraw();
	}

	if (x.popup != None)
		XDestroyWindow(x.dpy, x.popup);
	if (x.font)
		XftFontClose(x.dpy, x.font);
	XCloseDisplay(x.dpy);
	exit(0);
}
