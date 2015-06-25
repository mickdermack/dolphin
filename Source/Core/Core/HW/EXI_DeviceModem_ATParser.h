// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"

class IATHandler
{
public:
	virtual void HandleATCommand(const std::string& command, const std::string& arg, std::string* error_status) = 0;

	virtual void HandleDial(const std::string& dial_string, std::string* error_status) = 0;

	virtual void HandleExtendedCommand(const std::string& command, const std::vector<std::string>& args, std::string* error_status) = 0;
	virtual void QueryExtendedCommand(const std::string& command, std::string* error_status) = 0;
	virtual void GetExtendedParameter(const std::string& command, std::string* error_status) = 0;

	virtual void SetSParameter(u16 param, u16 value, std::string* error_status) = 0;
	virtual void GetSParameter(u16 param, std::string* error_status) = 0;
	virtual void ResetSParameter(u16 param, std::string* error_status) = 0;
};

class parser_error : std::exception
{
public:
	parser_error(std::string what)
		: m_what(what)
	{
	}

	const char* what() const
	{
		return m_what.c_str();
	}

private:
	std::string m_what;
};

class CATParser
{
public:
	CATParser(IATHandler* handler);
	std::string HandleLine(const std::string& line);

private:
	IATHandler* m_handler;

	void ParseCommand(std::string::const_iterator& iter, const std::string::const_iterator& end, std::string* error_status);
	void ParseExtendedCommand(std::string::const_iterator& iter, const std::string::const_iterator& end, std::string* error_status);
	void ParseSParameter(std::string::const_iterator& iter, const std::string::const_iterator& end, std::string* error_status);
	void ParseDial(std::string::const_iterator& iter, const std::string::const_iterator& end, std::string* error_status);

	void ParseStringConstant(std::string::const_iterator& iter, const std::string::const_iterator& end, std::string* result);

	void PreprocessString(std::string* str);

	inline static bool IsAlpha(char c)
	{
		return c >= 'A' && c <= 'Z';
	}

	inline static bool IsNumeric(char c)
	{
		return c >= '0' && c <= '9';
	}

	inline static bool IsHex(char c)
	{
		return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
	}

	// Checks if a character is valid in the name of an extended syntax command
	inline static bool IsExtChar(char c)
	{
		return IsAlpha(c) || IsNumeric(c) || c == '!' || c == '%' || c == '-'
			|| c == '.' || c == '/' || c == ':' || c == '_';
	}

	inline static bool IsDialChar(char c)
	{
		return IsNumeric(c) || c == '*' || c == '#' || c == '+' || c == 'A' || c == 'B' || c == 'C' || c == 'D'
			|| c == 'P' || c == 'T' || c == '!' || c == 'W' || c == '@';
	}

	inline static char Upper(char c)
	{
		if (c >= 'a' && c <= 'z')
		{
			return c - ('a' - 'A');
		}
		
		return c;
	}

	inline static bool IsControlChar(char c)
	{
		return c < 0x20;
	}

	inline static u8 HexCharToInt(char c)
	{
		return (c <= '9') ? (c - '0') : (10 + (c - 'a'));
	}

	inline static u8 DecCharToInt(char c)
	{
		return c - '0';
	}

	inline static void SkipSpaces(std::string::const_iterator& iter, const std::string::const_iterator& end)
	{
		while (iter != end && *iter == ' ')
		{
			++iter;
		}
	}
};
