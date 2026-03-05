#include <windows.h>
#include <bcrypt.h>

#include <cstdio>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <set>
#include <chrono>
#include <algorithm>

#include "../AnalogBoard_TestApp/WaveDataFileIO.h"

namespace fs = std::filesystem;

static int g_TestCount = 0;
static int g_PassCount = 0;
static int g_FailCount = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_TestCount++; \
    if (cond) { g_PassCount++; } \
    else { g_FailCount++; std::printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while(0)

#define RUN_TEST(func) do { \
    std::printf("[TEST] %s\n", #func); \
    func(); \
} while(0)

std::vector<unsigned char> ReadBinaryFile(const fs::path& path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
    {
        return {};
    }

    return std::vector<unsigned char>((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

bool WriteBinaryFile(const fs::path& path, const std::vector<unsigned char>& data)
{
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs)
    {
        return false;
    }

    ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return ofs.good();
}

std::string Sha256Hex(const std::vector<unsigned char>& data)
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    DWORD objLen = 0;
    DWORD hashLen = 0;
    DWORD cbData = 0;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
    {
        return {};
    }

    if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(DWORD), &cbData, 0) != 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLen), sizeof(DWORD), &cbData, 0) != 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    std::vector<UCHAR> hashObject(objLen, 0);
    std::vector<UCHAR> hashValue(hashLen, 0);

    if (BCryptCreateHash(hAlg, &hHash, hashObject.data(), objLen, nullptr, 0, 0) != 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    if (!data.empty())
    {
        if (BCryptHashData(hHash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0) != 0)
        {
            BCryptDestroyHash(hHash);
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return {};
        }
    }

    if (BCryptFinishHash(hHash, hashValue.data(), hashLen, 0) != 0)
    {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (UCHAR b : hashValue)
    {
        oss << std::setw(2) << static_cast<int>(b);
    }

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return oss.str();
}

std::string Sha256File(const fs::path& path)
{
    return Sha256Hex(ReadBinaryFile(path));
}

void EnsureCleanDir(const fs::path& path)
{
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path, ec);
}

bool IsRepoRootCandidate(const fs::path& dir)
{
    std::error_code ec;
    const bool hasGit = fs::exists(dir / L".git", ec);
    if (hasGit)
    {
        return true;
    }

    const bool hasSolution = fs::exists(dir / L"AnalogBoard_TestApp.sln", ec);
    const bool hasAppDir = fs::exists(dir / L"AnalogBoard_TestApp", ec);
    const bool hasUnitDir = fs::exists(dir / L"AnalogBoard_UnitTest", ec);
    return hasSolution && hasAppDir && hasUnitDir;
}

fs::path FindRepoRootFrom(const fs::path& startDir)
{
    fs::path dir = startDir;
    for (int i = 0; i < 12; ++i)
    {
        if (IsRepoRootCandidate(dir))
        {
            return dir;
        }

        if (!dir.has_parent_path())
        {
            break;
        }

        const fs::path parent = dir.parent_path();
        if (parent == dir)
        {
            break;
        }
        dir = parent;
    }

    return fs::path();
}

fs::path DetectRepoRoot()
{
    wchar_t exePath[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const fs::path fromExe = FindRepoRootFrom(fs::path(exePath).parent_path());
    if (!fromExe.empty())
    {
        return fromExe;
    }

    const fs::path fromCurrent = FindRepoRootFrom(fs::current_path());
    if (!fromCurrent.empty())
    {
        return fromCurrent;
    }

    return fs::current_path();
}

bool EndsWith(const std::wstring& text, const wchar_t* suffix)
{
    const std::wstring suffixStr(suffix);
    if (text.size() < suffixStr.size())
    {
        return false;
    }

    return text.compare(text.size() - suffixStr.size(), suffixStr.size(), suffixStr) == 0;
}

bool ParseWaveBinKey(const std::wstring& fileName, const wchar_t* marker, std::wstring* outKey)
{
    if (!EndsWith(fileName, L".bin"))
    {
        return false;
    }

    const size_t markerPos = fileName.rfind(marker);
    if (markerPos == std::wstring::npos)
    {
        return false;
    }

    const size_t indexStart = markerPos + std::wcslen(marker);
    const size_t indexEnd = fileName.size() - std::wcslen(L".bin");
    if (indexStart >= indexEnd)
    {
        return false;
    }

    for (size_t i = indexStart; i < indexEnd; ++i)
    {
        if (fileName[i] < L'0' || fileName[i] > L'9')
        {
            return false;
        }
    }

    if (outKey != nullptr)
    {
        *outKey = fileName.substr(0, markerPos) + L"|" + fileName.substr(indexStart, indexEnd - indexStart);
    }
    return true;
}

int CountCompleteBinPairs(const fs::path& dir)
{
    std::set<std::wstring> lowKeys;
    std::set<std::wstring> highKeys;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        const std::wstring name = entry.path().filename().wstring();
        std::wstring key;
        if (ParseWaveBinKey(name, L"_fl_", &key))
        {
            lowKeys.insert(key);
            continue;
        }

        if (ParseWaveBinKey(name, L"_fh_", &key))
        {
            highKeys.insert(key);
        }
    }

    int pairCount = 0;
    for (const std::wstring& key : lowKeys)
    {
        if (highKeys.find(key) != highKeys.end())
        {
            ++pairCount;
        }
    }
    return pairCount;
}

