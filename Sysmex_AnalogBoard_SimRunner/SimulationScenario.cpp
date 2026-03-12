#include "SimulationScenario.h"

#include <cwctype>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>

#include "../AnalogBoard_TestApp/FpgaRegisterEncoding.h"
#include "../AnalogBoard_TestApp/WaveAcquisitionEngine.h"

namespace SimRunner
{
    namespace
    {
        enum class NumericFieldStatus
        {
            Missing,
            Value,
            NegativeValue,
            OutOfRange,
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

        bool FindNumberToken(
            const std::wstring& json,
            const wchar_t* fieldName,
            std::wstring* outToken)
        {
            if (fieldName == nullptr || outToken == nullptr)
            {
                return false;
            }

            const std::wstring pattern = L"\"" + std::wstring(fieldName) + L"\"\\s*:\\s*(-?[0-9]+)";
            std::wregex regex(pattern);
            std::wsmatch match;
            if (!std::regex_search(json, match, regex) || match.size() < 2)
            {
                return false;
            }

            *outToken = match[1].str();
            return true;
        }

        NumericFieldStatus FindUnsignedField(const std::wstring& json, const wchar_t* fieldName, ULONG* outValue)
        {
            if (outValue == nullptr)
            {
                return NumericFieldStatus::Missing;
            }

            std::wstring token;
            if (!FindNumberToken(json, fieldName, &token))
            {
                return NumericFieldStatus::Missing;
            }

            if (!token.empty() && token[0] == L'-')
            {
                return NumericFieldStatus::NegativeValue;
            }

            try
            {
                const unsigned long long parsed = std::stoull(token);
                if (parsed > static_cast<unsigned long long>((std::numeric_limits<ULONG>::max)()))
                {
                    return NumericFieldStatus::OutOfRange;
                }

                *outValue = static_cast<ULONG>(parsed);
            }
            catch (const std::out_of_range&)
            {
                return NumericFieldStatus::OutOfRange;
            }
            catch (const std::exception&)
            {
                return NumericFieldStatus::OutOfRange;
            }

            return NumericFieldStatus::Value;
        }

        NumericFieldStatus FindIntField(const std::wstring& json, const wchar_t* fieldName, INT* outValue)
        {
            if (outValue == nullptr)
            {
                return NumericFieldStatus::Missing;
            }

            std::wstring token;
            if (!FindNumberToken(json, fieldName, &token))
            {
                return NumericFieldStatus::Missing;
            }

            try
            {
                const long long parsed = std::stoll(token);
                if (parsed < static_cast<long long>((std::numeric_limits<INT>::min)()) ||
                    parsed > static_cast<long long>((std::numeric_limits<INT>::max)()))
                {
                    return NumericFieldStatus::OutOfRange;
                }

                *outValue = static_cast<INT>(parsed);
            }
            catch (const std::out_of_range&)
            {
                return NumericFieldStatus::OutOfRange;
            }
            catch (const std::exception&)
            {
                return NumericFieldStatus::OutOfRange;
            }

            return NumericFieldStatus::Value;
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

            const std::wstring pattern = L"\"" + std::wstring(fieldName) + L"\"\\s*:\\s*\\[([\\s\\S]*?)\\]";
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

            if (scenario.timeoutRetryLimit < 0)
            {
                return FailValidation(L"timeout_retry_limit must be non-negative", outError);
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
            if (totalLogicalBytes > static_cast<ULONGLONG>((std::numeric_limits<ULONG>::max)()))
            {
                return FailValidation(L"total logical bytes exceed supported range", outError);
            }

            if (scenario.producerStepBytes != 0 && scenario.producerBurstsPerPoll != 0)
            {
                return FailValidation(L"producer_step_bytes and producer_bursts_per_poll cannot both be set", outError);
            }

            if (scenario.producerStepBytes != 0 &&
                (scenario.producerStepBytes % FpgaRegEncoding::kDdrAddressUnitBytes) != 0)
            {
                return FailValidation(L"producer_step_bytes must align to the 32-byte DDR address unit", outError);
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
            const NumericFieldStatus status = FindUnsignedField(json, fieldName, outValue);
            if (status == NumericFieldStatus::Value)
            {
                return true;
            }

            if (outError != nullptr)
            {
                if (status == NumericFieldStatus::NegativeValue)
                {
                    *outError = std::wstring(fieldName) + L" must not be negative";
                }
                else if (status == NumericFieldStatus::OutOfRange)
                {
                    *outError = std::wstring(fieldName) + L" is out of range";
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
            const NumericFieldStatus status = FindUnsignedField(json, fieldName, outValue);
            if (status == NumericFieldStatus::Value)
            {
                return true;
            }

            if (status == NumericFieldStatus::Missing)
            {
                return true;
            }

            if (outError != nullptr)
            {
                if (status == NumericFieldStatus::NegativeValue)
                {
                    *outError = std::wstring(fieldName) + L" must not be negative";
                }
                else
                {
                    *outError = std::wstring(fieldName) + L" is out of range";
                }
            }

            return false;
        };

        auto LoadOptionalIntField = [&](const wchar_t* fieldName, INT* outValue) -> bool
        {
            const NumericFieldStatus status = FindIntField(json, fieldName, outValue);
            if (status == NumericFieldStatus::Value)
            {
                return true;
            }

            if (status == NumericFieldStatus::Missing)
            {
                return true;
            }

            if (outError != nullptr)
            {
                *outError = std::wstring(fieldName) + L" is out of range";
            }

            return false;
        };

        auto LoadRequiredIntField = [&](const wchar_t* fieldName, INT* outValue) -> bool
        {
            const NumericFieldStatus status = FindIntField(json, fieldName, outValue);
            if (status == NumericFieldStatus::Value)
            {
                return true;
            }

            if (outError != nullptr)
            {
                if (status == NumericFieldStatus::OutOfRange)
                {
                    *outError = std::wstring(fieldName) + L" is out of range";
                }
                else
                {
                    *outError = L"missing " + std::wstring(fieldName);
                }
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

        if (!LoadOptionalIntField(L"init_poll_count", &scenario.initPollCount))
        {
            return false;
        }

        if (FindIntField(json, L"init_poll_count", &scenario.initPollCount) == NumericFieldStatus::Value)
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

        if (!LoadOptionalIntField(L"wait_poll_count", &scenario.waitPollCount))
        {
            return false;
        }

        if (FindIntField(json, L"wait_poll_count", &scenario.waitPollCount) == NumericFieldStatus::Value)
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

        if (!LoadRequiredIntField(L"timeout_retry_limit", &scenario.timeoutRetryLimit))
        {
            return false;
        }

        if (!LoadOptionalUnsignedField(L"write_delay_ms", &scenario.writeDelayMs))
        {
            return false;
        }

        if (!LoadOptionalIntField(L"write_fail_at", &scenario.writeFailAt))
        {
            return false;
        }

        if (!LoadOptionalIntField(L"publish_fail_at", &scenario.publishFailAt))
        {
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
