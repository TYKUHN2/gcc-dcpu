// { dg-require-namedlocale "" }

// Copyright (C) 2004, 2005, 2006, 2007, 2008 Free Software Foundation
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

#include <iostream>
#include <locale>
#include <cstdio>
#include <testsuite_hooks.h>

// libstdc++/12077
void test01()
{
  using namespace std;
  bool test __attribute__((unused)) = true;

  const char* name = "tmp_12077";

  locale loc = locale("is_IS.UTF-8");
  locale::global(loc);
  wcin.imbue(loc);

  const char* str =
    "\x1\x2\x3\x4\x5\x6\x7\x8\x9\xa\xb\xc\xd\xe\xf\x10\x11\x12\x13"
    "\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f\x20!\"#$%&"
    "'()*+,-./0123456789:;<=>?@}~\x7f\xc2\x80\xc2\x81\xc2\x82\xc2"
    "\x83\xc2\x84\xc2\x85\xc2\x86\xc2\x87\xc2\x88\xc2\x89\xc2\x8a"
    "\xc2\x8b\xc2\x8c\xc2\x8d\xc2\x8e\xc2\x8f\xc2\x90\xc2\x91\xc2"
    "\x92\xc2\x93\xc2\x94\xc2\x95\xc2\x96\xc2\x97\xc2\x98\xc2\x99"
    "\xc2\x9a\xc2\x9b\xc2\x9c\xc3\xba\xc3\xbb\xc3\xbc\xc3\xbd\xc3"
    "\xbe\xc3\xbf\xc4\x80\xc4\x81\xc4\x82\xc4\x83\xc4\x84\xc4\x85"
    "\xc4\x86\xc4\x87\xc4\x88\xc4\x89\xc4\x8a\xc4\x8b\xc4\x8c\xc4"
    "\x8d\xc4\x8e\xc4\x8f\xc4\x90\xc4\x91\xc4\x92\xc4\x93\xc4\x94"
    "\xc4\x95\xc4\x96\xc4\x97\xc4\x98\xc4\x99\xdf\xb8\xdf\xb9\xdf"
    "\xba\xdf\xbb\xdf\xbc\xdf\xbd\xdf\xbe\xdf\xbf\xe0\xa0\x80\xe0"
    "\xa0\x81\xe0\xa0\x82\xe0\xa0\x83\xe0\xa0\x84\xe0\xa0\x85\xe0"
    "\xa0\x86\xe0\xa0\x87\xe0\xa0\x88\xe0\xa0\x89\xe0\xa0\x8a\xe0"
    "\xa0\x8b\xe0\xa0\x8c\xe0\xa0\x8d\xe0\xa0\x8e\xe0\xa0\x8f\xe0"
    "\xa0\x90\xe0\xa0\x91\xe0\xa0\x92\xe0\xa0\x93\xe0\xa0\x94\xe0"
    "\xa0\x95\xe0\xa0\x96\xe0\xa0\x97\x1\x2\x4\x8\x10\x20@\xc2\x80"
    "\xc4\x80\xc8\x80\xd0\x80\xe0\xa0\x80\xe1\x80\x80\xe2\x80\x80"
    "\xe4\x80\x80\xe8\x80\x80\xf0\x90\x80\x80\xf0\xa0\x80\x80\xf1"
    "\x80\x80\x80\xf2\x80\x80\x80\xf4\x80\x80\x80\xf8\x88\x80\x80"
    "\x80\xf8\x90\x80\x80\x80\xf8\xa0\x80\x80\x80\xf9\x80\x80\x80"
    "\x80\xfa\x80\x80\x80\x80\xfc\x84\x80\x80\x80\x80\xfc\x88\x80"
    "\x80\x80\x80\xfc\x90\x80\x80\x80\x80\xfc\xa0\x80\x80\x80\x80"
    "\xfd\x80\x80\x80\x80\x80";

  FILE* file = fopen(name, "w");
  fputs(str, file);
  fclose(file);
  
  freopen(name, "r", stdin);
  
  streamsize n = wcin.rdbuf()->in_avail();
  while (n--)
    {
      wint_t c = wcin.rdbuf()->sbumpc();
      VERIFY( c != WEOF );
    }
}

int main()
{
  test01();
  return 0;
}
