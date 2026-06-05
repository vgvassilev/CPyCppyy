// Cppyy-tuned per-instance handler for Phase 1 VTableOverlay dispatch.
//
// CppInterOp ships one variadic template, Cpp::Thunks::dispatch<T, Slot, R,
// Args...>, that knows nothing about Python. The binding contributes a
// HandlerTraits struct (PyHandlerTraits) that pins down (a) where the
// per-instance handler lives -- here, vptr[-(kPrefix+1)], the slot reserved
// via MakeVTableOverlay's n_extra_prefix_slots -- and (b) how to marshal
// Args... to/from the target language.
//
// Marshaling uses MarshalScalar.h, the compile-time-dispatched scalar entry
// points that are also the canonical definition site for each scalar type's
// to/from-Py code. The slow-path Converter::FromMemory/ToMemory virtuals (in
// Converters.cxx) cover the full type universe and remain unchanged; a
// follow-up can DRY them by delegating to the same templates.
//
// Bound-method unboxing: getattr(self, name) returns a PyMethod that wraps
// (function, self). CPython's method_vectorcall extracts those on every
// dispatch -- a hot-path indirection the thunk table can skip. CreatePyHandler
// unboxes once and stores (func, self) directly; Call vectorcalls func with
// self prepended in args[0].

#include "PyVirtualHandler.h"

#include "MarshalScalar.h"

#include "CPyCppyy/PyException.h"

#include "CppInterOp/CppInterOp.h"
#include "CppInterOp/CppInterOpThunks.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace CPyCppyy {

struct PyHandler {
  PyObject* py_self;
  struct Entry {
    PyObject* func; // owned ref; the underlying function of an unboxed method
    PyObject* self; // owned ref; null if the entry is a free callable
  };
  std::vector<Entry> bound;
};

namespace {

// HandlerTraits contract for Cpp::Thunks::dispatch. `From` locates the
// per-instance handler (Phase 1: extra prefix slot, single fixed-offset
// load). `Call` marshals Args... and the return value via MarshalScalar.h's
// compile-time-dispatched templates, then vectorcalls the pre-unboxed
// (func, self) entry in the handler.
struct PyHandlerTraits {
  using Handler = PyHandler;

  static Handler& From(void* self) {
    
    return *static_cast<Handler*>(
        Cpp::VTableOverlayExtraSlot(self, 0));
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
          // Match Cling-emitted forwarder semantics: a Python error in
          // the override propagates as a C++ PyException that cppyy's
          // caller catches and re-raises Python-side.
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
            // MarshalFromPy set a Python error (e.g. wrong return type);
            // propagate same as a thrown override.
            PyGILState_Release(g);
            throw CPyCppyy::PyException{};
          }
        } else if (PyErr_Occurred()) {
          // Match Cling-emitted forwarder semantics: a Python error in
          // the override propagates as a C++ PyException that cppyy's
          // caller catches and re-raises Python-side.
          PyGILState_Release(g);
          throw CPyCppyy::PyException{};
        }
      }
      PyGILState_Release(g);
      return result;
    }
  }
};

// -----------------------------------------------------------------------------
// Thunk table -- the table of precompiled bridging functions cppyy installs
// in overlaid vtable slots. One row per (R, Args...) signature shape; each
// row is kMaxSlots instantiations of Cpp::Thunks::dispatch parameterized by
// (PyHandlerTraits, Slot, R, Args...). std::index_sequence expands the Slot
// axis at compile time; identical instantiations fold to the same .text, so
// the table costs only the pointer array per shape. "Thunk table" = the registry
// view; "thunks" = what populates it.
// -----------------------------------------------------------------------------

enum class SigShape : int {
  Unsupported = -1,
  // 0-arg
  Int_Void,        // int  f()
  Void_Void,       // void f()
  Bool_Void,       // bool f()
  Double_Void,     // double f()
  // 1-arg
  Int_Int,         // int  f(int)
  Int_Bool,        // int  f(bool)
  Void_Int,        // void f(int)
  Void_Bool,       // void f(bool)
  Double_Double,   // double f(double)
  Bool_Int,        // bool f(int)
  Bool_Bool,       // bool f(bool)
  // 2-arg
  Int_IntInt,      // int  f(int, int)
  Bool_IntInt,     // bool f(int, int)
};

