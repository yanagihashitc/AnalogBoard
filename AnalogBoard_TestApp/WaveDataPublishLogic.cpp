#include "WaveDataPublishLogic.h"

namespace wave_data_publish {

bool FilePairNames::HasEmptyPath() const
{
	return lowTmpFileName.empty() || highTmpFileName.empty() ||
		lowFinalFileName.empty() || highFinalFileName.empty();
}

void FilePairNames::Clear()
{
	lowTmpFileName.clear();
	highTmpFileName.clear();
	lowFinalFileName.clear();
	highFinalFileName.clear();
}

int PublishFilePair(FilePairNames& names, IPublishOps& ops)
{
	int result = 0;

	if (names.HasEmptyPath())
	{
		return kPublishErrInvalidArg;
	}

	result = ops.CloseAndRenameLow(names.lowTmpFileName, names.lowFinalFileName);
	if (result != 0)
	{
		ops.CleanupOnError(names.highTmpFileName, names.lowTmpFileName);
		return kPublishErrLowFileBase + result;
	}

	result = ops.CloseAndRenameHigh(names.highTmpFileName, names.highFinalFileName);
	if (result != 0)
	{
		if (!ops.RollbackLowFile(names.lowFinalFileName, names.lowTmpFileName))
		{
			ops.CleanupOnError(names.highTmpFileName, names.lowTmpFileName);
			return kPublishErrRollbackBase + result;
		}

		ops.CleanupOnError(names.highTmpFileName, names.lowTmpFileName);
		return kPublishErrHighFileBase + result;
	}

	names.Clear();
	return 0;
}

} // namespace wave_data_publish
