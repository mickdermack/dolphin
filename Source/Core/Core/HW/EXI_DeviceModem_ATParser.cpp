// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <algorithm>

#include "Core/HW/EXI_DeviceModem_ATParser.h"

CATParser::CATParser(IATHandler* handler)
	: m_handler(handler)
{
}

std::string CATParser::HandleLine(std::string line)
{
	INFO_LOG(SP1, "ATParser: Parsing \"%s\"", line.c_str());

	PreprocessString(&line);

	std::string::iterator iter = line.begin();
	std::string::iterator end = line.end();

	std::string result = "OK";

	try
	{
		while (iter < end)
		{
			std::string error_status;

			// Iterator is passed as reference and it can and will be modified in the called functions.
			// End is const, so it should stay the same.

			if (*iter == '+')
			{
				ParseExtendedCommand(iter, end, &error_status);
			}
			else if (*iter == 'S')
			{
				ParseSParameter(iter, end, &error_status);
			}
			else if (*iter == 'D')
			{
				ParseDial(iter, end, &error_status);
				result = "";
			}
			else
			{
				ParseCommand(iter, end, &error_status);
			}

			if (!error_status.empty())
			{
				return error_status;
			}

			// Don't increment the iterator as the functions set it to the char after
		}
	}
	catch (parser_error& pe)
	{
		INFO_LOG(SP1, "ATParser: %s in input \"%s\" at character %d", pe.what(), line, iter - line.begin() + 1);
		result = "ERROR";
	}

	return result;
}

void CATParser::ParseCommand(std::string::const_iterator& iter, const std::string::const_iterator& end, std::string* error_status)
{
	std::string cmd;
	std::string arg;

	if (*iter == '\\' || *iter == '&')
	{
		cmd.push_back(*iter);
		iter++;
	}

	if (iter != end && IsAlpha(*iter))
	{
		cmd.push_back(*iter);
		iter++;
	}
	else
	{
		throw parser_error("Expected alphabetical character for command");
	}

	bool skippedZero = false;
	while (iter != end && IsNumeric(*iter))
	{
		// Ignore leading zeroes as per V.250 5.3.1
		if (*iter != '0')
		{
			arg.push_back(*iter);
		}
		else
		{
			skippedZero = true;
		}

		++iter;
	}

	// Would be embarrassing if we ignored an argument that only consists of zeroes
	if (arg.empty() && skippedZero)
	{
		arg = "0";
	}

	m_handler->HandleATCommand(cmd, arg, error_status);
}

void CATParser::ParseExtendedCommand(std::string::const_iterator& iter, const std::string::const_iterator& end, std::string* error_status)
{
	std::string cmd;
	std::vector<std::string> args;

	// Skip over plus sign
	iter++;

	SkipSpaces(iter, end);

	if (iter != end && IsAlpha(*iter))
	{
		cmd.push_back(*iter);
		iter++;
	}
	else
	{
		throw parser_error("Expected alphabetical character for first char of extended cmd");
	}

	SkipSpaces(iter, end);

	while (iter != end)
	{
		if (IsExtChar(*iter))
		{
			cmd.push_back(*iter);
		}
		else if (*iter == '?')
		{
			m_handler->GetExtendedParameter(cmd, error_status);
			iter++;
			return;
		}
		else if (*iter == '=')
		{
			// Arguments follow
			break;
		}
		else
		{
			throw parser_error("Expected character valid for extended syntax command");
		}

		// *iter can't be space, because space is neither an ExtChar, nor a '?', nor a '='
		iter++;

		// SkipSpaces won't go past end
		SkipSpaces(iter, end);
	}

	if (iter == end)
	{
		// If the command name is the only thing here, execute with zero args
		m_handler->HandleExtendedCommand(cmd, args, error_status);
		return;
	}

	if (*iter == '=')
	{
		iter++;
	}

	if (iter == end)
	{
		// If the the line ends after the = sign, execute with one empty arg
		args.push_back("");
		m_handler->HandleExtendedCommand(cmd, args, error_status);
		return;
	}

	if (*iter == '?')
	{
		m_handler->QueryExtendedCommand(cmd, error_status);
		++iter;

		SkipSpaces(iter, end);

		if (iter != end)
		{
			if (*iter == ';')
			{
				iter++;
			}
			else
			{
				throw parser_error("Expected semicolon after querying extended command");
			}
		}
		return;
	}

	while (iter != end && *iter != ';')
	{
		std::string arg;

		SkipSpaces(iter, end);

		if (iter == end)
		{
			args.push_back(std::move(arg));
			break;
		}

		if (*iter == '"')
		{
			++iter;
			ParseStringConstant(iter, end, &arg);

			SkipSpaces(iter, end);

			if (iter != end && *iter != ',' && *iter != ';')
			{
				throw parser_error("Expected end of argument after end of string constant");
			}
		}
		else
		{
			while (iter != end && *iter != ',')
			{
				if (*iter != ' ')
				{
					arg.push_back(*iter);
				}
				iter++;
			}

			if (iter != end && *iter != ';')
			{
				iter++;
			}
		}

		args.push_back(std::move(arg));
	}

	if (iter != end && *iter == ';')
	{
		iter++;
	}

	// It is impossible for the parser to determine whether an extended syntax command is to be
	// executed (V.250 5.4.3.1) or an extended syntax parameter is to be set (V.250 5.4.4.2).
	// The handler has to decide that.
	m_handler->HandleExtendedCommand(cmd, args, error_status);
}

