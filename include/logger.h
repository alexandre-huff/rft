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
	Mnemonic:	logger.h
	Abstract:	Header file for logging to stderr

	Date:		31 December 2019
	Author:		Alexandre Huff
*/

/* This file is based on the following implementations */

/**
 * Copyright (c) 2017 rxi
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `log.c` for details.
 *
 * Source: https://github.com/rxi/log.c
 */

/*
 * Copyright (c) 2012 David Rodrigues
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Source: https://github.com/dmcrodrigues/macro-logger
 */

#ifndef _LOGGER_H
#define _LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#define LOGGER_USE_COLOR		// comment if too fancy, or compile with -DLOGGER_USE_COLOR

#define LOGGER_VERSION "0.2.1"

#define LOGGER_PADDING 20

/* logger levels */
#define LOGGER_NONE		0
#define LOGGER_FATAL	1
#define LOGGER_ERROR	2
#define LOGGER_WARN		3
#define LOGGER_INFO		4
#define LOGGER_DEBUG	5
#define LOGGER_TRACE	6

#ifndef LOGGER_LEVEL	// can be passed in compile time with -DLOGGER_LEVEL=number or e.g. LOGGER_INFO
#define LOGGER_LEVEL	LOGGER_INFO
#endif

#if LOGGER_LEVEL >= LOGGER_TRACE
#define logger_trace(message, args...) logger_log(LOGGER_TRACE, __FILE__, __LINE__, message "\n", ## args)
#else
#define logger_trace(message, args...)
#endif

#if LOGGER_LEVEL >= LOGGER_DEBUG
#define logger_debug(message, args...) logger_log(LOGGER_DEBUG, __FILE__, __LINE__, message "\n", ## args)
#else
#define logger_debug(message, args...)
#endif

#if LOGGER_LEVEL >= LOGGER_INFO
#define logger_info(message, args...) logger_log(LOGGER_INFO, __FILE__, __LINE__, message "\n", ## args)
#else
#define logger_info(message, args...)
#endif

#if LOGGER_LEVEL >= LOGGER_WARN
#define logger_warn(message, args...) logger_log(LOGGER_WARN, __FILE__, __LINE__, message "\n", ## args)
#else
#define logger_warn(message, args...)
#endif

#if LOGGER_LEVEL >= LOGGER_ERROR
#define logger_error(message, args...) logger_log(LOGGER_ERROR, __FILE__, __LINE__, message "\n", ## args)
#else
#define logger_error(message, args...)
#endif

#if LOGGER_LEVEL >= LOGGER_FATAL
#define logger_fatal(message, args...) logger_log(LOGGER_FATAL, __FILE__, __LINE__, message "\n", ## args)
#else
#define logger_fatal(message, args...)
#endif

#define logger_force(level, message, args...) logger_log(level, __FILE__, __LINE__, message "\n", ## args)

void logger_log(int level, const char *file, int line, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
