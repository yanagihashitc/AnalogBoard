#include "SimulationRunnerCore.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "../AnalogBoard_TestApp/WaveDataFileIO.h"
#include "FpgaDdrModel.h"
#include "SimulationEp4StatusHelper.h"
#include "SimulationScenario.h"

namespace fs = std::filesystem;

namespace SimRunner
{
    namespace
    {
        std::string WideToUtf8(const std::wstring& value)
        {
            if (value.empty())
            {
                return {};
            }

            const int required = ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            if (required <= 0)
            {
                return {};
            }

            std::string utf8(static_cast<size_t>(required), '\0');
            ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &utf8[0], required, nullptr, nullptr);
            return utf8;
        }

        std::string EscapeJsonString(const std::string& value)
        {
            static const char kHexDigits[] = "0123456789abcdef";

            std::string escaped;
            escaped.reserve(value.size());
            for (unsigned char ch : value)
            {
                switch (ch)
                {
                case '\"':
                    escaped += "\\\"";
                    break;
                case '\\':
                    escaped += "\\\\";
                    break;
                case '\b':
                    escaped += "\\b";
                    break;
                case '\f':
                    escaped += "\\f";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    if (ch < 0x20)
                    {
                        escaped += "\\u00";
                        escaped += kHexDigits[(ch >> 4) & 0x0Fu];
                        escaped += kHexDigits[ch & 0x0Fu];
                    }
                    else
                    {
                        escaped.push_back(static_cast<char>(ch));
                    }
                    break;
                }
            }

            return escaped;
        }

        class RunnerLogFile
        {
        public:
            bool Init(const fs::path& outputDirectory)
            {
                logPath_ = outputDirectory / L"runner.log";
                output_.open(logPath_.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
                if (!output_.is_open())
                {
                    return false;
                }
                return true;
            }

            void Append(const std::wstring& line)
            {
                if (!output_.is_open())
                {
                    return;
                }

                output_ << WideToUtf8(line) << "\n";
                output_.flush();
            }

            std::wstring GetPath() const
            {
                return logPath_.wstring();
            }

        private:
            fs::path logPath_;
            std::ofstream output_;
        };

        class RunnerObserver : public WaveAcquisition::IAcquisitionObserver
        {
        public:
            explicit RunnerObserver(RunnerLogFile* logger)
                : logger_(logger)
            {
            }

            void OnLog(const std::wstring& message) override
            {
                if (logger_ != nullptr)
                {
                    logger_->Append(message);
                }
            }

            void OnCollectedWaveCount(ULONG collectedWaveCount) override
            {
                if (logger_ != nullptr)
                {
                    std::wstringstream line;
                    line << L"[collect] wave_count=" << collectedWaveCount;
                    logger_->Append(line.str());
                }
            }

            void OnCycleSummary(const WaveAcquisition::AcquisitionSummary& summary) override
            {
                if (logger_ != nullptr)
                {
                    std::wstringstream line;
                    line << L"[summary] status="
                        << WaveAcquisition::WaveAcquisitionEngine::ToString(summary.terminalStatus)
                        << L" error=" << summary.errorCode
                        << L" ep6_calls=" << summary.metrics.ep6.callCount
                        << L" timeouts=" << summary.metrics.ep6TimeoutCount;
                    logger_->Append(line.str());
                }
            }

        private:
            RunnerLogFile* logger_ = nullptr;
        };

        class FakeUsbSession : public WaveAcquisition::IUsbSession
        {
        public:
            explicit FakeUsbSession(const SimulationScenario& scenario)
                : scenario_(scenario)
                , totalLogicalBytes_(scenario.waveSizeLow + scenario.waveSizeHigh)
            {
                totalLogicalBytes_ *= scenario.totalWaveCount;

                FpgaDdrModelConfig modelConfig = {};
                modelConfig.totalWaveBytes = totalLogicalBytes_;
                modelConfig.burstSizeBytes = static_cast<ULONG>(WaveAcquisition::kEp6ReadAlignmentBytes);
                modelConfig.producerStepBytes = scenario.producerStepBytes;
                modelConfig.producerBurstsPerPoll = scenario.producerBurstsPerPoll;
                modelConfig.initPollCount = scenario.initPollCount;
                modelConfig.waitPollCount = scenario.waitPollCount;
                ddrModel_ = FpgaDdrModel(modelConfig);
            }

