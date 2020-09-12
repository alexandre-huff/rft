// :vim ts=4 sw=4 noet:
/*
==================================================================================
	Copyright (c) 2020 AT&T Intellectual Property.
	Copyright (c) 2020 Alexandre Huff.

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
	Mnemonic:	stub_pthread.h
	Abstract:	Provides stub functions of the system's pthread module to build RFT tests

	Date:		7 September 2020
	Author:		Alexandre Huff
*/

int pthread_create(pthread_t *__restrict__ __newthread, const pthread_attr_t *__restrict__ __attr,
					void *(*__start_routine)(void *), void *__restrict__ __arg) {
	return 0;
}
