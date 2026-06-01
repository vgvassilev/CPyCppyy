#ifndef CPYCPPYY_DISPATCHER_H
#define CPYCPPYY_DISPATCHER_H

// Standard
#include <sstream>

namespace CPyCppyy {

class CPPScope;

// helper that inserts dispatchers for virtual methods
bool InsertDispatcher(CPPScope* klass, PyObject* bases, PyObject* dct, std::ostringstream& err);

// Fast-path detector for the synthesized-vtable dispatcher. Returns
// true when the (base, methods, overrides) shape is in the prebuilt
// thunk table supported by Cpp::MakeVTableProxy; false otherwise.
// Today's thunk table: single inheritance, exactly one `int(int)`
// override. Caller falls back to InsertDispatcher's string-template
// path when this returns false.
bool TryInsertVTableProxyDispatcher(CPPScope* klass, PyObject* bases, PyObject* dct);

// Observability of the detector across a workload. Counts total
// InsertDispatcher calls, fast-path-eligible ones, and miss reasons,
// plus a small ring of the most recent overridden-method signatures
// for shaping the thunk-table-expansion priority list.
PyObject* VTableProxyStats(PyObject*, PyObject*);
PyObject* VTableProxyStatsReset(PyObject*, PyObject*);

} // namespace CPyCppyy

#endif // !CPYCPPYY_DISPATCHER_H
