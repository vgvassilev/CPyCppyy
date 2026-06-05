#ifndef CPYCPPYY_DISPATCHER_H
#define CPYCPPYY_DISPATCHER_H

// Standard
#include <sstream>

namespace CPyCppyy {

class CPPScope;

// helper that inserts dispatchers for virtual methods
bool InsertDispatcher(CPPScope* klass, PyObject* bases, PyObject* dct, std::ostringstream& err);

// Catalog-eligibility gate for the fast-path vtable overlay. Returns true
// when every Python override matches a signature shape known to the
// PyVirtualHandler thunk catalog; otherwise the slow-path (Cling-emitted
// dispatcher) is used. Counters surfaced via the "vtable_proxy" section of
// the cppyy.perf_stats() registry (see Dispatcher.cxx + PerfStats.h).
bool TryInsertVTableProxyDispatcher(CPPScope* klass, PyObject* bases, PyObject* dct);

} // namespace CPyCppyy

#endif // !CPYCPPYY_DISPATCHER_H
