// :vim ts=4 sw=4 noet:
/*
==================================================================================
	Copyright (c) 2020 AT&T Intellectual Property.

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
	Mnemonic:	stub_snapshot.h
	Abstract:	Provides stub functions of the snapshot module to build RFT tests

	Date:		11 September 2020
	Author:		Alexandre Huff
*/

/*
	This is the callback function the RFT calls in the xApp code
*/
size_t take_snapshot( char **contexts, int nctx, unsigned int *items, unsigned char **data ) {
	return 0;
}
