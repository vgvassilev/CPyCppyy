#ifndef CPYCPPYY_PERFSTATS_H
#define CPYCPPYY_PERFSTATS_H

// Bindings
#include "CPyCppyy.h"

namespace CPyCppyy {

// Generic registry for performance / observability counters. Subsystems
// (catalog-eligibility, dispatcher inventory, JIT-emit cache, ...) each
// register a named section reporting their own counters into a Python
// dict. Exposed to user code via cppyy.perf_stats() / .perf_stats_reset().
//
// Static-init safe: sections register via function-local-static vector,
// so the order of subsystem initializers does not matter.

using PerfReporter = PyObject* (*)();   // returns a new dict for the section
using PerfResetter = void (*)();        // resets the section's counters

void RegisterPerfSection(const char* name,
                         PerfReporter report,
                         PerfResetter reset);

// Python module entry points: (section=None) -> dict, or dict-of-sections.
PyObject* PerfStats(PyObject*, PyObject* args);
PyObject* PerfStatsReset(PyObject*, PyObject* args);

} // namespace CPyCppyy

#endif // !CPYCPPYY_PERFSTATS_H