constexpr std::size_t kMaxSlots = 8;

template <class R, class... Args, std::size_t... I>
constexpr std::array<void*, sizeof...(I)>
MakeThunkRow(std::index_sequence<I...>) {
  return {{reinterpret_cast<void*>(
      &Cpp::Thunks::dispatch<PyHandlerTraits, I, R, Args...>)...}};
}

template <class R, class... Args>
const std::array<void*, kMaxSlots>& ThunkRow() {
  static const auto kRow =
      MakeThunkRow<R, Args...>(std::make_index_sequence<kMaxSlots>{});
  return kRow;
}

SigShape ClassifySignature(Cpp::TCppFunction_t method) {
  if (!method)
    return SigShape::Unsupported;
  // Cache canonical builtin QualTypes once. Cpp::GetType returns the
  // canonicalized QualType's opaque pointer, so equality on TCppType_t
  // is canonical-type identity -- replaces the per-call GetTypeAsString
  // + std::string compare against "void"/"bool"/"double" literals.
  static const Cpp::TCppType_t kVoid = Cpp::GetType("void");
  static const Cpp::TCppType_t kBool = Cpp::GetType("bool");
  static const Cpp::TCppType_t kDouble = Cpp::GetType("double");

  Cpp::TCppType_t ret = Cpp::GetFunctionReturnType(method);
  Cpp::TCppIndex_t nargs = Cpp::GetFunctionNumArgs(method);
  bool ret_int = Cpp::IsIntegerType(ret);
  bool ret_void = (ret == kVoid);
  bool ret_double = (ret == kDouble);
  bool ret_bool = (ret == kBool);
  if (nargs == 0) {
    if (ret_int)    return SigShape::Int_Void;
    if (ret_void)   return SigShape::Void_Void;
    if (ret_bool)   return SigShape::Bool_Void;
    if (ret_double) return SigShape::Double_Void;
  } else if (nargs == 1) {
    Cpp::TCppType_t a0 = Cpp::GetFunctionArgType(method, 0);
    bool a0_int = Cpp::IsIntegerType(a0);
    bool a0_double = (a0 == kDouble);
    bool a0_bool = (a0 == kBool);
    if (ret_int    && a0_int)    return SigShape::Int_Int;
    if (ret_int    && a0_bool)   return SigShape::Int_Bool;
    if (ret_void   && a0_int)    return SigShape::Void_Int;
    if (ret_void   && a0_bool)   return SigShape::Void_Bool;
    if (ret_double && a0_double) return SigShape::Double_Double;
    if (ret_bool   && a0_int)    return SigShape::Bool_Int;
    if (ret_bool   && a0_bool)   return SigShape::Bool_Bool;
  } else if (nargs == 2) {
    Cpp::TCppType_t a0 = Cpp::GetFunctionArgType(method, 0);
    Cpp::TCppType_t a1 = Cpp::GetFunctionArgType(method, 1);
    bool a0_int = Cpp::IsIntegerType(a0);
    bool a1_int = Cpp::IsIntegerType(a1);
    if (ret_int  && a0_int && a1_int) return SigShape::Int_IntInt;
    if (ret_bool && a0_int && a1_int) return SigShape::Bool_IntInt;
  }
  return SigShape::Unsupported;
}

