#if !defined(_RSYS_VERSION_H_)
#define _RSYS_VERSION_H_

#ifdef __cplusplus
extern "C" {
#endif

extern const char ROMlib_executor_version[];
extern const char *ROMlib_executor_full_name;

extern void ROMlib_set_system_version(uint32_t version);

#ifdef __cplusplus
}
#endif

#endif /* !_RSYS_VERSION_H_ */
