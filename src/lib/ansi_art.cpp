/**
 * Copyright (C) 2014 syndicode
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 **/

#include "stdafx.h"
#include "ansi_art.h"
#include "util.h"

using std::wstring;


CAnsiArt::CAnsiArt(size_t a_widthLimit, size_t a_heightLimit, size_t a_hintWidth, size_t a_hintHeight) :
	m_widthLimit(a_widthLimit),
	m_heightLimit(a_heightLimit),
	m_hintWidth(a_hintWidth),
	m_hintHeight(a_hintHeight),
	m_commands(),
	m_lines(),
	m_maxLineLength(),
	m_colorMap()
{
}


bool CAnsiArt::Parse(const wstring& a_text)
{
	m_commands.clear();

	enum class PS {
		PARSERERROR = -1,
		ANYTEXT = 1,
		ESC_CHAR,
		ESC_BRACKET,
		ESC_DATA,
		ESC_COMMAND,
	};

	PS parser_state = PS::ANYTEXT;
	wstring data;

	for (wchar_t c : a_text)
	{
		if (c == L'\x2190')
		{
			if (parser_state != PS::ANYTEXT)
			{
				parser_state = PS::PARSERERROR;
				break;
			}

			if (!data.empty())
			{
				m_commands.emplace_back(ansi_command_t{ 0, data });
				data.clear();
			}

			parser_state = PS::ESC_BRACKET;
		}
		else if (c == L'[' && parser_state == PS::ESC_BRACKET)
		{
			parser_state = PS::ESC_DATA;
		}
		else if (parser_state == PS::ESC_DATA)
		{
			if (iswdigit(c) || c == L';' || c == L'?')
			{
				data += c;
			}
			else if (iswalpha(c))
			{
				m_commands.emplace_back(ansi_command_t{ c, data });
				data.clear();

				parser_state = PS::ANYTEXT;
			}
			else if (iswspace(c))
			{
				// ignore sequences terminated with space instead of alpha character.
				data.clear();
				parser_state = PS::ANYTEXT;
			}
			else
			{
				parser_state = PS::PARSERERROR;
				break;
			}
		}
		else if (parser_state == PS::ANYTEXT)
		{
			data += c;
		}
		else
		{
			parser_state = PS::PARSERERROR;
			break;
		}
	}

	if (parser_state == PS::ANYTEXT)
	{
		if (!data.empty())
		{
			m_commands.emplace_back(ansi_command_t{ 0, data });
		}
	}
	else
	{
		parser_state = PS::PARSERERROR;
	}

	return (parser_state != PS::PARSERERROR && !m_commands.empty());
}


