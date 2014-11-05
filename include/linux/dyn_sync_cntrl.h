/* 
 * Dynamic sync control driver definitions
 * 
 * by andip71 (alias Lord Boeffla)
 * 
 */

#define DYN_FSYNC_ACTIVE_DEFAULT	false
#define DYN_FSYNC_VERSION_MAJOR 	1
#define DYN_FSYNC_VERSION_MINOR 	7

extern bool suspend_active;
extern bool dyn_fsync_active;

extern void dyn_fsync_resume(void);
extern void dyn_fsync_suspend(void);
