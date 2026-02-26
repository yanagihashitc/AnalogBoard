#pragma once

#include <string>

namespace wave_data_publish {

constexpr int kPublishErrInvalidArg = -10;
constexpr int kPublishErrLowFileBase = -100;
constexpr int kPublishErrHighFileBase = -200;
constexpr int kPublishErrRollbackBase = -300;

struct FilePairNames {
	std::wstring highTmpFileName;
	std::wstring lowTmpFileName;
	std::wstring highFinalFileName;
	std::wstring lowFinalFileName;

	bool HasEmptyPath() const;
	void Clear();
};

class IPublishOps {
public:
	virtual ~IPublishOps() = default;
	virtual int CloseAndRenameLow(const std::wstring& tmpPath, const std::wstring& finalPath) = 0;
	virtual int CloseAndRenameHigh(const std::wstring& tmpPath, const std::wstring& finalPath) = 0;
	virtual bool RollbackLowFile(const std::wstring& fromPath, const std::wstring& toPath) = 0;
	virtual void CleanupOnError(const std::wstring& highTmpPath, const std::wstring& lowTmpPath) = 0;
};

int PublishFilePair(FilePairNames& names, IPublishOps& ops);

} // namespace wave_data_publish
