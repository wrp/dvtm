static void tstack(void)
{
	unsigned int i, nx, nw, nh, mw, tw;
	Client *c;
	unsigned ny; /* y coordinate of the next window */
	unsigned n;  /* number of visible clients */
	unsigned m;  /* number of clients in the master area */
	unsigned mh; /* height of all windows in master area */

	for (n = 0, c = nextvisible(clients); c; c = nextvisible(c->next))
		if (!c->minimized)
			n++;

	m  = MAX(1, MIN(n, screen.nmaster));
	mh = n == m ? wah : screen.mfact * wah;
	mw = waw / m;
	tw = n == m ? 0 : waw / (n - m);
	nx = wax;
	ny = way + wah - mh;

	for (i = 0, c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (c->minimized)
			continue;
		if (i < m) {	/* master */
			if (i > 0) {
				mvvline(ny, nx, ACS_VLINE, nh);
				mvaddch(ny, nx, ACS_TTEE);
				nx++;
			}
			nh = mh;
			nw = (i < m - 1) ? mw : (wax + waw) - nx;
		} else {	/* tile window */
			if (i == m) {
				nx = wax;
				ny = way;
				nh = (way + wah) - ny - mh;
			}
			if (i > m) {
				mvvline(ny, nx, ACS_VLINE, nh);
				mvaddch(ny, nx, ACS_TTEE);
				nx++;
			}
			nw = (i < n - 1) ? tw : (wax + waw) - nx;
		}
		resize(c, nx, ny, nw, nh);
		nx += nw;
		i++;
	}
}
