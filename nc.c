/* $Revision$ */
#include <math.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <ncurses.h>

#include "error.h"
#include "gen.h"
#include "main.h"
#include "kalman.h"
#ifdef FW
#include "fft.h"
#endif

char win_resize = 0;
WINDOW *w_stats = NULL, *w_line1 = NULL, *w_slow = NULL, *w_line2 = NULL, *w_fast = NULL;
unsigned int max_x = 80, max_y = 25;
int stats_h = 8;
int logs_n = 0, slow_n = 0, fast_n = 0;
char **slow_history = NULL, **fast_history = NULL;
int window_history_n = 0;

double graph_limit = MY_DOUBLE_INF;
double hz = 1.0;

double *history = NULL, *history_temp = NULL, *history_fft_magn = NULL, *history_fft_phase = NULL;
char *history_set = NULL;
unsigned int history_n = 0;

char draw_phase = 0;

char pause_graphs = 0;

typedef enum { C_WHITE = 0, C_GREEN, C_YELLOW, C_BLUE, C_MAGENTA, C_CYAN, C_RED } color_t;

void determine_terminal_size(unsigned int *max_y, unsigned int *max_x)
{
        struct winsize size;

        *max_x = *max_y = 0;

        if (ioctl(1, TIOCGWINSZ, &size) == 0)
        {
                *max_y = size.ws_row;
                *max_x = size.ws_col;
        }

        if (!*max_x || !*max_y)
        {
                char *dummy = getenv("COLUMNS");
                if (dummy)
                        *max_x = atoi(dummy);
                else
                        *max_x = 80;

                dummy = getenv("LINES");
                if (dummy)
                        *max_x = atoi(dummy);
                else
                        *max_x = 24;
        }
}

void update_terminal(void)
{
        wnoutrefresh(w_stats);
        wnoutrefresh(w_slow);
        wnoutrefresh(w_fast);

        doupdate();
}

void create_windows(void)
{
	unsigned int nr = 0;

	if (w_stats)
	{
		delwin(w_stats);
		delwin(w_line1);
		delwin(w_slow);
		delwin(w_line2);
		delwin(w_fast);
	}

#ifdef FW
	fft_free();
	fft_init(max_x);
#endif

	if (max_x > history_n)
	{
		history = (double *)realloc(history, sizeof(double) * max_x);
		if (!history)
			error_exit("realloc issue");

		history_temp = (double *)realloc(history_temp, sizeof(double) * max_x);
		if (!history_temp)
			error_exit("realloc issue");

		/* halve of it is enough really */
		history_fft_magn = (double *)realloc(history_fft_magn, sizeof(double) * max_x);
		if (!history_fft_magn)
			error_exit("realloc issue");

		history_fft_phase = (double *)realloc(history_fft_phase, sizeof(double) * max_x);
		if (!history_fft_phase)
			error_exit("realloc issue");

		history_set = (char *)realloc(history_set, sizeof(char) * max_x);
		if (!history_set)
			error_exit("realloc issue");

		memset(&history[history_n], 0x00, (max_x - history_n) * sizeof(double));
		memset(&history_set[history_n], 0x00, (max_x - history_n) * sizeof(char));

		history_n = max_x;
	}

	if ((int)max_y > window_history_n)
	{
		slow_history = (char **)realloc(slow_history, sizeof(char *) * max_y);
		if (!slow_history)
			error_exit("realloc issue");

		fast_history = (char **)realloc(fast_history, sizeof(char *) * max_y);
		if (!fast_history)
			error_exit("realloc issue");

		memset(&slow_history[window_history_n], 0x00, (max_y - window_history_n) * sizeof(char *));
		memset(&fast_history[window_history_n], 0x00, (max_y - window_history_n) * sizeof(char *));

		window_history_n = max_y;
	}

	w_stats = newwin(stats_h, max_x,  0, 0);
	scrollok(w_stats, false);

	w_line1 = newwin(1, max_x, stats_h, 0);
	scrollok(w_line1, false);
	wnoutrefresh(w_line1);

	logs_n = max_y - (stats_h + 1 + 1);
	fast_n = logs_n * 11 / 20;
	slow_n = logs_n - fast_n;

	w_slow  = newwin(slow_n, max_x, (stats_h + 1), 0);
	scrollok(w_slow, true);

	w_line2 = newwin(1, max_x, (stats_h + 1) + slow_n, 0);
	scrollok(w_line2, false);
	wnoutrefresh(w_line2);

	w_fast  = newwin(fast_n, max_x, (stats_h + 1) + slow_n + 1, 0);
	scrollok(w_fast, true);

	wattron(w_line1, A_REVERSE);
	wattron(w_line2, A_REVERSE);
	for(nr=0; nr<max_x; nr++)
	{
		wprintw(w_line1, " ");
		wprintw(w_line2, " ");
	}
	wattroff(w_line2, A_REVERSE);
	wattroff(w_line1, A_REVERSE);

        wnoutrefresh(w_line1);
        wnoutrefresh(w_line2);

	doupdate();

	signal(SIGWINCH, handler);
}

