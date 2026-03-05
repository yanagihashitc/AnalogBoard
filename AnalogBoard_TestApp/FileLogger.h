#ifndef FILE_LOGGER_H
#define FILE_LOGGER_H

#ifndef UNICODE
#error "FileLogger requires UNICODE build"
#endif

#include <windows.h>
#include <string>
#include <mutex>

class FileLogger
{
public:
	FileLogger() : m_initialized(false), m_fileHandle(INVALID_HANDLE_VALUE) {}

	~FileLogger() { Close(); }

	bool Init(const std::wstring& exeDir)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_initialized) return true;

		std::wstring logsDir = exeDir;
		if (!logsDir.empty() && logsDir.back() != L'\\')
			logsDir += L'\\';
		logsDir += L"logs";

		if (!CreateDirectoryW(logsDir.c_str(), NULL))
		{
			if (GetLastError() != ERROR_ALREADY_EXISTS)
				return false;
		}

		SYSTEMTIME st;
		GetLocalTime(&st);
		wchar_t fname[32];
		swprintf_s(fname, L"\\%02d%02d%02d%02d%02d.log",
			st.wYear % 100, st.wMonth, st.wDay,
			st.wHour, st.wMinute);

		m_logFilePath = logsDir + fname;

		m_fileHandle = CreateFileW(
			m_logFilePath.c_str(),
			FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (m_fileHandle == INVALID_HANDLE_VALUE) return false;

		m_initialized = true;
		return true;
	}

	void Append(const std::wstring& line)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_initialized) return;
		if (m_fileHandle == INVALID_HANDLE_VALUE) return;

		int len = 0;
		if (!line.empty())
		{
			len = WideCharToMultiByte(CP_UTF8, 0,
				line.c_str(), (int)line.size(), nullptr, 0, nullptr, nullptr);
		}

		const int totalBytes = (len > 0) ? (len + 1) : 1; // include '\n'
		std::string buffer(totalBytes, '\n');
		if (len > 0)
		{
			const int converted = WideCharToMultiByte(CP_UTF8, 0,
				line.c_str(), (int)line.size(), &buffer[0], len, nullptr, nullptr);
			if (converted <= 0)
			{
				return;
			}
		}

		DWORD written = 0;
		WriteFile(m_fileHandle, buffer.data(), (DWORD)buffer.size(), &written, nullptr);
	}

	void Flush()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_initialized) return;
		if (m_fileHandle != INVALID_HANDLE_VALUE)
		{
			FlushFileBuffers(m_fileHandle);
		}
	}

	void Close()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_initialized) return;
		if (m_fileHandle != INVALID_HANDLE_VALUE)
		{
			FlushFileBuffers(m_fileHandle);
			CloseHandle(m_fileHandle);
			m_fileHandle = INVALID_HANDLE_VALUE;
		}
		m_initialized = false;
	}

	std::wstring GetLogFilePath() const { return m_logFilePath; }

private:
	std::mutex m_mutex;
	bool m_initialized;
	std::wstring m_logFilePath;
	HANDLE m_fileHandle;
};

#endif // FILE_LOGGER_H
