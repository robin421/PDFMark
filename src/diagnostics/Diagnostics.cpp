// PDFMark - Diagnostics implementation.
#include "diagnostics/Diagnostics.h"

#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstring>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <psapi.h>
#  pragma comment(lib, "psapi.lib")
#else
#  include <sys/resource.h>
#  include <sys/sysctl.h>
#  include <unistd.h>
#  ifdef __APPLE__
#    include <mach/mach.h>
#  endif
#endif

namespace pdfmark {

long long MemoryProbe::peakPhysicalBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS info;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info))) {
        return static_cast<long long>(info.PeakWorkingSetSize);
    }
    return 0;
#elif defined(__APPLE__)
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<long long>(info.resident_size_max);
    }
    return 0;
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return static_cast<long long>(usage.ru_maxrss) * 1024;
    }
    return 0;
#endif
}

long long MemoryProbe::currentPhysicalBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS info;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info))) {
        return static_cast<long long>(info.WorkingSetSize);
    }
    return 0;
#elif defined(__APPLE__)
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<long long>(info.resident_size);
    }
    return 0;
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return static_cast<long long>(usage.ru_ixrss + usage.ru_idrss + usage.ru_isrss);
    }
    return 0;
#endif
}

static std::string formatBytes(long long bytes) {
    if (bytes <= 0) return "0 B";
    constexpr long long KB = 1024;
    constexpr long long MB = KB * 1024;
    constexpr long long GB = MB * 1024;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    if (bytes >= GB) oss << (static_cast<double>(bytes) / GB) << " GB";
    else if (bytes >= MB) oss << (static_cast<double>(bytes) / MB) << " MB";
    else if (bytes >= KB) oss << (static_cast<double>(bytes) / KB) << " KB";
    else oss << bytes << " B";
    return oss.str();
}

std::string currentTimestampForFilename() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return oss.str();
}

std::string DiagnosticsReport::toFormattedText() const {
    std::ostringstream oss;
    oss << "PDFMark Diagnostics Report\n";
    oss << "=================================\n";
    oss << "Timestamp        : " << timestamp << "\n";
    oss << "OS               : " << osName << "\n";
    oss << "Logical CPUs     : " << cpuLogicalCores << "\n";
    oss << "Total RAM        : " << formatBytes(totalRamBytes) << "\n";
    oss << "Peak Memory Used : " << formatBytes(peakMemoryBytes) << "\n";
    oss << "Total Pages      : " << totalPages << "\n";
    oss << "Processed Pages  : " << processedPages << "\n";
    oss << "Failed Pages     : " << failedPages << "\n";
    oss << "Total Elapsed    : " << std::fixed << std::setprecision(2) << totalElapsedMs << " ms\n";
    oss << "Avg Page Time    : " << std::fixed << std::setprecision(2) << avgPageMs << " ms\n";
    oss << "Input Path       : " << inputPath << "\n";
    oss << "Output Path      : " << outputPath << "\n";
    oss << "Watermark Text   : " << watermarkText << "\n";
    oss << "=================================\n";
    return oss.str();
}

} // namespace pdfmark