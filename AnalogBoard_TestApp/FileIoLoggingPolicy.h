#pragma once

#include "WaveDataFileIO.h"

namespace FileIoLoggingPolicy
{
    inline bool ShouldLogOpenSuccess()
    {
        return false;
    }

    inline bool ShouldLogWriteSuccess()
    {
        return false;
    }

    inline bool ShouldLogCloseSuccess()
    {
        return false;
    }

    inline bool ShouldLogPublishSuccess(const WaveDataFileIO::RenameAttemptResult& renameResult)
    {
        return renameResult.retried;
    }
}
