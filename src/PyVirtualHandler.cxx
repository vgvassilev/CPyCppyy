// PyHandler binds the Python override against the C++ instance. The
// catalog itself -- the map of (traits-id, signature) -> thunk row --
// lives in libclangCppInterOp.so; this TU registers the built-in
// scalar rows through CppInterOp's open API at static-init.
//
// Bound-method unboxing: getattr(self, name) wraps (function, self)
// in a PyMethod that CPython unpacks on every call. Doing it once at
// handler creation drops a vectorcall indirection from the hot path.

#include "PyVirtualHandler.h"

#include "Cppyy.h"

namespace CPyCppyy {

bool IsInThunkTable(void* method) {
  return Cpp::LookupThunk(kPyHandlerTraitsId,
                          reinterpret_cast<Cpp::TCppFunction_t>(method),
                          /*slot=*/0) != nullptr;
}

bool InstallVTableProxyOverlay(void* inst, void* base_scope_raw,
                               const char* method_name, PyObject* py_self) {
  auto base = reinterpret_cast<Cpp::TCppScope_t>(base_scope_raw);
  auto funcs = Cppyy::GetMethodsFromName(base, method_name);
  if (funcs.empty())
    return false;
  Cpp::TCppFunction_t target =
      reinterpret_cast<Cpp::TCppFunction_t>(funcs.front());

  void* thunk = Cpp::LookupThunk(kPyHandlerTraitsId, target, /*slot=*/0);
  if (!thunk)
    return false;

  PyHandler* handler = CreatePyHandler(py_self, {method_name});
  Cpp::TCppConstFunction_t cmethod = (Cpp::TCppConstFunction_t)target;
  Cpp::VTableOverlay* ov = Cpp::MakeVTableOverlay(
      inst, base, &cmethod, &thunk, /*n=*/1, /*n_extra_prefix_slots=*/1,
      // Free the handler after the C++ destructor runs so cppyy's
      // MemoryRegulator can reclaim the Python wrapper.
      [](void* /*inst*/, void* data) {
        DestroyPyHandler(static_cast<PyHandler*>(data));
      },
      handler);
  if (!ov) {
    DestroyPyHandler(handler);
    return false;
  }
  Cpp::VTableOverlayExtraSlot(inst, 0) = handler;
  return true;
}

PyHandler* CreatePyHandler(PyObject* py_self,
                           const std::vector<std::string>& method_names) {
  auto* h = new PyHandler;
  h->py_self = nullptr;
  h->bound.reserve(method_names.size());
  // Refs to (func, self) are borrowed throughout: an owning ref to the
  // cppyy proxy would pin it past the C++ destructor and break the
  // overlay's dtor-hook teardown.
  for (const auto& name : method_names) {
    PyObject* bm = PyObject_GetAttrString(py_self, name.c_str());
    if (!bm) {
      PyErr_Clear();
      h->bound.push_back({nullptr, nullptr});
      continue;
    }
    if (PyMethod_Check(bm)) {
      // Unbox PyMethod once so the thunk skips method_vectorcall.
      PyObject* fn = PyMethod_GET_FUNCTION(bm);
      PyObject* sf = PyMethod_GET_SELF(bm);
      Py_DECREF(bm);
      h->bound.push_back({fn, sf});
    } else {
      PyObject* fn = bm;
      Py_DECREF(bm);
      h->bound.push_back({fn, nullptr});
    }
  }
  return h;
}

void DestroyPyHandler(PyHandler* h) {
  delete h;
}

// Built-in scalar rows. Each call instantiates one (R, Args...) row
// (kMaxOverlaySlots thunks) in this TU and publishes it. Downstream
// libraries register additional rows the same way -- the catalog is
// open by construction.
namespace {
const int _builtin_catalog = (
    RegisterCatalogRow<int>("int ()"),
    RegisterCatalogRow<void>("void ()"),
    RegisterCatalogRow<bool>("bool ()"),
    RegisterCatalogRow<double>("double ()"),
    RegisterCatalogRow<int, int>("int (int)"),
    RegisterCatalogRow<int, int&>("int (int &)"),
    RegisterCatalogRow<int, bool>("int (bool)"),
    RegisterCatalogRow<void, int>("void (int)"),
    RegisterCatalogRow<void, bool>("void (bool)"),
    RegisterCatalogRow<double, double>("double (double)"),
    RegisterCatalogRow<bool, int>("bool (int)"),
    RegisterCatalogRow<bool, bool>("bool (bool)"),
    RegisterCatalogRow<int, int, int>("int (int,int)"),
    RegisterCatalogRow<bool, int, int>("bool (int,int)"),
    0);
} // anonymous

} // namespace CPyCppyy
