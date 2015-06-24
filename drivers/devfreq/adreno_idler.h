#ifndef _ADRENO_IDLER_H
#define _ADRENO_IDLER_H

extern bool adreno_idler_active;
extern int adreno_idler(struct devfreq_dev_status stats, struct devfreq *devfreq,
		 unsigned long *freq);

#endif /* _ADRENO_IDLER_H */