            INT Connect() override
            {
                return WaveAcquisition::kUsbSuccess;
            }

            void Disconnect() override
            {
            }

            INT EP2_SendData(BYTE* buffer, size_t bufferSize) override
            {
                (void)buffer;
                (void)bufferSize;
                return WaveAcquisition::kUsbSuccess;
            }

            INT EP4_GetData(BYTE* buffer, size_t bufferSize) override
            {
                if (buffer == nullptr || bufferSize < WaveAcquisition::kEp4StatusBufferBytes)
                {
                    return WaveAcquisition::kAcquisitionErrEp4Read;
                }

                ddrModel_.AdvanceOnePoll();
                SimulationEp4StatusHelper::WriteStatusBuffer(
                    buffer,
                    bufferSize,
                    ddrModel_.GetWrittenBytes(),
                    ddrModel_.GetReadBytes(),
                    true,
                    ddrModel_.IsAdcSetEnd(),
                    ddrModel_.IsDdrWrEnd(),
                    ddrModel_.IsDdrRdEnd(),
                    ddrModel_.IsMeasTrg());
                return WaveAcquisition::kUsbSuccess;
            }

            INT EP6_GetData(BYTE* buffer, ULONG size) override
            {
                if (buffer == nullptr)
                {
                    return WaveAcquisition::kAcquisitionErrEp6Read;
                }

                INT result = WaveAcquisition::kUsbSuccess;
                if (ep6CallCount_ < static_cast<INT>(scenario_.ep6Results.size()))
                {
                    const Ep6ResultKind kind = scenario_.ep6Results[static_cast<size_t>(ep6CallCount_)];
                    if (kind == Ep6ResultKind::Timeout)
                    {
                        result = WaveAcquisition::kUsbErrTransferTimeout;
                    }
                    else if (kind == Ep6ResultKind::Disconnect)
                    {
                        result = WaveAcquisition::kUsbErrNoDevice;
                    }
                }

                ++ep6CallCount_;
                if (result != WaveAcquisition::kUsbSuccess)
                {
                    return result;
                }

                const ULONG readOffset = ddrModel_.GetReadBytes();
                for (ULONG i = 0; i < size; ++i)
                {
                    buffer[i] = static_cast<BYTE>((readOffset + i) & 0xFFu);
                }

                ddrModel_.OnEp6ReadCompleted(size);
                return WaveAcquisition::kUsbSuccess;
            }

        private:
            SimulationScenario scenario_;
            ULONG totalLogicalBytes_ = 0;
            INT ep6CallCount_ = 0;
            FpgaDdrModel ddrModel_;
        };

        class ScriptedWavePairSink : public WaveAcquisition::IWavePairSink
        {
        public:
            ScriptedWavePairSink(
                const fs::path& outputDirectory,
                const std::wstring& presetName,
                const SimulationScenario& scenario,
                RunnerLogFile* logger)
                : outputDirectory_(outputDirectory)
                , presetName_(presetName)
                , scenario_(scenario)
                , logger_(logger)
            {
            }

            INT OpenPair(INT index) override
            {
                ++openCallCount_;
                currentIndex_ = index;
                finalLowPath_ = outputDirectory_ / (presetName_ + L"_fl_" + std::to_wstring(index) + L".bin");
                finalHighPath_ = outputDirectory_ / (presetName_ + L"_fh_" + std::to_wstring(index) + L".bin");
                tmpLowPath_ = finalLowPath_;
                tmpLowPath_ += L".tmp";
                tmpHighPath_ = finalHighPath_;
                tmpHighPath_ += L".tmp";

                if (!writerLow_.Open(tmpLowPath_.c_str()))
                {
                    return WaveAcquisition::kAcquisitionErrOpenPair;
                }

                if (!writerHigh_.Open(tmpHighPath_.c_str()))
                {
                    writerLow_.Close();
                    ::DeleteFileW(tmpLowPath_.c_str());
                    return WaveAcquisition::kAcquisitionErrOpenPair;
                }

                pairOpen_ = true;
                if (logger_ != nullptr)
                {
                    logger_->Append(L"[open] pair=" + std::to_wstring(index));
                }
                return WaveAcquisition::kUsbSuccess;
            }