void* LookupThunk(SigShape shape, std::size_t slot) {
  if (slot >= kMaxSlots)
    return nullptr;
  switch (shape) {
  // 0-arg
  case SigShape::Int_Void:      return ThunkRow<int>()[slot];
  case SigShape::Void_Void:     return ThunkRow<void>()[slot];
  case SigShape::Bool_Void:     return ThunkRow<bool>()[slot];
  case SigShape::Double_Void:   return ThunkRow<double>()[slot];
  // 1-arg
  case SigShape::Int_Int:       return ThunkRow<int, int>()[slot];
  case SigShape::Int_Bool:      return ThunkRow<int, bool>()[slot];
  case SigShape::Void_Int:      return ThunkRow<void, int>()[slot];
  case SigShape::Void_Bool:     return ThunkRow<void, bool>()[slot];
  case SigShape::Double_Double: return ThunkRow<double, double>()[slot];
  case SigShape::Bool_Int:      return ThunkRow<bool, int>()[slot];
  case SigShape::Bool_Bool:     return ThunkRow<bool, bool>()[slot];
  // 2-arg
  case SigShape::Int_IntInt:    return ThunkRow<int, int, int>()[slot];
  case SigShape::Bool_IntInt:   return ThunkRow<bool, int, int>()[slot];
  default:
    return nullptr;
  }
}

} // namespace

bool IsInThunkTable(void* method) {
  return ClassifySignature(reinterpret_cast<Cpp::TCppFunction_t>(method))
      != SigShape::Unsupported;
}

