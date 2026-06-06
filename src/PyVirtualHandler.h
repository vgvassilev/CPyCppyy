// Cppyy-tuned per-instance handler for Phase 1 VTableOverlay dispatch.
// The handler struct itself is private to PyVirtualHandler.cxx; only the
// demo entry point + opaque create/destroy hooks are exposed here so this
// header doesn't have to pull in CppInterOp headers (which collide with
// cppyy's own `TemplateArgInfo` in mainstream cppyy TUs).

#ifndef CPYCPPYY_PYVIRTUALHANDLER_H
#define CPYCPPYY_PYVIRTUALHANDLER_H

#include <Python.h>

#include <string>
#include <vector>

namespace CPyCppyy {

// Opaque handle to the per-instance handler; created by CreatePyHandler
// and stashed by the caller into the overlay's extra prefix slot.
struct PyHandler;

PyHandler* CreatePyHandler(PyObject* py_self,
                           const std::vector<std::string>& method_names);
void DestroyPyHandler(PyHandler* h);

// Module-level demo entry: builds a Phase 1 overlay over a reflected
// `struct PhaseOneBase { virtual int frob(int) }` and runs an N-iteration
// loop dispatching through the overlaid vtable, returning mean ns/call.
// Registered as `_vtableproxy_hot_loop` in libcppyy.
PyObject* VTableProxyHotLoop(PyObject*, PyObject* args);

// True if `method` (an opaque clang FunctionDecl handle -- the same void*
// Cpp::TCppFunction_t and Cppyy::TCppMethod_t both wrap) has a signature
// covered by the thunk table. Used by Dispatcher.cxx's fast-path predicate
// so the thunk table is the single source of truth for "is this overrideable
// fast?"; no string-compare duplication.
bool IsInThunkTable(void* method);

// Wires the fast path onto a freshly-constructed Base instance: looks up
// the matching thunk for `method_name` in `base_scope`, installs a
// VTableOverlay over inst's vtable, creates a PyHandler from `py_self`,
// and stashes it at the overlay's extra prefix slot. Returns true on
// success. The overlay + handler lifetimes are tied to the instance --
// today they're leaked at instance destruction (PoC); production hooks
// follow once the wiring is proven.
bool InstallVTableProxyOverlay(void* inst, void* base_scope,
                               const char* method_name, PyObject* py_self);

} // namespace CPyCppyy

#endif // CPYCPPYY_PYVIRTUALHANDLER_H
