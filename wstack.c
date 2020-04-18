#include "dvtm.h"

/* layout the tile windows with vertical splits, master windows
 * with horizontal splits */

void
wstack(void)
{
	struct client *c;
	unsigned i;
	unsigned ny; /* y coordinate of the next window */
	unsigned nx; /* x coordinate of the next window */
	unsigned n;  /* number of visible clients */
	unsigned m;  /* number of clients in the master area */
	unsigned mh; /* height of all windows in master area */
	unsigned tw; /* width of each tile window */

	for( n = 0, c = nextvisible(clients); c; c = nextvisible(c->next) ) {
		if( !c->minimized ) {
			n++;
		}
	}

	m  = MAX(1, MIN(n, screen.nmaster));
	mh = n == m ? available_height : screen.mfact * available_height;
	tw = n == m ? 0 : available_width / (n - m);
	nx = main_window_x;
	ny = main_window_y + available_height - mh;

	for( i = 0, c = nextvisible(clients); c; c = nextvisible(c->next) ) {
		unsigned nh; /* height of the current window */
		unsigned nw; /* width of the current window */
		if( c->minimized ) {
			continue;
		}
		if (i < m) {	/* master */
			nh = mh / m;
			nw = available_width;
			nx = main_window_x;
		} else {	/* tile window */
			ny = main_window_y;
			if (i == m) {
				nx = main_window_x;
				nh = (main_window_y + available_height) - ny - mh;
			}
			if (i > m) {
				mvvline(ny, nx, ACS_VLINE, nh);
				mvaddch(ny, nx, ACS_TTEE);
				nx++;
			}
			nw = (i < n - 1) ? tw : (main_window_x + available_width) - nx;
		}
		resize(c, nx, ny, nw, nh);
		nx += nw;
		ny += nh;
		i++;
	}
}
