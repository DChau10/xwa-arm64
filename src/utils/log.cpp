#include "log.h"
#include "bytes.h"
#include "../compat.h"

#include <sstream>
#include <iomanip>

namespace Log
{
    std::string formatBytes(const Bytes &bytes)
    {
        std::ostringstream stream;

        stream << std::hex << std::setfill('0');

        for (uint8_t byte : bytes)
        {
            stream << std::setw(2);
            stream << static_cast<uint32_t>(byte) << ':';
        }

        std::string output = stream.str();

        // Remove trailing colon
        if (!output.empty())
        {
            output.pop_back();
        }

        return output;
    }

    std::string formatLog(std::string level, std::string message)
    {
        std::ostringstream stream;
        std::time_t time = std::time(nullptr);
        std::tm localTime = {};

        // Add local time to output if available
        if (portable_localtime(&time, &localTime))
        {
            stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << " ";
        }

        stream << std::left << std::setw(5);
        stream << level << " - ";
        stream << message << std::endl;

        return stream.str();
    }
}
