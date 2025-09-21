#include "core/mali_wrapper_icd.hpp"
#include "utils/logging.hpp"

__attribute__((constructor))
void mali_wrapper_init() {
    LOG_INFO("Mali Wrapper ICD library loaded");
}

__attribute__((destructor))
void mali_wrapper_cleanup() {
    LOG_INFO("Mali Wrapper ICD library unloaded");
    mali_wrapper::ShutdownWrapper();
}