bool CAnsiArt::Process()
{
	if (m_commands.empty())
	{
		// handle empty files...? naaah
		return false;
	}

	m_colorMap = std::make_shared<CNFOColorMap>();

	TwoDimVector<wchar_t> screen(
		(m_hintHeight ? m_hintHeight : 100),
		m_hintWidth,
		L' ');

	std::stack<std::pair<long, long>> saved_positions;
	long x = 0, y = 0;

	for (const ansi_command_t& cmd : m_commands)
	{
		long x_delta = 0, y_delta = 0;
		long n = 0, m = 0;

		if (cmd.cmd != 0 && cmd.cmd != L'm')
		{
			// this could be done somewhat nicer, but okay for now:
			wstring::size_type pos;

			n = std::max(CUtil::StringToLong(cmd.data), 1l);

			if ((pos = cmd.data.find(L';')) != wstring::npos)
			{
				m = std::max(CUtil::StringToLong(cmd.data.substr(pos + 1)), 1l);
			}
			else
			{
				m = 1;
			}
		}

		switch (cmd.cmd)
		{
		case 0: { // put text to current position
			size_t used_x_start = x, used_x = 0;

			for (wchar_t c : cmd.data)
			{
				if (c == L'\r')
				{
					// ignore CR
				}
				else if (c == L'\n' || (m_hintWidth != 0 && x == m_hintWidth - 1))
				{
					if (y >= screen.GetRows() - 1)
					{
						size_t new_rows = screen.GetRows() + std::max<size_t>(50, y - (screen.GetRows() - 1));

						if (new_rows > m_heightLimit || new_rows < screen.GetRows() /* overflow safeguard */)
						{
							return false;
						}

						screen.Extend(new_rows, screen.GetCols(), L' ');
					}

					if (c != L'\n')
					{
						// when line wrapping, do not forget this character!
						screen[y][x] = c;

						++used_x;
					}

					if (used_x > 0)
					{
						m_colorMap->PushUsedSection(y, used_x_start, used_x);
					}

					++y;
					x = 0;

					used_x_start = 0;
					used_x = 0;
				}
				else
				{
					if (x >= screen.GetCols() - 1)
					{
						size_t new_cols = screen.GetCols() + std::max<size_t>(50, x - (screen.GetCols() - 1));

						if (new_cols > m_widthLimit || new_cols < screen.GetCols() /* overflow safeguard */)
						{
							return false;
						}

						screen.Extend(screen.GetRows(), new_cols, L' ');
					}

					screen[y][x] = c;
					++x;

					++used_x;
				}
			}

			if (used_x > 0)
			{
				m_colorMap->PushUsedSection(y, used_x_start, used_x);
			}
			break;
		}
		case L'A': { // cursor up
			y_delta = -n;
			break;
		}
		case L'B': { // cursor down
			y_delta = n;
			break;
		}
		case L'C': { // cursor forward
			x_delta = n;
			break;
		}
		case L'D': { // cursor back
			x_delta = -n;
			break;
		}
		case L'E': { // cursor to beginning of next line
			y_delta = n;
			x = 0;
			break;
		}
		case L'F': { // cursor to beginning of previous line
			y_delta = -n;
			x = 0;
			break;
		}
		case L'G': { // move to given column
			x = n - 1;
			break;
		}
		case L'H':
		case L'f': { // moves the cursor to row n, column m
			y = n - 1;
			x = m - 1;
			break;
		}
		case L'J': { // erase display
			// only cursor pos change is supported, ignoring erase command:
			if (n == 2)
			{
				x = y = 0;
			}
			break;
		}
		case L'K': { // erase in line
			// unsupported
			break;
		}
		case L's': { // save cursor pos
			saved_positions.emplace(x, y);
			break;
		}
		case L'u': { // restore cursor pos
			if (!saved_positions.empty()) {
				x = saved_positions.top().first;
				y = saved_positions.top().second;
				saved_positions.pop();
			}
			break;
		}
		case L'm': { // rainbows and stuff!
			std::vector<uint8_t> params;

			for (const wstring& s : CUtil::StrSplit(cmd.data, L";"))
			{
				long n = CUtil::StringToLong(s);

				if (n >= 0 && n <= 255)
				{
					params.emplace_back(static_cast<uint8_t>(n));
				}
			}

			if (!params.empty())
			{
				m_colorMap->PushGraphicRendition(y, x, params);
			}
			break;
		}
		case L'h': // Changes the screen width or type
			if (n == 7)
			{
				// enable line wrapping
			}
			break;
		case L'l': // Reset screen width or type
			if (n == 7)
			{
				// disable line wrapping
			}
			break;
			// some more info about h + l: http://ascii-table.com/ansi-escape-sequences.php
		case L'S': // scroll up
		case L'T': // scroll down
		case L'n': // report cursor position
			// unsupported, ignore
			break;
		default:
			// unknown
			_ASSERT(false);
		}

		if (y_delta < 0 && std::abs(y_delta) <= y)
		{
			y += y_delta;
		}
		else if (y_delta > 0)
		{
			y += y_delta;
		}
		else if (y_delta != 0)
		{
			// out of bounds, confine to screen
			y = 0;
		}

		if (x_delta < 0 && std::abs(x_delta) <= x)
		{
			x += x_delta;
		}
		else if (x_delta > 0)
		{
			x += x_delta;
		}
		else if (x_delta != 0)
		{
			// out of bounds, confine to screen
			x = 0;
		}

		if (x >= screen.GetCols() || y >= screen.GetRows())
		{
			if (x >= m_widthLimit || y >= m_heightLimit)
			{
				return false;
			}

			screen.Extend(static_cast<size_t>(y) + 1, static_cast<size_t>(x) + 1, L' ');
		}
	}

	// and finally read lines from "screen" into internal structures:

	m_maxLineLength = 0;
	m_lines.clear();

	for (size_t row = 0; row < screen.GetRows(); ++row)
	{
		size_t line_used = 0;

		for (size_t col = screen.GetCols() - 1; col >= 0 && col < screen.GetCols(); col--)
		{
			if (screen[row][col] != L' ')
			{
				line_used = col + 1;
				break;
			}
		}

		m_lines.emplace_back(wstring(screen[row].begin(), screen[row].begin() + line_used));

		if (line_used > m_maxLineLength)
		{
			m_maxLineLength = line_used;
		}
	}

	// kill empty trailing lines:

	while (!m_lines.empty())
	{
		if (m_lines.back().find_first_not_of(L" ") != wstring::npos)
		{
			break;
		}

		m_lines.pop_back();
	}

	return true;
}


wstring CAnsiArt::GetAsClassicText() const
{
	wstring result;

	for (const wstring& line : m_lines)
	{
		result += line;
		result += L'\n';
	}

	return result;
}
