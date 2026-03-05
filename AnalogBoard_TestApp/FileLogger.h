#ifndef FILE_LOGGER_H
#define FILE_LOGGER_H

#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

class FileLogger
{
public:
	FileLogger() : m_initialized(false) {}

	~FileLogger() { Close(); }

	bool Init(const std::wstring& exeDir)
	{
		if (m_initialized) return true;

		namespace fs = std::filesystem;

		fs::path logsDir = fs::path(exeDir) / L"logs";
		std::error_code ec;
		fs::create_directories(logsDir, ec);
		if (ec) return false;

		SYSTEMTIME st;
		GetLocalTime(&st);
		wchar_t fname[32];
		swprintf_s(fname, L"%02d%02d%02d%02d%02d.log",
			st.wYear % 100, st.wMonth, st.wDay,
			st.wHour, st.wMinute);

		m_logFilePath = (logsDir / fname).wstring();

		m_ofs.open(m_logFilePath, std::ios::app);
		if (!m_ofs.is_open()) return false;

		m_initialized = true;
		return true;
	}

	void Append(const std::wstring& line)
	{
		if (!m_initialized) return;
		m_buffer.push_back(line);
	}

	void Flush()
	{
		if (!m_initialized || m_buffer.empty()) return;

		for (const auto& line : m_buffer)
		{
			int len = WideCharToMultiByte(CP_UTF8, 0,
				line.c_str(), (int)line.size(), nullptr, 0, nullptr, nullptr);
			std::string utf8(len, '\0');
			WideCharToMultiByte(CP_UTF8, 0,
				line.c_str(), (int)line.size(), &utf8[0], len, nullptr, nullptr);
			m_ofs << utf8 << "\n";
		}
		m_ofs.flush();
		m_buffer.clear();
	}

	void Close()
	{
		if (!m_initialized) return;
		Flush();
		m_ofs.close();
		m_initialized = false;
	}

	std::wstring GetLogFilePath() const { return m_logFilePath; }

private:
	bool m_initialized;
	std::wstring m_logFilePath;
	std::ofstream m_ofs;
	std::vector<std::wstring> m_buffer;
};

#endif // FILE_LOGGER_H