            INT Write(const BYTE* waveData, ULONG frameSizeLow, ULONG frameSizeHigh, INT waveCnt) override
            {
                ++writeCallCount_;
                if (scenario_.writeDelayMs > 0)
                {
                    ::Sleep(scenario_.writeDelayMs);
                }

                if (scenario_.writeFailAt > 0 && writeCallCount_ == scenario_.writeFailAt)
                {
                    return WaveAcquisition::kAcquisitionErrWritePair;
                }

                const INT result = WaveDataFileIO::SaveWaveDataToFileImpl(
                    writerLow_,
                    writerHigh_,
                    waveData,
                    frameSizeLow,
                    frameSizeHigh,
                    waveCnt);
                if (result != WaveDataFileIO::kSaveWaveDataOk)
                {
                    return WaveAcquisition::kAcquisitionErrWritePair;
                }

                return WaveAcquisition::kUsbSuccess;
            }

            INT PublishPair() override
            {
                ++publishCallCount_;
                writerLow_.Flush();
                writerHigh_.Flush();
                writerLow_.Close();
                writerHigh_.Close();

                if (scenario_.publishFailAt > 0 && publishCallCount_ == scenario_.publishFailAt)
                {
                    return WaveAcquisition::kAcquisitionErrPublishPair;
                }

                const WaveDataFileIO::PublishPairResult result = WaveDataFileIO::PublishWavePairAtomic(
                    tmpLowPath_.c_str(),
                    finalLowPath_.c_str(),
                    tmpHighPath_.c_str(),
                    finalHighPath_.c_str(),
                    0,
                    0);
                if (!result.success)
                {
                    return WaveAcquisition::kAcquisitionErrPublishPair;
                }

                pairOpen_ = false;
                return WaveAcquisition::kUsbSuccess;
            }

            void AbortPair() override
            {
                writerLow_.Close();
                writerHigh_.Close();
                if (!tmpLowPath_.empty())
                {
                    ::DeleteFileW(tmpLowPath_.c_str());
                }
                if (!tmpHighPath_.empty())
                {
                    ::DeleteFileW(tmpHighPath_.c_str());
                }
                pairOpen_ = false;
            }

            bool HasOpenPair() const override
            {
                return pairOpen_;
            }

        private:
            fs::path outputDirectory_;
            std::wstring presetName_;
            SimulationScenario scenario_;
            RunnerLogFile* logger_ = nullptr;
            WaveDataFileIO::StdFileWriter writerLow_;
            WaveDataFileIO::StdFileWriter writerHigh_;
            fs::path finalLowPath_;
            fs::path finalHighPath_;
            fs::path tmpLowPath_;
            fs::path tmpHighPath_;
            INT currentIndex_ = 0;
            INT openCallCount_ = 0;
            INT writeCallCount_ = 0;
            INT publishCallCount_ = 0;
            bool pairOpen_ = false;
        };

        fs::path BuildOutputDirectory(const std::wstring& repoRoot, const std::wstring& presetName)
        {
            SYSTEMTIME now = {};
            ::GetLocalTime(&now);

            wchar_t timestamp[64] = {};
            swprintf_s(
                timestamp,
                L"%04d%02d%02d_%02d%02d%02d_%03d",
                now.wYear,
                now.wMonth,
                now.wDay,
                now.wHour,
                now.wMinute,
                now.wSecond,
                now.wMilliseconds);

            return fs::path(repoRoot) / L"logs" / L"sim" / presetName / timestamp;
        }

        bool WriteSummaryJson(
            const fs::path& summaryPath,
            const std::wstring& presetName,
            int exitCode,
            const WaveAcquisition::AcquisitionSummary& summary)
        {
            std::ofstream output(summaryPath.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
            if (!output.is_open())
            {
                return false;
            }

            std::ostringstream json;
            json << "{\n";
            json << "  \"preset\": \"" << EscapeJsonString(WideToUtf8(presetName)) << "\",\n";
            json << "  \"terminal_status\": \"" << EscapeJsonString(WideToUtf8(WaveAcquisition::WaveAcquisitionEngine::ToString(summary.terminalStatus))) << "\",\n";
            json << "  \"error_code\": " << summary.errorCode << ",\n";
            json << "  \"exit_code\": " << exitCode << ",\n";
            json << "  \"ep6_call_count\": " << summary.metrics.ep6.callCount << ",\n";
            json << "  \"timeout_count\": " << summary.metrics.ep6TimeoutCount << ",\n";
            json << "  \"WAVE_WR_CNT\": " << summary.metrics.latestWaveWrCnt << ",\n";
            json << "  \"WAVE_RD_CNT\": " << summary.metrics.latestWaveRdCnt << ",\n";
            json << "  \"DDR_WR_END\": " << summary.metrics.latestDdrWrEnd << ",\n";
            json << "  \"DDR_RD_END\": " << summary.metrics.latestDdrRdEnd << ",\n";
            json << "  \"saved_wave_count\": " << summary.savedWaveCount << ",\n";
            json << "  \"published_pair_count\": " << summary.publishedPairCount << "\n";
            json << "}\n";
            output << json.str();
            return true;
        }

        fs::path BuildScenarioPath(const std::wstring& repoRoot, const std::wstring& presetName)
        {
            return fs::path(repoRoot) / L"data" / L"sim_scenarios" / (presetName + L".json");
        }
    }

