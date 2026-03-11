#include "SimulationScenario.h"

#include <cwctype>
#include <fstream>
#include <regex>
#include <sstream>

#include "../AnalogBoard_TestApp/WaveAcquisitionEngine.h"

namespace SimRunner
{
    namespace
    {
        enum class NumberFieldMatch
        {
            Missing,
            Value,
            NegativeValue,
        };

        bool ReadFileText(const std::wstring& path, std::wstring* outText)
        {
            if (outText == nullptr)
            {
                return false;
            }

            std::ifstream input(path, std::ios::binary);
            if (!input.is_open())
            {
                return false;
            }

            std::ostringstream buffer;
            buffer << input.rdbuf();
            const std::string utf8 = buffer.str();
            if (utf8.empty())
            {
                outText->clear();
                return true;
            }

            const int required = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
            if (required <= 0)
            {
                return false;
            }

            std::wstring wideText(static_cast<size_t>(required), L'\0');
            if (::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &wideText[0], required) <= 0)
            {
                return false;
            }

            *outText = wideText;
            return true;
        }

        NumberFieldMatch FindNumberToken(
            const std::wstring& json,
            const wchar_t* fieldName,
            std::wstring* outToken)
        {
            if (fieldName == nullptr || outToken == nullptr)
            {
                return NumberFieldMatch::Missing;
            }

            const std::wstring pattern = L"\"" + std::wstring(fieldName) + L"\"\\s*:\\s*(-?[0-9]+)";
            std::wregex regex(pattern);
            std::wsmatch match;
            if (!std::regex_search(json, match, regex) || match.size() < 2)
            {
                return NumberFieldMatch::Missing;
            }

            *outToken = match[1].str();
            if (!outToken->empty() && (*outToken)[0] == L'-')
            {
                return NumberFieldMatch::NegativeValue;
            }

            return NumberFieldMatch::Value;
        }

        NumberFieldMatch FindUnsignedField(const std::wstring& json, const wchar_t* fieldName, ULONG* outValue)
        {
            if (outValue == nullptr)
            {
                return NumberFieldMatch::Missing;
            }

            std::wstring token;
            const NumberFieldMatch match = FindNumberToken(json, fieldName, &token);
            if (match != NumberFieldMatch::Value)
            {
                return match;
            }

            *outValue = static_cast<ULONG>(std::stoul(token));
            return NumberFieldMatch::Value;
        }

        bool FindIntField(const std::wstring& json, const wchar_t* fieldName, INT* outValue)
        {
            if (outValue == nullptr)
            {
                return false;
            }

            std::wstring token;
            if (FindNumberToken(json, fieldName, &token) == NumberFieldMatch::Missing)
            {
                return false;
            }

            *outValue = static_cast<INT>(std::stoi(token));
            return true;
        }

        bool FindStringArrayField(
            const std::wstring& json,
            const wchar_t* fieldName,
            std::vector<std::wstring>* outValues)
        {
            if (fieldName == nullptr || outValues == nullptr)
            {
                return false;
            }

            const std::wstring pattern = L"\"" + std::wstring(fieldName) + L"\"\\s*:\\s*\\[(.*?)\\]";
            std::wregex regex(pattern);
            std::wsmatch match;
            if (!std::regex_search(json, match, regex) || match.size() < 2)
            {
                return false;
            }

            const std::wstring rawValues = match[1].str();
            std::wregex itemRegex(L"\"([^\"]+)\"");
            auto begin = std::wsregex_iterator(rawValues.begin(), rawValues.end(), itemRegex);
            auto end = std::wsregex_iterator();
            for (auto it = begin; it != end; ++it)
            {
                outValues->push_back((*it)[1].str());
            }

            return !outValues->empty();
        }

        bool TryParseEp6Result(const std::wstring& token, Ep6ResultKind* outValue)
        {
            if (outValue == nullptr)
            {
                return false;
            }

            std::wstring lower = token;
            for (wchar_t& ch : lower)
            {
                ch = static_cast<wchar_t>(std::towlower(ch));
            }

            if (lower == L"success")
            {
                *outValue = Ep6ResultKind::Success;
                return true;
            }
            if (lower == L"timeout")
            {
                *outValue = Ep6ResultKind::Timeout;
                return true;
            }
            if (lower == L"disconnect")
            {
                *outValue = Ep6ResultKind::Disconnect;
                return true;
            }

            return false;
        }

        bool FailValidation(const std::wstring& message, std::wstring* outError)
        {
            if (outError != nullptr)
            {
                *outError = message;
            }

            return false;
        }

