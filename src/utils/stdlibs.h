/*
	bumo is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	bumo is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with bumo.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef UTILS_STDLIBS_UTIL_H_
#define UTILS_STDLIBS_UTIL_H_
#include<cstdlib>

#ifdef OS_ANDROID
#define FMT_I64 "%lld"
#define FMT_I64_EX(fmt) "%" #fmt "lld"
#define FMT_U64 "%llu"
#define FMT_X64 "%llX"
#define FMT_SIZE "%u"
#endif
namespace utils {
	static int Atoi(const std::string &str);
	static long long Atoll(const std::string &str);
	static long  Atol(const std::string &str);
	static double Atof(const std::string &str);
	static void Abort(void);
}
#endif