int CountFilesBySuffix(const fs::path& dir, const wchar_t* suffix)
{
    int count = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        const std::wstring name = entry.path().filename().wstring();
        if (EndsWith(name, suffix))
        {
            ++count;
        }
    }

    return count;
}

struct PerfStats
{
    double averageMs = 0.0;
    double p95Ms = 0.0;
    double maxMs = 0.0;
};

PerfStats ComputePerfStats(const std::vector<double>& valuesMs)
{
    PerfStats stats = {};
    if (valuesMs.empty())
    {
        return stats;
    }

    double total = 0.0;
    for (double v : valuesMs)
    {
        total += v;
        if (v > stats.maxMs)
        {
            stats.maxMs = v;
        }
    }
    stats.averageMs = total / static_cast<double>(valuesMs.size());

    std::vector<double> sorted = valuesMs;
    std::sort(sorted.begin(), sorted.end());
    const size_t p95Index = static_cast<size_t>(std::floor(static_cast<double>(sorted.size() - 1) * 0.95));
    stats.p95Ms = sorted[p95Index];
    return stats;
}

class BufferWriter
{
public:
    bool Write(const BYTE* data, ULONG size)
    {
        if (size == 0)
        {
            return true;
        }
        if (data == nullptr)
        {
            return false;
        }

        bytes_.insert(bytes_.end(), data, data + size);
        return true;
    }

    const std::vector<BYTE>& Bytes() const
    {
        return bytes_;
    }

private:
    std::vector<BYTE> bytes_;
};

void Test_T0_SaveWaveDataToFileImpl_NullLowWriter_SkipsLow()
{
    const BYTE waveData[] = {
        0x01, 0x02, 0xA1, 0xA2, 0xA3,
        0x03, 0x04, 0xB1, 0xB2, 0xB3
    };

    BufferWriter highWriter;
    BufferWriter* lowWriter = nullptr;
    const INT result = WaveDataFileIO::SaveWaveDataToFileImpl(
        lowWriter,
        &highWriter,
        waveData,
        2,
        3,
        2);

    TEST_ASSERT(result == WaveDataFileIO::kSaveWaveDataOk, "T0 null low writer should be skipped");

    const std::vector<BYTE> expectedHigh = { 0xA1, 0xA2, 0xA3, 0xB1, 0xB2, 0xB3 };
    TEST_ASSERT(highWriter.Bytes() == expectedHigh, "T0 high writer must receive high payload only");
}

void Test_T0_SaveWaveDataToFileImpl_NullHighWriter_SkipsHigh()
{
    const BYTE waveData[] = {
        0x11, 0x12, 0xC1, 0xC2, 0xC3,
        0x13, 0x14, 0xD1, 0xD2, 0xD3
    };

    BufferWriter lowWriter;
    BufferWriter* highWriter = nullptr;
    const INT result = WaveDataFileIO::SaveWaveDataToFileImpl(
        &lowWriter,
        highWriter,
        waveData,
        2,
        3,
        2);

    TEST_ASSERT(result == WaveDataFileIO::kSaveWaveDataOk, "T0 null high writer should be skipped");

    const std::vector<BYTE> expectedLow = { 0x11, 0x12, 0x13, 0x14 };
    TEST_ASSERT(lowWriter.Bytes() == expectedLow, "T0 low writer must receive low payload only");
}

