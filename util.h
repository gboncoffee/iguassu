/* MIT/X Consortium License

© 2006-2019 Anselm R Garbe <anselm@garbe.ca>
© 2006-2008 Sander van Dijk <a.h.vandijk@gmail.com>
© 2006-2007 Michał Janeczek <janeczek@gmail.com>
© 2007 Kris Maglione <jg@suckless.org>
© 2009 Gottox <gottox@s01.de>
© 2009 Markus Schnalke <meillo@marmaro.de>
© 2009 Evan Gates <evan.gates@gmail.com>
© 2010-2012 Connor Lane Smith <cls@lubutu.com>
© 2014-2019 Hiltjo Posthuma <hiltjo@codemadness.org>
© 2015-2019 Quentin Rameau <quinq@fifth.space>

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE. */

#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define BETWEEN(X, A, B)        ((A) <= (X) && (X) <= (B))

void die(const char *fmt, ...);
void *ecalloc(size_t nmemb, size_t size);