void recreate_terminal(void)
{
	int index = 0;

        determine_terminal_size(&max_y, &max_x);

        resizeterm(max_y, max_x);

        endwin();
        refresh();

        create_windows();

	for(index = window_history_n - 1; index >= 0; index--)
	{
		if (slow_history[index])
			wprintw(w_slow, slow_history[index]);
		if (fast_history[index])
			wprintw(w_fast, fast_history[index]);
	}

	doupdate();

	win_resize = 0;
}

void init_ncurses_ui(double graph_limit_in, double hz_in)
{
	graph_limit = graph_limit_in;
	hz = hz_in;

        initscr();
        start_color();
        keypad(stdscr, TRUE);
        intrflush(stdscr, FALSE);
        noecho();
        //nonl();
        refresh();
        nodelay(stdscr, FALSE);
        meta(stdscr, TRUE);     /* enable 8-bit input */
        idlok(stdscr, TRUE);    /* may give a little clunky screenredraw */
        idcok(stdscr, TRUE);    /* may give a little clunky screenredraw */
        leaveok(stdscr, FALSE);

        init_pair(C_WHITE, COLOR_WHITE, COLOR_BLACK);
        init_pair(C_CYAN, COLOR_CYAN, COLOR_BLACK);
        init_pair(C_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(C_BLUE, COLOR_BLUE, COLOR_BLACK);
        init_pair(C_YELLOW, COLOR_YELLOW, COLOR_BLACK);
        init_pair(C_GREEN, COLOR_GREEN, COLOR_BLACK);
        init_pair(C_RED, COLOR_RED, COLOR_BLACK);

	kalman_init(0.0);

        recreate_terminal();
}

void end_ncurses(void)
{
	int index = 0;

	for(index=0; index<window_history_n; index++)
	{
		free(slow_history[index]);
		free(fast_history[index]);
	}

	free(slow_history);
	free(fast_history);

	if (w_stats)
	{
		delwin(w_stats);
		delwin(w_line1);
		delwin(w_slow);
		delwin(w_line2);
		delwin(w_fast);
	}

	endwin();

	free(history);
	free(history_set);

#ifdef FW
	fft_free();
	fft_stop();
#endif
	free(history_temp);
	free(history_fft_phase);
	free(history_fft_magn);
}

void fast_log(const char *fmt, ...)
{
	char buffer[4096] = { 0 };
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(buffer, sizeof buffer, fmt, ap);
        va_end(ap);

	free(fast_history[window_history_n - 1]);
	memmove(&fast_history[1], &fast_history[0], (window_history_n - 1) * sizeof(char *));
	fast_history[0] = strdup(buffer);

	wprintw(w_fast, buffer);

	if (win_resize)
		recreate_terminal();
}

void slow_log(const char *fmt, ...)
{
	char buffer[4096] = { 0 };
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(buffer, sizeof buffer, fmt, ap);
        va_end(ap);

	free(slow_history[window_history_n - 1]);
	memmove(&slow_history[1], &slow_history[0], (window_history_n - 1) * sizeof(char *));
	slow_history[0] = strdup(buffer);

	wprintw(w_slow, buffer);

	if (win_resize)
		recreate_terminal();
}

void my_beep(void)
{
	beep();
}

void status_line(char *fmt, ...)
{
        va_list ap;

	wattron(w_line2, A_REVERSE);

	wmove(w_line2, 0, 0);

        va_start(ap, fmt);
        vwprintw(w_line2, fmt, ap);
        va_end(ap);

	wattroff(w_line2, A_REVERSE);

	wnoutrefresh(w_line2);

	if (win_resize)
		recreate_terminal();
}

void draw_column(WINDOW *win, int x, int height, char overflow, char limitter)
{
	void *dummy = NULL;
	int y = 0, end_y = 0, win_h = 0, win_w = 0;

	getmaxyx(win, win_h, win_w);
	(void)win_w; /* silence warnings */

	end_y = max(0, win_h - height);

	for(y=win_h - 1; y >= end_y; y--)
		mvwchgat(win, y, x, 1, A_REVERSE, C_YELLOW, dummy);

	if (limitter)
		mvwchgat(win, 0, x, 1, A_REVERSE, C_BLUE, dummy);
	else if (overflow)
		mvwchgat(win, 0, x, 1, A_REVERSE, C_RED, dummy);
	else if (height == 0)
		mvwchgat(win, win_h - 1, x, 1, A_REVERSE, C_GREEN, dummy);
}

void draw_rad_column(WINDOW *win, int x, double val)
{
	void *dummy = NULL;
	int y = 0, end_y = 0, win_h = 0, win_w = 0;
	int center_y = 0;

	getmaxyx(win, win_h, win_w);
	(void)win_w; /* silence warnings */

	center_y = win_h / 2;
	end_y = (int)((double)(win_h / 2) * ((val / PI) + 1.0));

	if (end_y > center_y)
	{
		for(y=center_y; y<end_y; y++)
			mvwchgat(win, y, x, 1, A_REVERSE, C_YELLOW, dummy);
	}
	else
	{
		for(y=end_y; y<center_y; y++)
			mvwchgat(win, y, x, 1, A_REVERSE, C_YELLOW, dummy);
	}
}

double get_cur_scc()
{
        double scc_val = 0.0;
        double prev_val = 0.0, u0 = 0.0;
        double t[3] = { 0 };
        unsigned int loop = 0, n = 0;
	char first = 1;

        t[0] = t[1] = t[2] = 0.0;

        for(loop=0; loop<history_n; loop++)
        {
                double cur_val = history[loop];

		if (!history_set[loop])
			continue;

                if (first)
                {
                        prev_val = 0;
                        u0 = cur_val;
			first = 0;
                }
                else
		{
                        t[0] += prev_val * cur_val;
		}

                t[1] = t[1] + cur_val;
                t[2] = t[2] + (cur_val * cur_val);
                prev_val = cur_val;

		n++;
        }

        t[0] = t[0] + prev_val * u0;
        t[1] = t[1] * t[1];

        scc_val = (double)n * t[2] - t[1];

        if (scc_val == 0.0)
                return -1.0;

	return ((double)n * t[0] - t[1]) / scc_val;
}

#ifdef FW
void draw_fft(void)
{
	double mx_mag = 0.0;
	unsigned int index = 0, highest = 0;
	int cx = 0, cy = 0;
	/* double max_freq = hz / 2.0; */
	double highest_freq = 0, avg_freq_index = 0.0, total_val = 0.0, avg_freq = 0.0;
	unsigned int dummy = 0;

	getyx(w_slow, cy, cx);

	for(index=0; index<max_x; index++)
	{
		double val = 0.0;

		if (history_set[index])
			val = history[index];
		else
			val = index > 0 ? history[index - 1] : 0;

		if (val > graph_limit)
			val = graph_limit;

		history_temp[index] = val;
	}

	fft_do(history_temp, history_fft_magn, history_fft_phase);

	for(index=1; index<max_x/2; index++)
	{
		avg_freq_index += (double)index * history_fft_magn[index];
		total_val += history_fft_magn[index];

		if (history_fft_magn[index] > mx_mag)
		{
			mx_mag = history_fft_magn[index];
			highest = index;
		}
	}

	highest_freq = (hz / (double)max_x) * (double)highest;

	avg_freq_index /= total_val;
	avg_freq = (hz / (double)max_x) * avg_freq_index;

	wattron(w_line1, A_REVERSE);
	mvwprintw(w_line1, 0, 38, "highest: %6.2fHz, avg: %6.2fHz", highest_freq, avg_freq);
	wattroff(w_line1, A_REVERSE);
	wnoutrefresh(w_line1);

	dummy = max_x / 2 + 1;

	if (draw_phase)
	{
		int y = 0;

		for(y=0; y<slow_n; y++)
			mvwchgat(w_slow, y, dummy, 1, A_REVERSE, C_WHITE, NULL);

		for(index=0; index<(unsigned int)slow_n; index++)
			mvwchgat(w_slow, index, 0, max_x, A_NORMAL, C_WHITE, NULL);

		for(index=1; index<dummy - 1; index++)
		{
			draw_rad_column(w_slow, index - 1, history_fft_phase[index]);
		}
	}
	else
	{
		for(index=0; index<(unsigned int)slow_n; index++)
			mvwchgat(w_slow, index, max_x / 2, max_x / 2, A_NORMAL, C_WHITE, NULL);
	}

	for(index=1; index<dummy; index++)
	{
		int height_magn = (int)((double)slow_n * (history_fft_magn[index] / mx_mag));
		draw_column(w_slow, max_x / 2 + index - 1, height_magn, 0, 0);
	}

	wmove(w_slow, cy, cx);

	wnoutrefresh(w_slow);
}
#endif

double calc_trend()
{
	unsigned int half = history_n / 2, index = 0;
	double v1 = 0.0, v2 = 0.0;
	int n_v1 = 0, n_v2 = 0;

	for(index=0; index<half; index++)
	{
		if (!history_set[index])
			continue;

		v1 += history[index];
		n_v1++;
	}

	for(index=half; index<history_n; index++)
	{
		if (!history_set[index])
			continue;

		v2 += history[index];
		n_v2++;
	}

	if (n_v2 == 0 || n_v1 == 0)
		return 0;

	v1 /= (double)n_v1;
	v2 /= (double)n_v2;

	return (v1 - v2) / (v2 / 100.0);
}

void draw_graph(double val)
{
	int index = 0, loop_n = min(max_x, history_n), n = 0, n2 = 0;
	double avg = 0, sd = 0;
	double avg2 = 0, sd2 = 0;
	double mi = MY_DOUBLE_INF, ma = -MY_DOUBLE_INF, diff = 0.0;

	for(index=0; index<loop_n; index++)
	{
		double val = history[index];

		if (!history_set[index])
			continue;

		mi = min(val, mi);
		ma = max(val, ma);

		avg += val;
		sd += val * val;
		n++;
	}

	avg /= (double)n;
	sd = sqrt((sd / (double)n) - pow(avg, 2.0));

	mi = max(mi, avg - sd);
	ma = min(ma, avg + sd);

	for(index=0; index<loop_n; index++)
	{
		double val = history[index];

		if (!history_set[index])
			continue;

		if (val < mi || val > ma)
			continue;

		avg2 += val;
		sd2 += val * val;
		n2++;
	}

	avg2 /= (double)n2;
	sd2 = sqrt((sd2 / (double)n2) - pow(avg2, 2.0));

	mi = max(mi, avg2 - sd2);
	ma = min(ma, avg2 + sd2);
	diff = ma - mi;

	if (diff == 0.0)
		diff = 1.0;

	wattron(w_line1, A_REVERSE);
	mvwprintw(w_line1, 0, 0, "graph range: %7.2fms - %7.2fms    ", mi, ma);
	wattroff(w_line1, A_REVERSE);
	wnoutrefresh(w_line1);

	/* fprintf(stderr, "%d| %f %f %f %f\n", h_stats.n, mi, avg, ma, sd); */

	for(index=0; index<loop_n; index++)
	{
		char overflow = 0, limitter = 0;
		double val = 0, height = 0;
		int i_h = 0, x = max_x - (1 + index);

		if (!history_set[index])
		{
			mvwchgat(w_stats, stats_h - 1, x, 1, A_REVERSE, C_CYAN, NULL);
			continue;
		}

		if (history[index] < graph_limit)
			val = history[index];
		else
		{
			val = graph_limit;
			limitter = 1;
		}

		height = (val - mi) / diff;

		if (height > 1.0)
		{
			height = 1.0;
			overflow = 1;
		}

		i_h = (int)(height * stats_h);
		/* fprintf(stderr, "%d %f %f %d %d\n", index, history[index], height, i_h, overflow); */

		draw_column(w_stats, x, i_h, overflow, limitter);
	}
}

void update_stats(stats_t *resolve, stats_t *connect, stats_t *request, stats_t *total, stats_t *ssl_setup, int n_ok, int n_fail, const char *last_connect_str, const char *fp, char use_tfo, char dg, char use_ssl, stats_t *st_to, stats_t *tcp_rtt_stats, int re_tx, int pmtu, int tos)
{
	double k = 0.0;
	char force_redraw = 0;
	struct pollfd p = { 0, POLLIN, 0 };

	werase(w_stats);

	if (n_ok)
	{
		char buffer[4096] = { 0 };
		unsigned int buflen = 0;

		mvwprintw(w_stats, 0, 0, "         %6s %6s %6s %6s %6s", "cur", "min", "avg", "max", "sd");
		mvwprintw(w_stats, 1, 0, "resolve: %6.2f %6.2f %6.2f %6.2f %6.2f",
			resolve -> cur, resolve -> min, resolve -> avg / (double)resolve -> n, resolve -> max, calc_sd(resolve));
		mvwprintw(w_stats, 2, 0, "connect: %6.2f %6.2f %6.2f %6.2f %6.2f",
			connect -> cur, connect -> min, connect -> avg / (double)connect -> n, connect -> max, calc_sd(connect));
		if (use_ssl)
		{
			mvwprintw(w_stats, 3, 0, "ssl   : %6.2f %6.2f %6.2f %6.2f %6.2f",
				ssl_setup -> cur, ssl_setup -> min, ssl_setup -> avg / (double)ssl_setup -> n, ssl_setup -> max, calc_sd(ssl_setup));
		}
		else
		{
			mvwprintw(w_stats, 3, 0, "ssl   :");
		}

		mvwprintw(w_stats, 4, 0, "request: %6.2f %6.2f %6.2f %6.2f %6.2f",
			request -> cur, request -> min, request -> avg / (double)request -> n, request -> max, calc_sd(request));
		mvwprintw(w_stats, 5, 0, "total  : %6.2f %6.2f %6.2f %6.2f %6.2f",
			total -> cur, total -> min, total -> avg / (double)total -> n, total -> max, calc_sd(total));

		k = kalman_do(total -> cur);
		mvwprintw(w_stats, 6, 0, "ok: %3d, fail: %3d%s, scc: %.3f, kalman: %.3f", n_ok, n_fail, use_tfo ? ", with TFO" : "", get_cur_scc(), k);

		if (max_x >= 44 * 2 + 1)
		{
			double trend = calc_trend();
			char trend_dir = ' ';

			mvwprintw(w_stats, 0, 45, "         %6s %6s %6s %6s %6s", "cur", "min", "avg", "max", "sd");
			mvwprintw(w_stats, 1, 45, "t offst: %6.2f %6.2f %6.2f %6.2f %6.2f",
				st_to -> cur, st_to -> min, st_to -> avg / (double)st_to -> n, st_to -> max, calc_sd(st_to));

#if defined(linux) || defined(__FreeBSD__)
			mvwprintw(w_stats, 2, 45, "tcp rtt: %6.2f %6.2f %6.2f %6.2f %6.2f",
				tcp_rtt_stats -> cur, tcp_rtt_stats -> min, tcp_rtt_stats -> avg / (double)tcp_rtt_stats -> n, tcp_rtt_stats -> max, calc_sd(tcp_rtt_stats));
#endif

			if (trend < 0)
				trend_dir = '-';
			else if (trend > 0)
				trend_dir = '+';

			mvwprintw(w_stats, 7, 48, "trend: %c%.2f%%, re-tx: %2d, pmtu: %5d, TOS: %02x", trend_dir, fabs(trend), re_tx, pmtu, tos);
		}

		buflen = snprintf(buffer, sizeof buffer, "HTTP rc: %s, SSL fp: %s", last_connect_str, fp ? fp : "n/a");

		if (buflen <= max_x)
			mvwprintw(w_stats, 7, 0, "%s", buffer);
		else
		{
			static char prev_sf[48] = { 0 };

			mvwprintw(w_stats, 7, 0, "http result code: %s", last_connect_str);

			if (fp && strcmp(prev_sf, fp))
			{
				slow_log("\nSSL fingerprint: %s", fp);

				memcpy(prev_sf, fp, 47);
			}
		}
	}

	memmove(&history[1], &history[0], (history_n - 1) * sizeof(double));
	memmove(&history_set[1], &history_set[0], (history_n - 1) * sizeof(char));

	history[0]= total -> cur;
	history_set[0] = 1;

	if (poll(&p, 1, 0) == 1 && p.revents == POLLIN)
	{
		int c = getch();

		if (c == 12) /* ^L */
			force_redraw = 1;

		if (c == 'H')
			pause_graphs = !pause_graphs;

		if (c == 'q')
			stop = 1;
	}

	if (dg && !pause_graphs)
	{
		draw_graph(k);
#ifdef FW
		draw_fft();
#endif
	}

	wnoutrefresh(w_stats);

	if (win_resize || force_redraw)
		recreate_terminal();
}
