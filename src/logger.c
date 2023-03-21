// :vim ts=4 sw=4 noet:
/*
==================================================================================
	Copyright (c) 2019-2020 AT&T Intellectual Property.
	Copyright (c) 2019-2020 Alexandre Huff.

	Licensed under the Apache License, Version 2.0 (the "License");
	you may not use this file except in compliance with the License.
	You may obtain a copy of the License at

		http://www.apache.org/licenses/LICENSE-2.0

	Unless required by applicable law or agreed to in writing, software
	distributed under the License is distributed on an "AS IS" BASIS,
	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
	See the License for the specific language governing permissions and
	limitations under the License.
==================================================================================
*/

/*
	Mnemonic:	logger.c
	Abstract:	Implementation of a generic logging to stderr

	Date:		31 December 2019
	Author:		Alexandre Huff
*/

/* This file is based on the following implementation */

/*
 * Copyright (c) 2017 rxi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Source: https://github.com/rxi/log.c
 */

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

#include "logger.h"


static const char *level_names[] = {
	"NONE", "FATAL", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"
};

#ifdef LOGGER_USE_COLOR
static const char *level_colors[] = {
	"\x1b[0m", "\x1b[35m", "\x1b[31m", "\x1b[33m", "\x1b[32m", "\x1b[36m", "\x1b[94m"
};
#endif


void logger_log(int level, const char *file, int line, const char *fmt, ...) {
	va_list args;
	char buf[16];
	char bufcat[32];
	struct timespec now;
	char *filename;

	filename = strrchr( file, '/' );
	if( filename != NULL )
		filename++;
	else
		filename = (char *) file;

	/* Acquire locking to ensure that all information of a log line is atomically written */
	flockfile( stderr );

	/* Get current time */
	timespec_get( &now, TIME_UTC );
	struct tm *lt = localtime( &now.tv_sec );

	/* Logging to stderr */
	buf[strftime(buf, sizeof(buf), "%H:%M:%S", lt)] = '\0';
	snprintf( bufcat, 32, "%s.%03ld", buf, now.tv_nsec / 1000000 );
#ifdef LOGGER_USE_COLOR
	fprintf(
	stderr, "%s %s%-5s\x1b[0m \x1b[90m%s:%d\x1b[0m ",
	bufcat, level_colors[level], level_names[level], filename, line);
#else
	fprintf(stderr, "%s %-5s %s:%d: ", bufcat, level_names[level], filename, line);
#endif
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fflush(stderr);

	/* Release lock */
	funlockfile( stderr );
}
