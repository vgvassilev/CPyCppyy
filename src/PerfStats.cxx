// Bindings
#include "PerfStats.h"

// Standard
#include <string>
#include <vector>

namespace CPyCppyy {

namespace {
struct PerfSection {
    std::string name;
    PerfReporter report;
    PerfResetter reset;
};

// Function-local-static avoids the static-initializer-order fiasco when
// subsystem initializers (Dispatcher.cxx, future JIT-emit cache, ...) call
// RegisterPerfSection from translation-unit-scope statics of their own.
std::vector<PerfSection>& Sections() {
    static std::vector<PerfSection> v;
    return v;
}

const PerfSection* FindSection(const char* name) {
    for (auto& s : Sections())
        if (s.name == name)
            return &s;
    return nullptr;
}
} // anonymous

void RegisterPerfSection(const char* name,
                         PerfReporter report,
                         PerfResetter reset) {
    Sections().push_back({name, report, reset});
}

PyObject* PerfStats(PyObject*, PyObject* args)
{
    const char* section_name = nullptr;
    if (!PyArg_ParseTuple(args, "|s:perf_stats", &section_name))
        return nullptr;

    if (section_name) {
        const PerfSection* s = FindSection(section_name);
        if (!s) {
            PyErr_Format(PyExc_KeyError,
                "no perf-stats section named '%s'", section_name);
            return nullptr;
        }
        return s->report();
    }

    PyObject* all = PyDict_New();
    for (auto& s : Sections()) {
        PyObject* sub = s.report();
        if (sub) {
            PyDict_SetItemString(all, s.name.c_str(), sub);
            Py_DECREF(sub);
        }
    }
    return all;
}

PyObject* PerfStatsReset(PyObject*, PyObject* args)
{
    const char* section_name = nullptr;
    if (!PyArg_ParseTuple(args, "|s:perf_stats_reset", &section_name))
        return nullptr;

    if (section_name) {
        const PerfSection* s = FindSection(section_name);
        if (!s) {
            PyErr_Format(PyExc_KeyError,
                "no perf-stats section named '%s'", section_name);
            return nullptr;
        }
        s->reset();
        Py_RETURN_NONE;
    }

    for (auto& s : Sections())
        s.reset();
    Py_RETURN_NONE;
}

} // namespace CPyCppyy
