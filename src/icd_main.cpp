#include "core/mali_wrapper_icd.hpp"
#include "utils/logging.hpp"

// Library constructor - called when the library is loaded
__attribute__((constructor))
void mali_wrapper_init() {
    mali_wrapper::Logger::Instance().SetLevel(mali_wrapper::LogLevel::ERROR);
    LOG_INFO("Mali Wrapper ICD library loaded");
}

// Library destructor - called when the library is unloaded
__attribute__((destructor))
void mali_wrapper_cleanup() {
    LOG_INFO("Mali Wrapper ICD library unloaded");
    mali_wrapper::ShutdownWrapper();
}