        bool ValidateScenario(const SimulationScenario& scenario, std::wstring* outError)
        {
            const ULONGLONG oneWaveSize =
                static_cast<ULONGLONG>(scenario.waveSizeLow) +
                static_cast<ULONGLONG>(scenario.waveSizeHigh);

            if (oneWaveSize == 0)
            {
                return FailValidation(L"wave sizes must not both be zero", outError);
            }

            if (scenario.wavesPerFile == 0)
            {
                return FailValidation(L"waves_per_file must be greater than zero", outError);
            }

            if (scenario.totalWaveCount == 0)
            {
                return FailValidation(L"total_wave_count must be greater than zero", outError);
            }

            if (scenario.maxReadChunkBytes == 0)
            {
                return FailValidation(L"max_read_chunk_bytes must be greater than zero", outError);
            }

            if ((scenario.maxReadChunkBytes % static_cast<ULONG>(WaveAcquisition::kEp6ReadAlignmentBytes)) != 0)
            {
                return FailValidation(L"max_read_chunk_bytes must align to EP6 reads", outError);
            }

            const ULONGLONG totalLogicalBytes = oneWaveSize * static_cast<ULONGLONG>(scenario.totalWaveCount);
            if (scenario.producerStepBytes != 0 && scenario.producerBurstsPerPoll != 0)
            {
                return FailValidation(L"producer_step_bytes and producer_bursts_per_poll cannot both be set", outError);
            }

            if (scenario.producerStepBytes != 0 &&
                static_cast<ULONGLONG>(scenario.producerStepBytes) < totalLogicalBytes &&
                (scenario.producerStepBytes % static_cast<ULONG>(WaveAcquisition::kEp6ReadAlignmentBytes)) != 0)
            {
                return FailValidation(L"producer_step_bytes must align to EP6 reads for multi-step scenarios", outError);
            }

            return true;
        }
    }

    bool LoadScenarioFromFile(
        const std::wstring& path,
        SimulationScenario* outScenario,
        std::wstring* outError)
    {
        if (outScenario == nullptr)
        {
            if (outError != nullptr)
            {
                *outError = L"outScenario is null";
            }
            return false;
        }

        std::wstring json;
        if (!ReadFileText(path, &json))
        {
            if (outError != nullptr)
            {
                *outError = L"failed to read scenario file";
            }
            return false;
        }

        SimulationScenario scenario = {};
        auto LoadRequiredUnsignedField = [&](const wchar_t* fieldName, ULONG* outValue) -> bool
        {
            const NumberFieldMatch match = FindUnsignedField(json, fieldName, outValue);
            if (match == NumberFieldMatch::Value)
            {
                return true;
            }

            if (outError != nullptr)
            {
                if (match == NumberFieldMatch::NegativeValue)
                {
                    *outError = std::wstring(fieldName) + L" must not be negative";
                }
                else
                {
                    *outError = L"missing " + std::wstring(fieldName);
                }
            }

            return false;
        };

        auto LoadOptionalUnsignedField = [&](const wchar_t* fieldName, ULONG* outValue) -> bool
        {
            const NumberFieldMatch match = FindUnsignedField(json, fieldName, outValue);
            if (match == NumberFieldMatch::Value)
            {
                return true;
            }

            if (match == NumberFieldMatch::Missing)
            {
                return true;
            }

            if (outError != nullptr)
            {
                *outError = std::wstring(fieldName) + L" must not be negative";
            }

            return false;
        };

        if (!LoadRequiredUnsignedField(L"wave_size_low", &scenario.waveSizeLow))
        {
            return false;
        }

        if (!LoadRequiredUnsignedField(L"wave_size_high", &scenario.waveSizeHigh))
        {
            return false;
        }

        if (!LoadRequiredUnsignedField(L"waves_per_file", &scenario.wavesPerFile))
        {
            return false;
        }

        if (!LoadRequiredUnsignedField(L"total_wave_count", &scenario.totalWaveCount))
        {
            return false;
        }

        if (!LoadOptionalUnsignedField(L"producer_step_bytes", &scenario.producerStepBytes))
        {
            return false;
        }

        if (!LoadOptionalUnsignedField(L"producer_bursts_per_poll", &scenario.producerBurstsPerPoll))
        {
            return false;
        }

        if (FindIntField(json, L"init_poll_count", &scenario.initPollCount))
        {
            if (scenario.initPollCount < 0)
            {
                if (outError != nullptr)
                {
                    *outError = L"init_poll_count must be non-negative";
                }
                return false;
            }
        }

        if (FindIntField(json, L"wait_poll_count", &scenario.waitPollCount))
        {
            if (scenario.waitPollCount < 0)
            {
                if (outError != nullptr)
                {
                    *outError = L"wait_poll_count must be non-negative";
                }
                return false;
            }
        }

        if (!LoadRequiredUnsignedField(L"max_read_chunk_bytes", &scenario.maxReadChunkBytes))
        {
            return false;
        }

        if (!FindIntField(json, L"timeout_retry_limit", &scenario.timeoutRetryLimit))
        {
            if (outError != nullptr)
            {
                *outError = L"missing timeout_retry_limit";
            }
            return false;
        }

        if (!LoadRequiredUnsignedField(L"write_delay_ms", &scenario.writeDelayMs))
        {
            return false;
        }

        if (!FindIntField(json, L"write_fail_at", &scenario.writeFailAt))
        {
            if (outError != nullptr)
            {
                *outError = L"missing write_fail_at";
            }
            return false;
        }

        if (!FindIntField(json, L"publish_fail_at", &scenario.publishFailAt))
        {
            if (outError != nullptr)
            {
                *outError = L"missing publish_fail_at";
            }
            return false;
        }

        std::vector<std::wstring> ep6Tokens;
        if (!FindStringArrayField(json, L"ep6_results", &ep6Tokens))
        {
            if (outError != nullptr)
            {
                *outError = L"missing ep6_results";
            }
            return false;
        }

        for (const std::wstring& token : ep6Tokens)
        {
            Ep6ResultKind resultKind = Ep6ResultKind::Success;
            if (!TryParseEp6Result(token, &resultKind))
            {
                if (outError != nullptr)
                {
                    *outError = L"invalid ep6_results value: " + token;
                }
                return false;
            }

            scenario.ep6Results.push_back(resultKind);
        }

        if (!ValidateScenario(scenario, outError))
        {
            return false;
        }

        *outScenario = scenario;
        return true;
    }
}
