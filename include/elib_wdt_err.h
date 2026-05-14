/* elib_wdt_err.h - Software Watchdog Error Codes */
#ifndef ELIB_WDT_ERR_H
#define ELIB_WDT_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ELIB_WDT_OK = 0,
    ELIB_WDT_ERR_INVALID_PARAM,
    ELIB_WDT_ERR_NOT_INITIALIZED,
    ELIB_WDT_ERR_ALREADY_INITIALIZED,
    ELIB_WDT_ERR_FULL,
    ELIB_WDT_ERR_ALREADY_REGISTERED,
    ELIB_WDT_ERR_NOT_FOUND,
    ELIB_WDT_ERR_NOT_RUNNING,
} elib_wdt_err_t;

#ifdef __cplusplus
}
#endif

#endif /* ELIB_WDT_ERR_H */