void Test_T1_BinaryFormatUnchanged_AllPairs()
{
    struct PairCase
    {
        const wchar_t* dataDir;
        const wchar_t* timestamp;
        int index;
        int waveNum;
    };

    constexpr ULONG kFrameSizeL = 38400;
    constexpr ULONG kFrameSizeH = 24000;
    constexpr ULONG kOneWaveSize = kFrameSizeL + kFrameSizeH;
    const fs::path repoRoot = DetectRepoRoot();

    const PairCase cases[] = {
        { L"sample_data",  L"251224_1007", 1, 500 },
        { L"sample_data2", L"260220_1309", 1, 100 },
        { L"sample_data2", L"260220_1309", 2, 100 },
        { L"sample_data2", L"260220_1309", 3, 100 },
        { L"sample_data3", L"251224_1406", 1, 500 },
        { L"sample_data3", L"251224_1406", 2, 500 },
    };

    const fs::path outputRoot = fs::temp_directory_path() / L"wave_data_io_tests" / L"tmp_wave_data_io_t1";
    EnsureCleanDir(outputRoot);

    for (const PairCase& c : cases)
    {
        std::wstringstream flName;
        std::wstringstream fhName;
        std::wstringstream readbufName;
        flName << c.timestamp << L"_fl_" << c.index << L".bin";
        fhName << c.timestamp << L"_fh_" << c.index << L".bin";
        readbufName << c.timestamp << L"_readbuf_" << c.index << L".bin";

        const fs::path baseDataDir = repoRoot / L"data" / c.dataDir;
        const fs::path srcFl = baseDataDir / flName.str();
        const fs::path srcFh = baseDataDir / fhName.str();
        const fs::path readBufPath = baseDataDir / L"reconstructed" / readbufName.str();

        const std::vector<unsigned char> readBuf = ReadBinaryFile(readBufPath);
        const size_t expectedReadBufSize = static_cast<size_t>(c.waveNum) * static_cast<size_t>(kOneWaveSize);
        TEST_ASSERT(!readBuf.empty(), "ReadBuf file must exist");
        TEST_ASSERT(readBuf.size() == expectedReadBufSize, "ReadBuf size must match WaveNum * OneWaveSize");
        if (readBuf.size() != expectedReadBufSize)
        {
            continue;
        }

        const fs::path outDir = outputRoot / c.dataDir;
        fs::create_directories(outDir);
        const fs::path outTmpFl = outDir / (flName.str() + L".tmp");
        const fs::path outTmpFh = outDir / (fhName.str() + L".tmp");
        const fs::path outFl = outDir / flName.str();
        const fs::path outFh = outDir / fhName.str();

        WaveDataFileIO::StdFileWriter writerLow;
        WaveDataFileIO::StdFileWriter writerHigh;
        TEST_ASSERT(writerLow.Open(outTmpFl.c_str()), "Open low tmp output");
        TEST_ASSERT(writerHigh.Open(outTmpFh.c_str()), "Open high tmp output");
        if (!writerLow.IsOpen() || !writerHigh.IsOpen())
        {
            continue;
        }

        const INT saveResult = WaveDataFileIO::SaveWaveDataToFileImpl(
            writerLow, writerHigh, readBuf.data(), kFrameSizeL, kFrameSizeH, c.waveNum);
        TEST_ASSERT(saveResult == WaveDataFileIO::kSaveWaveDataOk, "SaveWaveDataToFileImpl result");
        TEST_ASSERT(writerLow.Flush(), "Flush low output");
        TEST_ASSERT(writerHigh.Flush(), "Flush high output");
        TEST_ASSERT(writerLow.Close(), "Close low output");
        TEST_ASSERT(writerHigh.Close(), "Close high output");

        TEST_ASSERT(::MoveFileExW(outTmpFl.c_str(), outFl.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE, "Rename low tmp -> bin");
        TEST_ASSERT(::MoveFileExW(outTmpFh.c_str(), outFh.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE, "Rename high tmp -> bin");

        const std::string srcHashFl = Sha256File(srcFl);
        const std::string srcHashFh = Sha256File(srcFh);
        const std::string outHashFl = Sha256File(outFl);
        const std::string outHashFh = Sha256File(outFh);
        TEST_ASSERT(!srcHashFl.empty() && !srcHashFh.empty(), "Source hash must be computed");
        TEST_ASSERT(!outHashFl.empty() && !outHashFh.empty(), "Output hash must be computed");
        TEST_ASSERT(srcHashFl == outHashFl, "FL SHA256 must be unchanged");
        TEST_ASSERT(srcHashFh == outHashFh, "FH SHA256 must be unchanged");
    }
}

void Test_T2_AtomicPublish_Success()
{
    const fs::path root = fs::temp_directory_path() / L"wave_data_io_tests" / L"tmp_wave_data_io_t2_success";
    EnsureCleanDir(root);

    const fs::path tmpFl = root / L"ok_fl_1.bin.tmp";
    const fs::path tmpFh = root / L"ok_fh_1.bin.tmp";
    const fs::path finalFl = root / L"ok_fl_1.bin";
    const fs::path finalFh = root / L"ok_fh_1.bin";

    TEST_ASSERT(WriteBinaryFile(tmpFl, { 0x11, 0x22, 0x33 }), "Write low tmp");
    TEST_ASSERT(WriteBinaryFile(tmpFh, { 0x44, 0x55, 0x66 }), "Write high tmp");

    const WaveDataFileIO::PublishPairResult result = WaveDataFileIO::PublishWavePairAtomic(
        tmpFl.c_str(), finalFl.c_str(), tmpFh.c_str(), finalFh.c_str(), 100);

    TEST_ASSERT(result.success, "PublishWavePairAtomic success");
    TEST_ASSERT(result.low.success, "Low rename success");
    TEST_ASSERT(result.high.success, "High rename success");
    TEST_ASSERT(fs::exists(finalFl), "Final low exists");
    TEST_ASSERT(fs::exists(finalFh), "Final high exists");
    TEST_ASSERT(!fs::exists(tmpFl), "Low tmp removed");
    TEST_ASSERT(!fs::exists(tmpFh), "High tmp removed");
}

void Test_T2_AtomicPublish_FlRenameFail_TmpRemains()
{
    const fs::path root = fs::temp_directory_path() / L"wave_data_io_tests" / L"tmp_wave_data_io_t2_fl_fail";
    EnsureCleanDir(root);

    const fs::path tmpFl = root / L"fail_fl_1.bin.tmp";
    const fs::path tmpFh = root / L"fail_fh_1.bin.tmp";
    const fs::path finalFl = root / L"fail_fl_1.bin";
    const fs::path finalFh = root / L"fail_fh_1.bin";

    TEST_ASSERT(WriteBinaryFile(tmpFl, { 0xAA, 0xBB, 0xCC }), "Write low tmp");
    TEST_ASSERT(WriteBinaryFile(tmpFh, { 0xDD, 0xEE, 0xFF }), "Write high tmp");
    TEST_ASSERT(WriteBinaryFile(finalFl, { 0x01 }), "Write existing low final");

    HANDLE lock = ::CreateFileW(
        finalFl.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    TEST_ASSERT(lock != INVALID_HANDLE_VALUE, "Lock final low file");

    const WaveDataFileIO::PublishPairResult result = WaveDataFileIO::PublishWavePairAtomic(
        tmpFl.c_str(), finalFl.c_str(), tmpFh.c_str(), finalFh.c_str(), 100);

    TEST_ASSERT(!result.success, "Publish should fail when low rename fails");
    TEST_ASSERT(!result.low.success, "Low rename fails");
    TEST_ASSERT(fs::exists(tmpFl), "Low tmp should remain");
    TEST_ASSERT(fs::exists(tmpFh), "High tmp should remain untouched");

    if (lock != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(lock);
    }
}

void Test_T2_AtomicPublish_FhRenameFail_RollbackLow()
{
    const fs::path root = fs::temp_directory_path() / L"wave_data_io_tests" / L"tmp_wave_data_io_t2_fh_fail";
    EnsureCleanDir(root);

    const fs::path tmpFl = root / L"fhfail_fl_1.bin.tmp";
    const fs::path tmpFh = root / L"fhfail_fh_1.bin.tmp";
    const fs::path finalFl = root / L"fhfail_fl_1.bin";
    const fs::path finalFh = root / L"fhfail_fh_1.bin";

    TEST_ASSERT(WriteBinaryFile(tmpFl, { 0x10, 0x20, 0x30 }), "Write low tmp");
    TEST_ASSERT(WriteBinaryFile(tmpFh, { 0x40, 0x50, 0x60 }), "Write high tmp");
    TEST_ASSERT(WriteBinaryFile(finalFh, { 0x99 }), "Write existing high final");

    HANDLE lock = ::CreateFileW(
        finalFh.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    TEST_ASSERT(lock != INVALID_HANDLE_VALUE, "Lock final high file");

    const WaveDataFileIO::PublishPairResult result = WaveDataFileIO::PublishWavePairAtomic(
        tmpFl.c_str(), finalFl.c_str(), tmpFh.c_str(), finalFh.c_str(), 100);

    TEST_ASSERT(!result.success, "Publish should fail when high rename fails");
    TEST_ASSERT(result.low.success, "Low rename should succeed first");
    TEST_ASSERT(!result.high.success, "High rename should fail");
    TEST_ASSERT(result.rollbackAttempted, "Rollback must be attempted");
    TEST_ASSERT(!fs::exists(finalFl), "Low final must be deleted by rollback");
    TEST_ASSERT(fs::exists(tmpFh), "High tmp should remain");

    if (lock != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(lock);
    }
}

void Test_T2_AtomicPublish_FhRenameFail_RestoreExistingLow()
{
    const fs::path root = fs::temp_directory_path() / L"wave_data_io_tests" / L"tmp_wave_data_io_t2_fh_fail_restore";
    EnsureCleanDir(root);

    const fs::path tmpFl = root / L"restore_fl_1.bin.tmp";
    const fs::path tmpFh = root / L"restore_fh_1.bin.tmp";
    const fs::path finalFl = root / L"restore_fl_1.bin";
    const fs::path finalFh = root / L"restore_fh_1.bin";

    const std::vector<unsigned char> existingLow = { 0xDE, 0xAD, 0xBE, 0xEF };
    const std::vector<unsigned char> newLow = { 0x10, 0x20, 0x30 };

    TEST_ASSERT(WriteBinaryFile(tmpFl, newLow), "Write low tmp");
    TEST_ASSERT(WriteBinaryFile(tmpFh, { 0x40, 0x50, 0x60 }), "Write high tmp");
    TEST_ASSERT(WriteBinaryFile(finalFl, existingLow), "Write existing low final");
    TEST_ASSERT(WriteBinaryFile(finalFh, { 0x99 }), "Write existing high final");

    HANDLE lock = ::CreateFileW(
        finalFh.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    TEST_ASSERT(lock != INVALID_HANDLE_VALUE, "Lock final high file");

    const WaveDataFileIO::PublishPairResult result = WaveDataFileIO::PublishWavePairAtomic(
        tmpFl.c_str(), finalFl.c_str(), tmpFh.c_str(), finalFh.c_str(), 100);

    TEST_ASSERT(!result.success, "Publish should fail when high rename fails");
    TEST_ASSERT(result.low.success, "Low rename should succeed before high failure");
    TEST_ASSERT(!result.high.success, "High rename should fail");
    TEST_ASSERT(result.rollbackAttempted, "Rollback must be attempted");
    TEST_ASSERT(result.rollbackSucceeded, "Rollback should restore existing low");
    TEST_ASSERT(fs::exists(finalFl), "Existing low final should remain after rollback");
    TEST_ASSERT(ReadBinaryFile(finalFl) == existingLow, "Existing low final content must be preserved");
    TEST_ASSERT(!fs::exists(tmpFl), "Low tmp should be consumed by low rename");
    TEST_ASSERT(fs::exists(tmpFh), "High tmp should remain when high rename fails");

    if (lock != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(lock);
    }
}

void Test_T3_PseudoIntegration_TmpOnlyThenPublishedPair()
{
    const fs::path root = fs::temp_directory_path() / L"wave_data_io_tests" / L"tmp_wave_data_io_t3_publish";
    EnsureCleanDir(root);

    const fs::path tmpFl = root / L"260305_0105_fl_1.bin.tmp";
    const fs::path tmpFh = root / L"260305_0105_fh_1.bin.tmp";
    const fs::path finalFl = root / L"260305_0105_fl_1.bin";
    const fs::path finalFh = root / L"260305_0105_fh_1.bin";

    // Given: Measuring in progress, only tmp files exist.
    TEST_ASSERT(WriteBinaryFile(tmpFl, { 0x01, 0x02, 0x03 }), "T3-1 write low tmp");
    TEST_ASSERT(WriteBinaryFile(tmpFh, { 0x04, 0x05, 0x06 }), "T3-1 write high tmp");
    TEST_ASSERT(CountCompleteBinPairs(root) == 0, "T3-1 no complete pair while tmp only");
    TEST_ASSERT(CountFilesBySuffix(root, L".bin") == 0, "T3-1 no published .bin while tmp only");

    // When: Atomic publish is executed.
    const WaveDataFileIO::PublishPairResult result = WaveDataFileIO::PublishWavePairAtomic(
        tmpFl.c_str(), finalFl.c_str(), tmpFh.c_str(), finalFh.c_str(), 100);

    // Then: Only completed bin pair is observed.
    TEST_ASSERT(result.success, "T3-1 publish success");
    TEST_ASSERT(CountCompleteBinPairs(root) == 1, "T3-1 complete pair appears after publish");
    TEST_ASSERT(CountFilesBySuffix(root, L".bin.tmp") == 0, "T3-1 no tmp after publish");
}

void Test_T3_ForcedStop_TmpRemains_ExcludedFromDownstream()
{
    const fs::path root = fs::temp_directory_path() / L"wave_data_io_tests" / L"tmp_wave_data_io_t3_forced_stop";
    EnsureCleanDir(root);

    const fs::path tmpFl = root / L"260305_0200_fl_3.bin.tmp";
    const fs::path tmpFh = root / L"260305_0200_fh_3.bin.tmp";

    // Given: Interrupted session left only tmp files.
    TEST_ASSERT(WriteBinaryFile(tmpFl, { 0xAA, 0xBB }), "T3-2 write low tmp");
    TEST_ASSERT(WriteBinaryFile(tmpFh, { 0xCC, 0xDD }), "T3-2 write high tmp");

    // When: Downstream scans only *.bin pair files.
    const int pairCount = CountCompleteBinPairs(root);
    const int binCount = CountFilesBySuffix(root, L".bin");

    // Then: Tmp remnants are excluded from downstream discovery.
    TEST_ASSERT(pairCount == 0, "T3-2 no complete pair detected");
    TEST_ASSERT(binCount == 0, "T3-2 no .bin file detected");
    TEST_ASSERT(CountFilesBySuffix(root, L".bin.tmp") == 2, "T3-2 tmp files remain for later cleanup");
}

void Test_T3_RestartCleanup_DeletesTargetPatternOnly()
{
    const fs::path root = fs::temp_directory_path() / L"wave_data_io_tests" / L"tmp_wave_data_io_t3_cleanup";
    EnsureCleanDir(root);

    const fs::path targetFlTmp = root / L"260305_0300_fl_1.bin.tmp";
    const fs::path targetFhTmp = root / L"260305_0300_fh_1.bin.tmp";
    const fs::path otherTmp = root / L"other.bin.tmp";
    const fs::path finalBin = root / L"260305_0300_fl_1.bin";
    const fs::path cfg = root / L"260305_0300_cfg.txt";

    // Given: Startup folder contains target and non-target files.
    TEST_ASSERT(WriteBinaryFile(targetFlTmp, { 0x10 }), "T3-3 write target fl tmp");
    TEST_ASSERT(WriteBinaryFile(targetFhTmp, { 0x11 }), "T3-3 write target fh tmp");
    TEST_ASSERT(WriteBinaryFile(otherTmp, { 0x12 }), "T3-3 write non-target tmp");
    TEST_ASSERT(WriteBinaryFile(finalBin, { 0x13 }), "T3-3 write final bin");
    TEST_ASSERT(WriteBinaryFile(cfg, { 0x14 }), "T3-3 write cfg");

    // When: Startup cleanup runs.
    const WaveDataFileIO::CleanupTmpResult cleanupResult =
        WaveDataFileIO::CleanupResidualBinTmpFiles(root.c_str());

    // Then: Only *_fl_*.bin.tmp and *_fh_*.bin.tmp are deleted.
    TEST_ASSERT(cleanupResult.deletedCount == 2, "T3-3 deleted count must be 2");
    TEST_ASSERT(cleanupResult.failedCount == 0, "T3-3 no delete failure");
    TEST_ASSERT(!fs::exists(targetFlTmp), "T3-3 target fl tmp deleted");
    TEST_ASSERT(!fs::exists(targetFhTmp), "T3-3 target fh tmp deleted");
    TEST_ASSERT(fs::exists(otherTmp), "T3-3 non-target tmp preserved");
    TEST_ASSERT(fs::exists(finalBin), "T3-3 final bin preserved");
    TEST_ASSERT(fs::exists(cfg), "T3-3 cfg preserved");
}

void Test_T3_RestartCleanup_NoTarget_NoDelete()
{
    const fs::path root = fs::temp_directory_path() / L"wave_data_io_tests" / L"tmp_wave_data_io_t3_cleanup_empty";
    EnsureCleanDir(root);

    const fs::path onlyBin = root / L"sample_fl_1.bin";
    const fs::path onlyCfg = root / L"sample_cfg.txt";

    // Given: No cleanup target exists.
    TEST_ASSERT(WriteBinaryFile(onlyBin, { 0x21 }), "T3-3-empty write bin");
    TEST_ASSERT(WriteBinaryFile(onlyCfg, { 0x22 }), "T3-3-empty write cfg");

    // When: Startup cleanup runs.
    const WaveDataFileIO::CleanupTmpResult cleanupResult =
        WaveDataFileIO::CleanupResidualBinTmpFiles(root.c_str());

    // Then: Nothing is deleted and no failure occurs.
    TEST_ASSERT(cleanupResult.deletedCount == 0, "T3-3-empty deleted count must be 0");
    TEST_ASSERT(cleanupResult.failedCount == 0, "T3-3-empty failed count must be 0");
    TEST_ASSERT(fs::exists(onlyBin), "T3-3-empty bin preserved");
    TEST_ASSERT(fs::exists(onlyCfg), "T3-3-empty cfg preserved");
}

void Test_T4_DownstreamPolling_ErrorRateReduced()
{
    const fs::path root = fs::temp_directory_path() / L"wave_data_io_tests" / L"tmp_wave_data_io_t4";
    EnsureCleanDir(root);

    constexpr int kFileCount = 120;
    std::vector<unsigned char> payload(8192, 0x7A);
    int directFailCount = 0;
    int atomicFailCount = 0;

    for (int i = 1; i <= kFileCount; ++i)
    {
        const fs::path directFinal = root / (L"direct_fl_" + std::to_wstring(i) + L".bin");

        // Given: Direct write opens final path with exclusive access.
        HANDLE h = ::CreateFileW(
            directFinal.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        TEST_ASSERT(h != INVALID_HANDLE_VALUE, "T4 direct writer open");
        if (h == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        // When: Poller tries reading while writer is still open.
        HANDLE poll = ::CreateFileW(
            directFinal.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (poll == INVALID_HANDLE_VALUE && ::GetLastError() == ERROR_SHARING_VIOLATION)
        {
            ++directFailCount;
        }
        if (poll != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(poll);
        }

        DWORD written = 0;
        ::WriteFile(h, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr);
        ::CloseHandle(h);
    }

    for (int i = 1; i <= kFileCount; ++i)
    {
        const fs::path finalPath = root / (L"atomic_fl_" + std::to_wstring(i) + L".bin");
        const fs::path tmpPath = finalPath.wstring() + L".tmp";

        WaveDataFileIO::StdFileWriter writer;
        TEST_ASSERT(writer.Open(tmpPath.c_str()), "T4 atomic writer open tmp");
        if (!writer.IsOpen())
        {
            continue;
        }
        TEST_ASSERT(writer.Write(payload.data(), static_cast<ULONG>(payload.size())), "T4 atomic write tmp");
        TEST_ASSERT(writer.Flush(), "T4 atomic flush tmp");
        TEST_ASSERT(writer.Close(), "T4 atomic close tmp");

        // Given: Poller scans final path before publish completion.
        HANDLE pollBefore = ::CreateFileW(
            finalPath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (pollBefore == INVALID_HANDLE_VALUE && ::GetLastError() == ERROR_SHARING_VIOLATION)
        {
            ++atomicFailCount;
        }
        if (pollBefore != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(pollBefore);
        }

        // When: File is atomically published.
        const WaveDataFileIO::RenameAttemptResult renameResult =
            WaveDataFileIO::RenameTempFileWithRetry(tmpPath.c_str(), finalPath.c_str(), 100);
        TEST_ASSERT(renameResult.success, "T4 atomic rename success");
    }

    // Then: Polling sharing failures are reduced in atomic flow.
    const double directRate = static_cast<double>(directFailCount) / static_cast<double>(kFileCount);
    const double atomicRate = static_cast<double>(atomicFailCount) / static_cast<double>(kFileCount);
    std::printf("  [T4] direct fail rate=%.4f atomic fail rate=%.4f\n", directRate, atomicRate);
    TEST_ASSERT(atomicFailCount <= directFailCount, "T4 atomic fail count must be <= direct fail count");
}

void Test_T11_Performance_500Files_Measurement()
{
    const fs::path root = fs::temp_directory_path() / L"wave_data_io_tests" / L"tmp_wave_data_io_t11_perf";
    EnsureCleanDir(root);

    constexpr int kFileCount = 500;
    constexpr ULONG kLowSize = 19200000;
    constexpr ULONG kHighSize = 12000000;
    std::vector<unsigned char> lowData(kLowSize, 0x51);
    std::vector<unsigned char> highData(kHighSize, 0xA3);

    std::vector<double> directTimesMs;
    std::vector<double> atomicTimesMs;
    directTimesMs.reserve(kFileCount);
    atomicTimesMs.reserve(kFileCount);

    int renameRetryCount = 0;
    int renameAttemptCount = 0;

    for (int i = 1; i <= kFileCount; ++i)
    {
        const fs::path finalLow = root / (L"perf_direct_fl_" + std::to_wstring(i) + L".bin");
        const fs::path finalHigh = root / (L"perf_direct_fh_" + std::to_wstring(i) + L".bin");

        const auto t0 = std::chrono::steady_clock::now();
        WaveDataFileIO::StdFileWriter writerLow;
        WaveDataFileIO::StdFileWriter writerHigh;
        TEST_ASSERT(writerLow.Open(finalLow.c_str()), "T11 direct open low");
        TEST_ASSERT(writerHigh.Open(finalHigh.c_str()), "T11 direct open high");
        TEST_ASSERT(writerLow.Write(lowData.data(), kLowSize), "T11 direct write low");
        TEST_ASSERT(writerHigh.Write(highData.data(), kHighSize), "T11 direct write high");
        TEST_ASSERT(writerLow.Flush(), "T11 direct flush low");
        TEST_ASSERT(writerHigh.Flush(), "T11 direct flush high");
        TEST_ASSERT(writerLow.Close(), "T11 direct close low");
        TEST_ASSERT(writerHigh.Close(), "T11 direct close high");
        const auto t1 = std::chrono::steady_clock::now();
        directTimesMs.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    for (int i = 1; i <= kFileCount; ++i)
    {
        const fs::path finalLow = root / (L"perf_atomic_fl_" + std::to_wstring(i) + L".bin");
        const fs::path finalHigh = root / (L"perf_atomic_fh_" + std::to_wstring(i) + L".bin");
        const fs::path tmpLow = finalLow.wstring() + L".tmp";
        const fs::path tmpHigh = finalHigh.wstring() + L".tmp";

        const auto t0 = std::chrono::steady_clock::now();
        WaveDataFileIO::StdFileWriter writerLow;
        WaveDataFileIO::StdFileWriter writerHigh;
        TEST_ASSERT(writerLow.Open(tmpLow.c_str()), "T11 atomic open low tmp");
        TEST_ASSERT(writerHigh.Open(tmpHigh.c_str()), "T11 atomic open high tmp");
        TEST_ASSERT(writerLow.Write(lowData.data(), kLowSize), "T11 atomic write low");
        TEST_ASSERT(writerHigh.Write(highData.data(), kHighSize), "T11 atomic write high");
        TEST_ASSERT(writerLow.Flush(), "T11 atomic flush low");
        TEST_ASSERT(writerHigh.Flush(), "T11 atomic flush high");
        TEST_ASSERT(writerLow.Close(), "T11 atomic close low");
        TEST_ASSERT(writerHigh.Close(), "T11 atomic close high");

        const WaveDataFileIO::PublishPairResult publishResult = WaveDataFileIO::PublishWavePairAtomic(
            tmpLow.c_str(), finalLow.c_str(), tmpHigh.c_str(), finalHigh.c_str(), 100);
        TEST_ASSERT(publishResult.success, "T11 atomic publish success");
        renameAttemptCount += 2;
        if (publishResult.low.retried)
        {
            ++renameRetryCount;
        }
        if (publishResult.high.retried)
        {
            ++renameRetryCount;
        }
        const auto t1 = std::chrono::steady_clock::now();
        atomicTimesMs.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    const PerfStats directStats = ComputePerfStats(directTimesMs);
    const PerfStats atomicStats = ComputePerfStats(atomicTimesMs);
    const double degradationRate = (atomicStats.averageMs / directStats.averageMs) - 1.0;
    const double retryRate = renameAttemptCount > 0
        ? static_cast<double>(renameRetryCount) / static_cast<double>(renameAttemptCount)
        : 0.0;

    std::printf(
        "  [T11] direct avg=%.3fms p95=%.3fms max=%.3fms | atomic avg=%.3fms p95=%.3fms max=%.3fms | retryRate=%.5f\n",
        directStats.averageMs,
        directStats.p95Ms,
        directStats.maxMs,
        atomicStats.averageMs,
        atomicStats.p95Ms,
        atomicStats.maxMs,
        retryRate);

    TEST_ASSERT(!directTimesMs.empty() && !atomicTimesMs.empty(), "T11 measurement vectors must not be empty");
    std::printf("  [T11] degradation=%.2f%%\n", degradationRate * 100.0);
}

int main()
{
    std::printf("=== WaveDataFileIO Unit Tests ===\n\n");

    RUN_TEST(Test_T0_SaveWaveDataToFileImpl_NullLowWriter_SkipsLow);
    RUN_TEST(Test_T0_SaveWaveDataToFileImpl_NullHighWriter_SkipsHigh);
    RUN_TEST(Test_T1_BinaryFormatUnchanged_AllPairs);
    RUN_TEST(Test_T2_AtomicPublish_Success);
    RUN_TEST(Test_T2_AtomicPublish_FlRenameFail_TmpRemains);
    RUN_TEST(Test_T2_AtomicPublish_FhRenameFail_RollbackLow);
    RUN_TEST(Test_T2_AtomicPublish_FhRenameFail_RestoreExistingLow);
    RUN_TEST(Test_T3_PseudoIntegration_TmpOnlyThenPublishedPair);
    RUN_TEST(Test_T3_ForcedStop_TmpRemains_ExcludedFromDownstream);
    RUN_TEST(Test_T3_RestartCleanup_DeletesTargetPatternOnly);
    RUN_TEST(Test_T3_RestartCleanup_NoTarget_NoDelete);
    RUN_TEST(Test_T4_DownstreamPolling_ErrorRateReduced);
    RUN_TEST(Test_T11_Performance_500Files_Measurement);

    std::printf("\n=== Results: %d tests, %d passed, %d failed ===\n", g_TestCount, g_PassCount, g_FailCount);

    return g_FailCount > 0 ? 1 : 0;
}