void CATParser::ParseStringConstant(std::string::const_iterator& iter, const std::string::const_iterator& end, std::string* result)
{
	std::string::const_iterator start = iter;

	// The remaining characters in an escape sequence
	u8 remaining_esc = 0;
	u8 escape_code = 0;

	while (iter != end)
	{
		if (*iter == '"')
		{
			break;
		}

		if (*iter == '\\')
		{
			remaining_esc = 2;
			iter++;
			continue;
		}

		if (remaining_esc == 0)
		{
			result->push_back(*iter);
		}
		else
		{
			if (!IsHex(*iter))
			{
				throw parser_error("Invalid character in escape sequence");
			}

			escape_code = escape_code * 16 + HexCharToInt(*iter);
			remaining_esc--;

			if (remaining_esc == 0)
			{
				result->push_back(escape_code);
			}
		}

		iter++;
	}

	if (remaining_esc > 0)
	{
		throw parser_error("Unexpected end of string in escape sequence");
	}

	// If the iterator reached the end, there has been no closing quote.
	if (iter == end)
	{
		// Let the error message say the start of the string
		iter = start;
		throw parser_error("Unterminated string constant");
	}
}

void CATParser::ParseSParameter(std::string::const_iterator& iter, const std::string::const_iterator& end, std::string* error_status)
{

	u16 param = 0;
	u16 value = 0;

	// Skip S
	iter++;

	SkipSpaces(iter, end);

	while (iter != end && IsNumeric(*iter))
	{
		param = param * 10 + DecCharToInt(*iter);

		if (param > 99)
		{
			throw parser_error("Too high S-parameter");
		}
		
		iter++;
	}

	if (iter == end)
	{
		throw parser_error("Unexpected end of line in S-parameter");
	}

	if (*iter == '?')
	{
		m_handler->GetSParameter(param, error_status);
		return;
	}

	if (*iter != '=')
	{
		throw parser_error("Unexpected character in S-parameter");
	}

	// Skip over equals sign
	iter++;

	if (iter == end)
	{
		// Implementation should decide what happens with ex. "ATS3=".
		// V.250 5.3.2 says that 0 should be assumed or an error should be caused.
		// My Sony Ericsson W200i resets the parameter to default though.
		// No idea what the actual modem adapter would do, but resetting seems sensible.
		m_handler->ResetSParameter(param, error_status);
		return;
	}

	while (iter != end && IsNumeric(*iter))
	{
		value = value * 10 + DecCharToInt(*iter);

		if (value > 999)
		{
			throw parser_error("Too high S-parameter value");
		}

		iter++;
	}

	m_handler->SetSParameter(param, value, error_status);
}

void CATParser::ParseDial(std::string::const_iterator& iter, const std::string::const_iterator& end, std::string* error_status)
{
	std::string dial_string;

	// Skip over 'D'
	iter++;

	while (iter != end)
	{
		if (*iter == ';')
		{
			throw parser_error("Semicolon after dial string not supported");
		}

		dial_string.push_back(*iter);

		iter++;
	}

	m_handler->HandleDial(dial_string, error_status);
}

void CATParser::PreprocessString(std::string* str)
{
	// Remove all control characters from command line as per V.250 5.2.2
	str->erase(std::remove_if(str->begin(), str->end(), &IsControlChar), str->end());

	// Transform all lower case letters to upper case ones as per V.250 5.1
	std::transform(str->begin(), str->end(), str->begin(), &Upper);
}