bool InstallVTableProxyOverlay(void* inst, void* base_scope_raw,
                               const char* method_name, PyObject* py_self) {
  auto base = reinterpret_cast<Cpp::TCppScope_t>(base_scope_raw);
  std::vector<Cpp::TCppFunction_t> methods;
  Cpp::GetClassMethods(base, methods);
  Cpp::TCppFunction_t target = nullptr;
  for (auto* m : methods) {
    if (Cpp::GetName(m) == method_name) {
      target = m;
      break;
    }
  }
  if (!target)
    return false;

  SigShape shape = ClassifySignature(target);
  void* thunk = LookupThunk(shape, /*slot=*/0);
  if (!thunk)
    return false;

  PyHandler* handler = CreatePyHandler(py_self, {method_name});
  Cpp::TCppConstFunction_t cmethod = (Cpp::TCppConstFunction_t)target;
  Cpp::VTableOverlay* ov = Cpp::MakeVTableOverlay(
      inst, base, &cmethod, &thunk, /*n=*/1, /*n_extra_prefix_slots=*/1,
      // on_destroy fires once after the C++ destructor runs (the
      // operator-delete path the cppyy proxy takes). Frees the handler --
      // releasing its Py_INCREFs on (func, self) -- so the Python wrapper
      // can be reclaimed by GC and cppyy's MemoryRegulator picks up the
      // destruction normally (test10's s_count balance).
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
  // Borrow py_self -- only used inside this function for GetAttrString.
  // The Entry::{func, self} captured below hold their own owning refs to
  // what the hot path actually reads, so we don't tie py_self's lifetime
  // to the handler. This matters in the PoC where the handler is leaked:
  // a Py_INCREF here would pin the Python wrapper and block cppyy's
  // MemoryRegulator from destructing the C++ instance (test10 regression).
  h->py_self = nullptr;
  h->bound.reserve(method_names.size());
  for (const auto& name : method_names) {
    PyObject* bm = PyObject_GetAttrString(py_self, name.c_str());
    if (!bm) {
      PyErr_Clear();
      h->bound.push_back({nullptr, nullptr});
      continue;
    }
    if (PyMethod_Check(bm)) {
      // Unbox once: skip CPython's method_vectorcall trampoline on every
      // dispatch. Borrow refs to func and self -- DO NOT Py_INCREF. A
      // strong ref to self (the cppyy proxy) keeps the proxy refcount
      // above 0, which deadlocks the lifetime cycle: proxy refcount
      // gates Python GC, which gates cppyy's MemoryRegulator, which
      // gates the C++ destructor (D0), which is what would fire the
      // dtor hook that frees this handler. Borrowed is safe because
      // the C++ object dispatching through the overlay is alive => its
      // class (owns func) is alive => its proxy (= self) is alive.
      PyObject* fn = PyMethod_GET_FUNCTION(bm);
      PyObject* sf = PyMethod_GET_SELF(bm);
      Py_DECREF(bm);
      h->bound.push_back({fn, sf});
    } else {
      // Already a free callable (lambda, builtin, etc.) -- no self to
      // prepend; the entry stores it as-is with self=null. Borrowed for
      // the same reason as above; we drop the GetAttrString INCREF.
      PyObject* fn = bm;
      Py_DECREF(bm);
      h->bound.push_back({fn, nullptr});
    }
  }
  return h;
}

void DestroyPyHandler(PyHandler* h) {
  if (!h)
    return;
  // Entry::{func, self} are borrowed (see CreatePyHandler); no DECREF.
  // py_self was set to null at creation -- nothing to release.
  delete h;
}

namespace {

Cpp::TCppFunction_t FindMethod(Cpp::TCppScope_t scope, const char* name) {
  std::vector<Cpp::TCppFunction_t> methods;
  Cpp::GetClassMethods(scope, methods);
  for (auto* m : methods)
    if (Cpp::GetName(m) == name)
      return m;
  return nullptr;
}

// Itanium ABI: vptr[0..1] = dtor pair, vptr[2] = first user virtual (frob).
// The demo hardcodes this; production fast path computes it via VTableContext.
constexpr int kFrobSlot = 2;

void EnsureBenchBases() {
  static bool s_init = false;
  if (s_init) return;
  Cpp::CreateInterpreter({"-include", "new"});
  Cpp::Declare(
      "struct B_int_int { virtual ~B_int_int(){} virtual int frob(int x){return x;} };"
      "struct B_int_void { virtual ~B_int_void(){} virtual int frob(){return 0;} };"
      "struct B_void_int { virtual ~B_void_int(){} virtual void frob(int){} };"
      "struct B_double_double { virtual ~B_double_double(){} virtual double frob(double x){return x;} };"
      "struct B_bool_int { virtual ~B_bool_int(){} virtual bool frob(int){return false;} };"
      "struct B_int_intint { virtual ~B_int_intint(){} virtual int frob(int a, int b){return a+b;} };"
  );
  s_init = true;
}

double TimedDispatch(std::string_view shape, void* inst, void* slot_fn, int N) {
  using namespace std::chrono;
  auto t0 = steady_clock::now();
  if (shape == "int_int") {
    auto fn = reinterpret_cast<int (*)(void*, int)>(slot_fn);
    volatile int sink = 0;
    for (int i = 0; i < N; ++i) sink += fn(inst, 7);
  } else if (shape == "int_void") {
    auto fn = reinterpret_cast<int (*)(void*)>(slot_fn);
    volatile int sink = 0;
    for (int i = 0; i < N; ++i) sink += fn(inst);
  } else if (shape == "void_int") {
    auto fn = reinterpret_cast<void (*)(void*, int)>(slot_fn);
    for (int i = 0; i < N; ++i) fn(inst, 7);
  } else if (shape == "double_double") {
    auto fn = reinterpret_cast<double (*)(void*, double)>(slot_fn);
    volatile double sink = 0;
    for (int i = 0; i < N; ++i) sink += fn(inst, 7.0);
  } else if (shape == "bool_int") {
    auto fn = reinterpret_cast<bool (*)(void*, int)>(slot_fn);
    volatile int sink = 0;
    for (int i = 0; i < N; ++i) sink ^= int(fn(inst, 7));
  } else if (shape == "int_intint") {
    auto fn = reinterpret_cast<int (*)(void*, int, int)>(slot_fn);
    volatile int sink = 0;
    for (int i = 0; i < N; ++i) sink += fn(inst, 3, 5);
  }
  auto dt = steady_clock::now() - t0;
  return duration<double, std::nano>(dt).count() / N;
}

bool WarmupAndProbe(std::string_view shape, void* inst, void* slot_fn) {
  if (shape == "int_int") {
    auto fn = reinterpret_cast<int (*)(void*, int)>(slot_fn);
    fn(inst, 41);
    if (PyErr_Occurred()) return false;
    for (int i = 0; i < 10000; ++i) fn(inst, 7);
  } else if (shape == "int_void") {
    auto fn = reinterpret_cast<int (*)(void*)>(slot_fn);
    fn(inst);
    if (PyErr_Occurred()) return false;
    for (int i = 0; i < 10000; ++i) fn(inst);
  } else if (shape == "void_int") {
    auto fn = reinterpret_cast<void (*)(void*, int)>(slot_fn);
    fn(inst, 41);
    if (PyErr_Occurred()) return false;
    for (int i = 0; i < 10000; ++i) fn(inst, 7);
  } else if (shape == "double_double") {
    auto fn = reinterpret_cast<double (*)(void*, double)>(slot_fn);
    fn(inst, 41.0);
    if (PyErr_Occurred()) return false;
    for (int i = 0; i < 10000; ++i) fn(inst, 7.0);
  } else if (shape == "bool_int") {
    auto fn = reinterpret_cast<bool (*)(void*, int)>(slot_fn);
    fn(inst, 41);
    if (PyErr_Occurred()) return false;
    for (int i = 0; i < 10000; ++i) fn(inst, 7);
  } else if (shape == "int_intint") {
    auto fn = reinterpret_cast<int (*)(void*, int, int)>(slot_fn);
    fn(inst, 3, 5);
    if (PyErr_Occurred()) return false;
    for (int i = 0; i < 10000; ++i) fn(inst, 3, 5);
  } else {
    return false;
  }
  return true;
}

} // namespace

PyObject* VTableProxyHotLoop(PyObject*, PyObject* args) {
  PyObject* py_impl;
  const char* shape_cstr;
  int N;
  if (!PyArg_ParseTuple(args, "Osi", &py_impl, &shape_cstr, &N))
    return nullptr;
  std::string_view shape(shape_cstr);

  EnsureBenchBases();
  std::string base_name = std::string("B_") + shape_cstr;
  Cpp::TCppScope_t B = Cpp::GetNamed(base_name);
  Cpp::TCppFunction_t frob = B ? FindMethod(B, "frob") : nullptr;
  if (!B || !frob) {
    PyErr_Format(PyExc_RuntimeError,
                 "VTableProxyHotLoop: scope/method lookup failed for %s",
                 shape_cstr);
    return nullptr;
  }

  SigShape sigshape = ClassifySignature(frob);
  void* thunk = LookupThunk(sigshape, /*slot=*/0);
  if (!thunk) {
    PyErr_Format(PyExc_RuntimeError,
                 "VTableProxyHotLoop: signature %s not in thunk table",
                 shape_cstr);
    return nullptr;
  }

  void* inst = Cpp::Construct(B);
  if (!inst) {
    PyErr_SetString(PyExc_RuntimeError, "VTableProxyHotLoop: Construct failed");
    return nullptr;
  }

  PyHandler* handler = CreatePyHandler(py_impl, {"frob"});
  auto ov = Cpp::MakeUniqueVTableOverlay(
      inst, B, {{frob, thunk}}, /*n_extra_prefix_slots=*/1);
  if (!ov) {
    DestroyPyHandler(handler);
    Cpp::Destruct(inst, B);
    PyErr_SetString(PyExc_RuntimeError,
                    "VTableProxyHotLoop: MakeUniqueVTableOverlay failed");
    return nullptr;
  }
  void** vptr = *reinterpret_cast<void***>(inst);
  Cpp::VTableOverlayExtraSlot(inst, 0) = handler;
  void* slot_fn = vptr[kFrobSlot];

  if (!WarmupAndProbe(shape, inst, slot_fn)) {
    ov.reset();
    DestroyPyHandler(handler);
    Cpp::Destruct(inst, B);
    return nullptr;
  }
  double ns = TimedDispatch(shape, inst, slot_fn, N);
  if (PyErr_Occurred()) {
    ov.reset();
    DestroyPyHandler(handler);
    Cpp::Destruct(inst, B);
    return nullptr;
  }

  ov.reset();
  DestroyPyHandler(handler);
  Cpp::Destruct(inst, B);
  return PyFloat_FromDouble(ns);
}

} // namespace CPyCppyy