    int ExitCodeFromStatus(WaveAcquisition::TerminalStatus status)
    {
        switch (status)
        {
        case WaveAcquisition::TerminalStatus::Success:
            return 0;
        case WaveAcquisition::TerminalStatus::Ep6Timeout:
            return 2;
        case WaveAcquisition::TerminalStatus::UsbDisconnect:
            return 3;
        case WaveAcquisition::TerminalStatus::PublishFailed:
            return 4;
        case WaveAcquisition::TerminalStatus::WriteFailed:
            return 5;
        case WaveAcquisition::TerminalStatus::OpenPairFailed:
            return 6;
        case WaveAcquisition::TerminalStatus::InvalidConfig:
            return 7;
        case WaveAcquisition::TerminalStatus::Stopped:
            return 8;
        case WaveAcquisition::TerminalStatus::Ep4ReadFailed:
            return 9;
        case WaveAcquisition::TerminalStatus::Ep6ReadFailed:
            return 10;
        case WaveAcquisition::TerminalStatus::AlignmentError:
            return 11;
        default:
            return 1;
        }
    }

    bool RunPreset(
        const std::wstring& repoRoot,
        const std::wstring& presetName,
        SimulationRunResult* outResult,
        std::wstring* outError)
    {
        if (outResult == nullptr)
        {
            if (outError != nullptr)
            {
                *outError = L"outResult is null";
            }
            return false;
        }

        const fs::path scenarioPath = BuildScenarioPath(repoRoot, presetName);
        SimulationScenario scenario = {};
        std::wstring loadError;
        if (!LoadScenarioFromFile(scenarioPath.wstring(), &scenario, &loadError))
        {
            if (outError != nullptr)
            {
                *outError = L"failed to load scenario: " + loadError;
            }
            return false;
        }
        scenario.presetName = presetName;

        const fs::path outputDirectory = BuildOutputDirectory(repoRoot, presetName);
        fs::create_directories(outputDirectory);

        RunnerLogFile logger;
        if (!logger.Init(outputDirectory))
        {
            if (outError != nullptr)
            {
                *outError = L"failed to initialize logger";
            }
            return false;
        }

        RunnerObserver observer(&logger);
        FakeUsbSession usbSession(scenario);
        ScriptedWavePairSink sink(outputDirectory, presetName, scenario, &logger);

        WaveAcquisition::RunConfig config = {};
        config.waveSizeLow = scenario.waveSizeLow;
        config.waveSizeHigh = scenario.waveSizeHigh;
        config.wavesPerFile = scenario.wavesPerFile;
        config.maxReadChunkBytes = scenario.maxReadChunkBytes;
        config.ep6TimeoutRetryLimit = scenario.timeoutRetryLimit;
        config.ep4PollSleepMs = 0;

        WaveAcquisition::WaveAcquisitionEngine engine(&usbSession, &sink, &observer, nullptr);
        const WaveAcquisition::AcquisitionSummary summary = engine.RunCycle(config);
        const int exitCode = ExitCodeFromStatus(summary.terminalStatus);

        const fs::path summaryPath = outputDirectory / L"summary.json";
        if (!WriteSummaryJson(summaryPath, presetName, exitCode, summary))
        {
            if (outError != nullptr)
            {
                *outError = L"failed to write summary.json";
            }
            return false;
        }

        outResult->exitCode = exitCode;
        outResult->outputDirectory = outputDirectory.wstring();
        outResult->runnerLogPath = logger.GetPath();
        outResult->summaryPath = summaryPath.wstring();
        outResult->summary = summary;
        return true;
    }
}
