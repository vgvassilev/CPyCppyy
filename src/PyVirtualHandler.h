// Per-instance handler + binding-side wrapper over CppInterOp's open
// thunk catalog. The catalog itself lives in libclangCppInterOp;
// CPyCppyy contributes the PyHandler / PyHandlerTraits pair and
// pins the traits-id used to namespace its rows.

#ifndef CPYCPPYY_PYVIRTUALHANDLER_H
#define CPYCPPYY_PYVIRTUALHANDLER_H

#include <Python.h>

#include "MarshalScalar.h"

#include "CPyCppyy/PyException.h"

#include "CppInterOp/Dispatch.h"
#include "CppInterOp/CppInterOpThunks.h"

#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>

namespace CPyCppyy {

// Process-wide identity for this binding's rows in the CppInterOp
// catalog. Downstream code that wants to override a row registers
// against the same string; the contract is stable.
inline constexpr const char* kPyHandlerTraitsId = "CPyCppyy::PyHandlerTraits";

// Per-instance handler. CreatePyHandler binds the methods named in
// `method_names` against `py_self`; the resulting pointer is stashed
// in the overlay's extra prefix slot for the thunk to read back.
struct PyHandler {
  PyObject* py_self;
  struct Entry {
    PyObject* func; // borrowed
    PyObject* self; // borrowed; null for free callables
  };
  std::vector<Entry> bound;
};
PyHandler* CreatePyHandler(PyObject* py_self,
                           const std::vector<std::string>& method_names);
void DestroyPyHandler(PyHandler* h);

// HandlerTraits contract for Cpp::Thunks::dispatch. `From` reads the
// per-instance handler from the overlay's extra prefix slot through
// the CppInterOp dispatch table (no direct link dep).
struct PyHandlerTraits {
  using Handler = PyHandler;

  static Handler& From(void* self) {
    return *static_cast<Handler*>(Cpp::VTableOverlayExtraSlot(self, 0));
  }

  template <class R, class... Args>
  static R Call(Handler& h, std::size_t Slot, Args... args) {
    constexpr std::size_t N = sizeof...(Args);
    PyGILState_STATE g = PyGILState_Ensure();
    const auto& e = h.bound[Slot];

    if constexpr (std::is_void_v<R>) {
      if (e.func) {
        PyObject* py_args[1 + N];
        py_args[0] = e.self;
        std::size_t i = 1;
        ((py_args[i++] = MarshalToPy<Args>(args)), ...);
        PyObject* const* base = e.self ? py_args : py_args + 1;
        std::size_t nargs = (e.self ? 1u : 0u) + N;
        PyObject* py_res = PyObject_Vectorcall(e.func, base, nargs, nullptr);
        for (std::size_t k = 1; k < 1 + N; ++k)
          Py_DECREF(py_args[k]);
        if (py_res) {
          Py_DECREF(py_res);
        } else if (PyErr_Occurred()) {
          PyGILState_Release(g);
          throw CPyCppyy::PyException{};
        }
      }
      PyGILState_Release(g);
      return;
    } else {
      R result{};
      if (e.func) {
        PyObject* py_args[1 + N];
        py_args[0] = e.self;
        std::size_t i = 1;
        ((py_args[i++] = MarshalToPy<Args>(args)), ...);
        PyObject* const* base = e.self ? py_args : py_args + 1;
        std::size_t nargs = (e.self ? 1u : 0u) + N;
        PyObject* py_res = PyObject_Vectorcall(e.func, base, nargs, nullptr);
        for (std::size_t k = 1; k < 1 + N; ++k)
          Py_DECREF(py_args[k]);
        if (py_res) {
          bool ok = MarshalFromPy<R>(py_res, result);
          Py_DECREF(py_res);
          if (!ok) {
            PyGILState_Release(g);
            throw CPyCppyy::PyException{};
          }
        } else if (PyErr_Occurred()) {
          PyGILState_Release(g);
          throw CPyCppyy::PyException{};
        }
      }
      PyGILState_Release(g);
      return result;
    }
  }
};

// Pin Traits=PyHandlerTraits and the traits-id; downstream Python-binding
// libraries call this from a TU-scope initializer to extend the catalog.
template <class R, class... Args>
inline int RegisterCatalogRow(const char* signature) {
  return Cpp::Thunks::RegisterCatalogRow<PyHandlerTraits, R, Args...>(
      kPyHandlerTraitsId, signature);
}

// True iff some catalog row claims `method`.
bool IsInThunkTable(void* method);

// Install the overlay over `inst` and stash the per-instance handler
// at the extra prefix slot. Returns false on lookup miss.
bool InstallVTableProxyOverlay(void* inst, void* base_scope,
                               const char* method_name, PyObject* py_self);

} // namespace CPyCppyy

#endif // CPYCPPYY_PYVIRTUALHANDLER_H
