#pragma once

#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif //_WIN32

inline std::ostream& blue(std::ostream &s)
{
#ifdef _WIN32
	HANDLE hStdout=GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hStdout, FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_INTENSITY);
#endif //_WIN32
	return s;
}

inline std::ostream& red(std::ostream &s)
{
#ifdef _WIN32
	HANDLE hStdout=GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hStdout, FOREGROUND_RED|FOREGROUND_INTENSITY);
#endif //_WIN32
	return s;
}

inline std::ostream& green(std::ostream &s)
{
#ifdef _WIN32
	HANDLE hStdout=GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hStdout, FOREGROUND_GREEN|FOREGROUND_INTENSITY);
#endif //_WIN32
	return s;
}

inline std::ostream& yellow(std::ostream &s)
{
#ifdef _WIN32
	HANDLE hStdout=GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hStdout, FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY);
#endif //_WIN32
	return s;
}

inline std::ostream& white(std::ostream &s)
{
#ifdef _WIN32
	HANDLE hStdout=GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hStdout, FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE);
#endif //_WIN32
	return s;
}

struct color {
#ifdef _WIN32
	color(WORD attribute) :m_color(attribute) {};
	WORD m_color;
#endif //_WIN32
};

template <class _Elem, class _Traits>
std::basic_ostream<_Elem, _Traits>&
operator<<(std::basic_ostream<_Elem, _Traits>& i, color& c)
{
#ifdef _WIN32
	HANDLE hStdout=GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hStdout, c.m_color);
#endif //_WIN32
	return i;
}