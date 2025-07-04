/*
 * hexdiff - hexadecimal differencing program
 *
 * Copyright 2018 Austin Roach <ahroach@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>


// ANSI escape sequences
static const char ansi_green[] = "\x1B""[32m";
static const char ansi_red[] = "\x1B""[31m";
static const char ansi_reset[] = "\x1B""[0m";
static const char empty_str[] = "";


static int sigint_recv = 0;

static void sigint_handler(int signum)
{
	sigint_recv = 1;
}


static void show_help(char **argv, int verbose)
{
	fprintf(stderr,
	        "Usage: %s [-ah] [-n len] file1 file2 [skip1 [skip2]]\n",
	        argv[0]);
	if (verbose) {
		printf(" -a      print all lines\n"
		       " -h      show help\n"
		       " -n len  maximum number of bytes to compare\n"
		       " skip1   starting offset for file1\n"
		       " skip2   starting offset for file2\n");
	}
	exit(EXIT_FAILURE);
}


static void printicize(uint8_t * buf)
{
	// Convert non-ASCII printable values to '.'
	for (int i = 0; i < 8; i++) {
		if ((buf[i] < 0x20) || (buf[i] > 0x7e)) {
			buf[i] = '.';
		}
	}
}


static void print_same(uint8_t *buf1, uint8_t *buf2,
                       unsigned long long int skip1,
                       unsigned long long int skip2,
		       unsigned long long int cnt)
{
	// Print the left side
	printf("%s0x%010llx  "
	       "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx ",
	       ansi_reset, skip1 + cnt, buf1[0], buf1[1], buf1[2], buf1[3],
	       buf1[4], buf1[5], buf1[6], buf1[7]);
	printicize(buf1);
	printf("%c%c%c%c%c%c%c%c    ", buf1[0], buf1[1], buf1[2], buf1[3],
	       buf1[4], buf1[5], buf1[6], buf1[7]);

	// Print the right side
	printf("0x%010llx  "
	       "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx ",
	       skip2 + cnt, buf2[0], buf2[1], buf2[2], buf2[3], buf2[4],
	       buf2[5], buf2[6], buf2[7]);
	printicize(buf2);
	printf("%c%c%c%c%c%c%c%c\n", buf2[0], buf2[1], buf2[2], buf2[3],
	       buf2[4], buf2[5], buf2[6], buf2[7]);
}


static void print_diff(uint8_t *buf1, uint8_t *buf2,
                       unsigned long long int skip1,
                       unsigned long long int skip2,
		       unsigned long long int cnt)
{
	const char *color[8];
	const char *color_last;

	// Assign escape sequences as appropriate for each byte
	for (int i = 0; i < 8; i++) {
		color[i] = buf1[i] == buf2[i] ? ansi_green : ansi_red;
	}

	// Remove many redundant escape sequences
	color_last = color[0];

	if ((color[0] == ansi_red) && (color[7] == ansi_red)) {
		// Beginning of each section is preceded by the address
		// (always red), or by the last element of a preceding
		// section. As long as the beginning and ending elements are
		// both red, we can get rid of the escape sequence at the
		// beginning of the section.
		color[0] = empty_str;
	}

	for (int i = 1; i < 8; i++) {
		if (color[i] == color_last) {
			color[i] = empty_str;
		} else {
			color_last = color[i];
		}
	}

	// Print the left side
	printf("%s0x%010llx  "
	       "%s%02hhx%s%02hhx%s%02hhx%s%02hhx"
	       "%s%02hhx%s%02hhx%s%02hhx%s%02hhx ",
	       ansi_red, skip1 + cnt, color[0], buf1[0], color[1], buf1[1],
	       color[2], buf1[2], color[3], buf1[3], color[4], buf1[4],
	       color[5], buf1[5], color[6], buf1[6], color[7], buf1[7]);
	printicize(buf1);
	printf("%s%c%s%c%s%c%s%c%s%c%s%c%s%c%s%c    ", color[0], buf1[0],
	       color[1], buf1[1], color[2], buf1[2], color[3], buf1[3],
	       color[4], buf1[4], color[5], buf1[5], color[6], buf1[6],
	       color[7], buf1[7]);

	// Print the right side
	printf("%s0x%010llx  "
	       "%s%02hhx%s%02hhx%s%02hhx%s%02hhx"
	       "%s%02hhx%s%02hhx%s%02hhx%s%02hhx ",
	       ansi_red, skip2 + cnt, color[0], buf2[0], color[1], buf2[1],
	       color[2], buf2[2], color[3], buf2[3], color[4], buf2[4],
	       color[5], buf2[5], color[6], buf2[6], color[7], buf2[7]);
	printicize(buf2);
	printf("%s%c%s%c%s%c%s%c%s%c%s%c%s%c%s%c\n", color[0], buf2[0],
	       color[1], buf2[1], color[2], buf2[2], color[3], buf2[3],
	       color[4], buf2[4], color[5], buf2[5], color[6], buf2[6],
	       color[7], buf2[7]);
	printf("%s", ansi_reset);
}


int main(int argc, char **argv)
{
	int opt, show_all, input_end;
	unsigned long long int max_len, skip1, skip2, cnt, eq_run;
	char *fname1, *fname2;
	FILE *file1, *file2;
	// struct sigaction sigint_action;
	uint8_t buf1[8], buf2[8];

#if defined(_WIN32) || defined(_WIN64)
	signal(SIGINT, sigint_handler);
#else
	struct sigaction sigint_action;
	sigint_action.sa_handler = sigint_handler;
	sigemptyset(&sigint_action.sa_mask);
	sigint_action.sa_flags = 0;
	sigaction(SIGINT, &sigint_action, NULL);
#endif

	// Parse the input arguments
	show_all = 0;
	max_len = 0;
	while ((opt = getopt(argc, argv, "ahn:")) != -1) {
		switch (opt) {
		case 'a':
			show_all = 1;
			break;
		case 'h':
			show_help(argv, 1);
		case 'n':
			max_len = strtoull(optarg, NULL, 0);
			break;
		default:
			show_help(argv, 0);
		}
	}

	// Get the filenames and any skip values
	if ((argc - optind) < 2) show_help(argv, 0);
	fname1 = argv[optind++];
	fname2 = argv[optind++];
	skip1 = (optind < argc) ? strtoull(argv[optind++], NULL, 0) : 0;
	skip2 = (optind < argc) ? strtoull(argv[optind++], NULL, 0) : 0;
	if (optind < argc) show_help(argv, 0); //Leftover arguments

	// Open the files and seek to the appropriate spots
	if ((file1 = fopen(fname1, "r")) == NULL) {
		fprintf(stderr, "fopen: %s: %s\n", fname1, strerror(errno));
		exit(EXIT_FAILURE);
	}
	if ((file2 = fopen(fname2, "r")) == NULL) {
		fprintf(stderr, "fopen: %s: %s\n", fname2, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (fseeko(file1, skip1, SEEK_SET) != 0) {
		fprintf(stderr,
		        "fseek to 0x%llx in %s: %s\n", skip1, fname1,
		        strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (fseeko(file2, skip2, SEEK_SET) != 0) {
		fprintf(stderr,
		        "fseek to 0x%llx in %s: %s\n", skip2, fname2,
		        strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Set up signal handler for SIGINT
	// sigint_action.sa_handler = sigint_handler;
	// sigaction(SIGINT, &sigint_action, NULL);

	// Begin printing output
	printf("%s   offset      0 1 2 3 4 5 6 7 01234567    "
	       "   offset      0 1 2 3 4 5 6 7 01234567\n",
	       ansi_reset);
	
	input_end = 0;
	cnt = 0;
	eq_run = 0;
	while ((input_end == 0) && ((cnt < max_len) || (max_len == 0)) &&
	       (sigint_recv == 0)) {
		// If we fail to fill the buffer due to EOF, we want
		// the residual values to be 0
		memset(buf1, 0, 8);
		memset(buf2, 0, 8);

		// TODO: Probably less I/O overhead if we read more than
		// 8 bytes at a time. Or, threads...
		if (fread(buf1, 1, 8, file1) != 8) {
			input_end = 1;
		}
		if (fread(buf2, 1, 8, file2) != 8) {
			input_end = 1;
		}
		
		if (memcmp(buf1, buf2, 8) == 0) {
			if ((eq_run == 0) || (show_all == 1)) {
				print_same(buf1, buf2, skip1, skip2, cnt);	
			} else if (eq_run == 1) {
				printf("...\n");
			}
			eq_run++;
		} else {
			print_diff(buf1, buf2, skip1, skip2, cnt);
			eq_run = 0;
		}
	
		cnt += 8;
	}

	fclose(file1);
	fclose(file2);

	return 0;
}
