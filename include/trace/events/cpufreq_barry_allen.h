#undef TRACE_SYSTEM
#define TRACE_SYSTEM cpufreq_barry_allen

#if !defined(_TRACE_CPUFREQ_BARRY_ALLEN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CPUFREQ_BARRY_ALLEN_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(set,
	TP_PROTO(u32 cpu_id, unsigned long targfreq,
	         unsigned long actualfreq),
	TP_ARGS(cpu_id, targfreq, actualfreq),

	TP_STRUCT__entry(
	    __field(          u32, cpu_id    )
	    __field(unsigned long, targfreq   )
	    __field(unsigned long, actualfreq )
	   ),

	TP_fast_assign(
	    __entry->cpu_id = (u32) cpu_id;
	    __entry->targfreq = targfreq;
	    __entry->actualfreq = actualfreq;
	),

	TP_printk("cpu=%u targ=%lu actual=%lu",
	      __entry->cpu_id, __entry->targfreq,
	      __entry->actualfreq)
);

DEFINE_EVENT(set, cpufreq_barry_allen_setspeed,
	TP_PROTO(u32 cpu_id, unsigned long targfreq,
	     unsigned long actualfreq),
	TP_ARGS(cpu_id, targfreq, actualfreq)
);

DECLARE_EVENT_CLASS(loadeval,
	    TP_PROTO(unsigned long cpu_id, unsigned long load,
		     unsigned long curtarg, unsigned long curactual,
		     unsigned long newtarg),
		    TP_ARGS(cpu_id, load, curtarg, curactual, newtarg),

	    TP_STRUCT__entry(
		    __field(unsigned long, cpu_id    )
		    __field(unsigned long, load      )
		    __field(unsigned long, curtarg   )
		    __field(unsigned long, curactual )
		    __field(unsigned long, newtarg   )
	    ),

	    TP_fast_assign(
		    __entry->cpu_id = cpu_id;
		    __entry->load = load;
		    __entry->curtarg = curtarg;
		    __entry->curactual = curactual;
		    __entry->newtarg = newtarg;
	    ),

	    TP_printk("cpu=%lu load=%lu cur=%lu actual=%lu targ=%lu",
		      __entry->cpu_id, __entry->load, __entry->curtarg,
		      __entry->curactual, __entry->newtarg)
);

DEFINE_EVENT(loadeval, cpufreq_barry_allen_target,
	    TP_PROTO(unsigned long cpu_id, unsigned long load,
		     unsigned long curtarg, unsigned long curactual,
		     unsigned long newtarg),
	    TP_ARGS(cpu_id, load, curtarg, curactual, newtarg)
);

DEFINE_EVENT(loadeval, cpufreq_barry_allen_already,
	    TP_PROTO(unsigned long cpu_id, unsigned long load,
		     unsigned long curtarg, unsigned long curactual,
		     unsigned long newtarg),
	    TP_ARGS(cpu_id, load, curtarg, curactual, newtarg)
);

DEFINE_EVENT(loadeval, cpufreq_barry_allen_notyet,
	    TP_PROTO(unsigned long cpu_id, unsigned long load,
		     unsigned long curtarg, unsigned long curactual,
		     unsigned long newtarg),
	    TP_ARGS(cpu_id, load, curtarg, curactual, newtarg)
);

DECLARE_EVENT_CLASS(modeeval,
	    TP_PROTO(unsigned long cpu_id, unsigned long total_load,
		     unsigned long single_enter, unsigned long multi_enter,
		     unsigned long single_exit, unsigned long multi_exit, unsigned long mode),
		    TP_ARGS(cpu_id, total_load, single_enter, multi_enter, single_exit, multi_exit, mode),

	    TP_STRUCT__entry(
		    __field(unsigned long, cpu_id      )
		    __field(unsigned long, total_load  )
		    __field(unsigned long, single_enter)
		    __field(unsigned long, multi_enter )
		    __field(unsigned long, single_exit )
		    __field(unsigned long, multi_exit  )
		    __field(unsigned long, mode)
	    ),

	    TP_fast_assign(
		    __entry->cpu_id = cpu_id;
		    __entry->total_load = total_load;
		    __entry->single_enter = single_enter;
		    __entry->multi_enter = multi_enter;
		    __entry->single_exit = single_exit;
		    __entry->multi_exit = multi_exit;
		    __entry->mode = mode ;
	    ),

	    TP_printk("cpu=%lu load=%3lu s_en=%6lu m_en=%6lu s_ex=%6lu m_ex=%6lu ret=%lu",
		      __entry->cpu_id, __entry->total_load, __entry->single_enter,
		      __entry->multi_enter, __entry->single_exit, __entry->multi_exit, __entry->mode)
);

DEFINE_EVENT(modeeval, cpufreq_barry_allen_mode,
	    TP_PROTO(unsigned long cpu_id, unsigned long total_load,
		     unsigned long single_enter, unsigned long multi_enter,
		     unsigned long single_exit, unsigned long multi_exit, unsigned long mode),
	    TP_ARGS(cpu_id, total_load, single_enter, multi_enter, single_exit, multi_exit, mode)
);

TRACE_EVENT(cpufreq_barry_allen_boost,
	    TP_PROTO(const char *s),
	    TP_ARGS(s),
	    TP_STRUCT__entry(
		    __string(s, s)
	    ),
	    TP_fast_assign(
		    __assign_str(s, s);
	    ),
	    TP_printk("%s", __get_str(s))
);

TRACE_EVENT(cpufreq_barry_allen_unboost,
	    TP_PROTO(const char *s),
	    TP_ARGS(s),
	    TP_STRUCT__entry(
		    __string(s, s)
	    ),
	    TP_fast_assign(
		    __assign_str(s, s);
	    ),
	    TP_printk("%s", __get_str(s))
);

#endif /* _TRACE_CPUFREQ_BARRY_ALLEN_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

