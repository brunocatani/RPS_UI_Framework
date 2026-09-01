#pragma once

#include <filesystem>
#include <memory>
#include <utility>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

namespace rpsui::log
{
    inline std::shared_ptr<spdlog::logger> instance;

    inline bool initialize() noexcept
    {
        try {
            auto directory = F4SE::log::log_directory();
            if (!directory) {
                return false;
            }
            const std::string_view expected =
                REL::Module::IsVR() ? "Fallout4VR/F4SE" : "Fallout4/F4SE";
            if (!directory->generic_string().ends_with(expected)) {
                directory = directory->parent_path().append(expected);
            }
            std::error_code error;
            std::filesystem::create_directories(*directory, error);
            if (error) {
                return false;
            }
            *directory /= "RPS_UI_Framework.log";
            auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                directory->string(), 5 * 1024 * 1024, 3, true);
            instance = std::make_shared<spdlog::logger>(
                "RPS_UI_Framework", std::move(sink));
            instance->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");
            instance->set_level(spdlog::level::info);
            instance->flush_on(spdlog::level::info);
            spdlog::set_default_logger(instance);
            return true;
        } catch (...) {
            return false;
        }
    }

    template <class... Args>
    void info(spdlog::format_string_t<Args...> format, Args&&... args) noexcept
    {
        try {
            if (instance) instance->info(format, std::forward<Args>(args)...);
        } catch (...) {}
    }

    template <class... Args>
    void warn(spdlog::format_string_t<Args...> format, Args&&... args) noexcept
    {
        try {
            if (instance) instance->warn(format, std::forward<Args>(args)...);
        } catch (...) {}
    }

    template <class... Args>
    void error(spdlog::format_string_t<Args...> format, Args&&... args) noexcept
    {
        try {
            if (instance) instance->error(format, std::forward<Args>(args)...);
        } catch (...) {}
    }

    template <class... Args>
    void critical(spdlog::format_string_t<Args...> format, Args&&... args) noexcept
    {
        try {
            if (instance) instance->critical(format, std::forward<Args>(args)...);
        } catch (...) {}
    }
}
