#ifndef CPYCPPYY_MARSHALSCALAR_H
#define CPYCPPYY_MARSHALSCALAR_H

// Single-definition-site scalar marshaling.
//
// Each Converter subclass in Converters.cxx historically inlined a
// per-T pair of Python C-API <-> C calls (PyLong_FromLong,
// PyFloat_AsDouble, ...) in its CPPYY_IMPL_BASIC_CONVERTER_* macro
// body. The thunk catalog instantiates the same per-T marshaling
// pattern from its own dispatcher templates -- two parallel
// implementations to keep in sync per type.
//
// These templates are the single source of truth: the slow-path
// Converter virtuals (#61) and the catalog thunk-table (#60) both
// delegate here. All bodies are pure Python C-API which signals
// failure via NULL / -1 + PyErr_Occurred -- never via a C++ throw --
// so the contract is noexcept across the board, letting callers strip
// EH plumbing.

#include "Cppyy.h"  // for PY_LONG_DOUBLE
#include <Python.h>
#include <climits>
#include <cstdint>
#include <cwchar>
#include <string>

namespace CPyCppyy { extern PyObject* gDefaultObject; }

namespace CPyCppyy {

// Primary templates deliberately undefined: an unsupported T at a
// thunk-table or Converter instantiation site fails to link with a
// recognizable symbol name. Strict() variants reject float-to-int
// implicit coercion and range-check the destination -- the slow-path
// Converter::ToMemory / SetArg policy. The non-strict pair is for the
// catalog fast path where the caller has already validated.
template <class T> PyObject* MarshalToPy(const T& v) noexcept;
template <class T> bool      MarshalFromPy(PyObject* o, T& out) noexcept;
template <class T> bool      MarshalFromPyStrict(PyObject* o, T& out) noexcept;

// --- bool ------------------------------------------------------------------
template <> inline PyObject* MarshalToPy<bool>(const bool& v) noexcept {
  return PyBool_FromLong(v);
}
template <> inline bool MarshalFromPy<bool>(PyObject* o, bool& out) noexcept {
  int r = PyObject_IsTrue(o);
  if (r < 0) return false;
  out = (r != 0);
  return true;
}

// --- int8 / uint8 ----------------------------------------------------------
template <> inline PyObject* MarshalToPy<int8_t>(const int8_t& v) noexcept {
  return PyLong_FromLong(v);
}
template <> inline bool MarshalFromPy<int8_t>(PyObject* o,
                                              int8_t& out) noexcept {
  long v = PyLong_AsLong(o);
  if (v == -1 && PyErr_Occurred()) return false;
  out = static_cast<int8_t>(v);
  return true;
}

template <> inline PyObject* MarshalToPy<uint8_t>(const uint8_t& v) noexcept {
  return PyLong_FromLong(v);
}
template <> inline bool MarshalFromPy<uint8_t>(PyObject* o,
                                               uint8_t& out) noexcept {
  unsigned long v = PyLong_AsUnsignedLong(o);
  if (v == static_cast<unsigned long>(-1) && PyErr_Occurred()) return false;
  out = static_cast<uint8_t>(v);
  return true;
}

// --- short family ----------------------------------------------------------
template <> inline PyObject* MarshalToPy<short>(const short& v) noexcept {
  return PyLong_FromLong(v);
}
template <> inline bool MarshalFromPy<short>(PyObject* o,
                                             short& out) noexcept {
  long v = PyLong_AsLong(o);
  if (v == -1 && PyErr_Occurred()) return false;
  out = static_cast<short>(v);
  return true;
}

template <> inline PyObject*
MarshalToPy<unsigned short>(const unsigned short& v) noexcept {
  return PyLong_FromUnsignedLong(v);
}
template <> inline bool MarshalFromPy<unsigned short>(
    PyObject* o, unsigned short& out) noexcept {
  unsigned long v = PyLong_AsUnsignedLong(o);
  if (v == static_cast<unsigned long>(-1) && PyErr_Occurred()) return false;
  out = static_cast<unsigned short>(v);
  return true;
}

// --- int / unsigned int ----------------------------------------------------
template <> inline PyObject* MarshalToPy<int>(const int& v) noexcept {
  return PyLong_FromLong(v);
}
// Reference-arg specialization: cppyy treats Python refs as in-params,
// so int& marshals by value (override modifies the Python copy; the
// C++ ref is not written back).
template <> inline PyObject* MarshalToPy<int&>(int& v) noexcept {
  return PyLong_FromLong(v);
}
template <> inline bool MarshalFromPy<int>(PyObject* o, int& out) noexcept {
  long v = PyLong_AsLong(o);
  if (v == -1 && PyErr_Occurred()) return false;
  out = static_cast<int>(v);
  return true;
}

template <>
inline PyObject* MarshalToPy<unsigned int>(const unsigned int& v) noexcept {
  return PyLong_FromUnsignedLong(v);
}
template <> inline bool MarshalFromPy<unsigned int>(
    PyObject* o, unsigned int& out) noexcept {
  unsigned long v = PyLong_AsUnsignedLong(o);
  if (v == static_cast<unsigned long>(-1) && PyErr_Occurred()) return false;
  out = static_cast<unsigned int>(v);
  return true;
}

// --- long / unsigned long --------------------------------------------------
template <> inline PyObject* MarshalToPy<long>(const long& v) noexcept {
  return PyLong_FromLong(v);
}
template <> inline bool MarshalFromPy<long>(PyObject* o, long& out) noexcept {
  long v = PyLong_AsLong(o);
  if (v == -1 && PyErr_Occurred()) return false;
  out = v;
  return true;
}

template <>
inline PyObject* MarshalToPy<unsigned long>(const unsigned long& v) noexcept {
  return PyLong_FromUnsignedLong(v);
}
template <> inline bool MarshalFromPy<unsigned long>(
    PyObject* o, unsigned long& out) noexcept {
  unsigned long v = PyLong_AsUnsignedLong(o);
  if (v == static_cast<unsigned long>(-1) && PyErr_Occurred()) return false;
  out = v;
  return true;
}

// --- long long / unsigned long long ---------------------------------------
template <>
inline PyObject* MarshalToPy<long long>(const long long& v) noexcept {
  return PyLong_FromLongLong(v);
}
template <> inline bool MarshalFromPy<long long>(PyObject* o,
                                                 long long& out) noexcept {
  long long v = PyLong_AsLongLong(o);
  if (v == -1 && PyErr_Occurred()) return false;
  out = v;
  return true;
}

template <> inline PyObject*
MarshalToPy<unsigned long long>(const unsigned long long& v) noexcept {
  return PyLong_FromUnsignedLongLong(v);
}
template <> inline bool MarshalFromPy<unsigned long long>(
    PyObject* o, unsigned long long& out) noexcept {
  unsigned long long v = PyLong_AsUnsignedLongLong(o);
  if (v == static_cast<unsigned long long>(-1) && PyErr_Occurred())
    return false;
  out = v;
  return true;
}

// --- floating-point family -------------------------------------------------
// PyFloat_AsDouble also accepts PyLong via __float__, matching Python's
// numeric tower; the fast path leans on that rather than reject int returns.
template <> inline PyObject* MarshalToPy<float>(const float& v) noexcept {
  return PyFloat_FromDouble(v);
}
template <> inline bool MarshalFromPy<float>(PyObject* o,
                                             float& out) noexcept {
  double v = PyFloat_AsDouble(o);
  if (v == -1.0 && PyErr_Occurred()) return false;
  out = static_cast<float>(v);
  return true;
}

template <> inline PyObject* MarshalToPy<double>(const double& v) noexcept {
  return PyFloat_FromDouble(v);
}
template <> inline bool MarshalFromPy<double>(PyObject* o,
                                              double& out) noexcept {
  double v = PyFloat_AsDouble(o);
  if (v == -1.0 && PyErr_Occurred()) return false;
  out = v;
  return true;
}

// PY_LONG_DOUBLE is typedef'd to long double in Cppyy.h.
template <> inline PyObject*
MarshalToPy<PY_LONG_DOUBLE>(const PY_LONG_DOUBLE& v) noexcept {
  return PyFloat_FromDouble(static_cast<double>(v));
}
template <> inline bool MarshalFromPy<PY_LONG_DOUBLE>(
    PyObject* o, PY_LONG_DOUBLE& out) noexcept {
  double v = PyFloat_AsDouble(o);
  if (v == -1.0 && PyErr_Occurred()) return false;
  out = static_cast<PY_LONG_DOUBLE>(v);
  return true;
}

// --- char / signed char / unsigned char -----------------------------------
// CPyCppyy maps `char` and `unsigned char` to 1-char Python strings (the
// CharExecutor / UCharExecutor convention); see CPyCppyy_PyText_FromLong in
// Executors.cxx. Distinct from the int8_t / uint8_t specializations above
// which marshal as integers (Int8Executor).
namespace detail {
inline PyObject* PyTextFromChar(long cl) noexcept {
  if (cl < -128 || cl > 255) {
    PyErr_SetString(PyExc_ValueError, "char conversion out of range");
    return nullptr;
  }
  unsigned char c = static_cast<unsigned char>(cl < 0 ? 256 + cl : cl);
  return PyUnicode_FromStringAndSize(reinterpret_cast<char*>(&c), 1);
}
inline bool CharFromPy(PyObject* o, long& cl, long lo, long hi) noexcept {
  Py_ssize_t len = 0;
  const char* s = nullptr;
  if (PyUnicode_Check(o))
    s = PyUnicode_AsUTF8AndSize(o, &len);
  else if (PyBytes_Check(o))
    PyBytes_AsStringAndSize(o, const_cast<char**>(&s), &len);
  if (s) {
    if (len != 1) {
      PyErr_Format(PyExc_TypeError,
                   "char expected, got string of size %zd", len);
      return false;
    }
    cl = static_cast<unsigned char>(s[0]);
    return true;
  }
  PyErr_Clear();
  long l = PyLong_AsLong(o);
  if (l == -1 && PyErr_Occurred()) return false;
  if (l < lo || l > hi) {
    PyErr_Format(PyExc_ValueError,
                 "char value %ld not in range [%ld,%ld]", l, lo, hi);
    return false;
  }
  cl = l;
  return true;
}
} // namespace detail

template <> inline PyObject* MarshalToPy<char>(const char& v) noexcept {
  return detail::PyTextFromChar(static_cast<long>(v));
}
template <> inline bool MarshalFromPy<char>(PyObject* o, char& out) noexcept {
  long cl;
  if (!detail::CharFromPy(o, cl, CHAR_MIN, CHAR_MAX)) return false;
  out = static_cast<char>(cl);
  return true;
}

// NOTE: `signed char` is the same type as int8_t and `unsigned char` is
// uint8_t on all platforms CPyCppyy targets, so specializing MarshalToPy
// on those types would collide with the int8_t / uint8_t specializations
// above. The slow-path UCharExecutor / SCharAsIntExecutor still use the
// spelling-keyed lookup in the executor registry to pick between
// str-marshaling and int-marshaling.

// --- wchar_t / char16_t / char32_t ----------------------------------------
template <> inline PyObject* MarshalToPy<wchar_t>(const wchar_t& v) noexcept {
  return PyUnicode_FromWideChar(&v, 1);
}
template <> inline bool
MarshalFromPy<wchar_t>(PyObject* o, wchar_t& out) noexcept {
  if (!PyUnicode_Check(o) || PyUnicode_GetLength(o) != 1) {
    PyErr_SetString(PyExc_TypeError, "wchar_t expected, got non-1-char str");
    return false;
  }
  wchar_t w[1] = {};
  if (PyUnicode_AsWideChar(o, w, 1) != 1) return false;
  out = w[0];
  return true;
}

template <> inline PyObject*
MarshalToPy<char16_t>(const char16_t& v) noexcept {
  return PyUnicode_DecodeUTF16(reinterpret_cast<const char*>(&v),
                               sizeof(char16_t), nullptr, nullptr);
}
template <> inline bool
MarshalFromPy<char16_t>(PyObject* o, char16_t& out) noexcept {
  if (!PyUnicode_Check(o) || PyUnicode_GetLength(o) != 1) {
    PyErr_SetString(PyExc_TypeError, "char16_t expected, got non-1-char str");
    return false;
  }
  Py_UCS4 c = PyUnicode_ReadChar(o, 0);
  if (c == (Py_UCS4)-1) return false;
  if (c > 0xFFFF) {
    PyErr_SetString(PyExc_ValueError, "char16_t value > 0xFFFF");
    return false;
  }
  out = static_cast<char16_t>(c);
  return true;
}

template <> inline PyObject*
MarshalToPy<char32_t>(const char32_t& v) noexcept {
  return PyUnicode_DecodeUTF32(reinterpret_cast<const char*>(&v),
                               sizeof(char32_t), nullptr, nullptr);
}
template <> inline bool
MarshalFromPy<char32_t>(PyObject* o, char32_t& out) noexcept {
  if (!PyUnicode_Check(o) || PyUnicode_GetLength(o) != 1) {
    PyErr_SetString(PyExc_TypeError, "char32_t expected, got non-1-char str");
    return false;
  }
  Py_UCS4 c = PyUnicode_ReadChar(o, 0);
  if (c == (Py_UCS4)-1) return false;
  out = static_cast<char32_t>(c);
  return true;
}

// --- C-strings (const char*, wchar_t*, char16_t*, char32_t*) --------------
// MarshalToPy direction only. The from-Python direction requires owning the
// returned buffer somewhere (the Python str's underlying storage isn't
// stable across decrefs) and that policy lives in the executor / converter
// layer, not the per-T marshaler. See CStringRefConverter for the slow-path
// lifetime story.
template <> inline PyObject*
MarshalToPy<const char*>(const char* const& v) noexcept {
  if (!v) return PyUnicode_FromStringAndSize("", 0);
  return PyUnicode_FromString(v);
}
template <> inline PyObject*
MarshalToPy<char*>(char* const& v) noexcept {
  if (!v) return PyUnicode_FromStringAndSize("", 0);
  return PyUnicode_FromString(v);
}
template <> inline PyObject*
MarshalToPy<const wchar_t*>(const wchar_t* const& v) noexcept {
  if (!v) return PyUnicode_FromWideChar(L"", 0);
  return PyUnicode_FromWideChar(v, std::wcslen(v));
}
template <> inline PyObject*
MarshalToPy<wchar_t*>(wchar_t* const& v) noexcept {
  if (!v) return PyUnicode_FromWideChar(L"", 0);
  return PyUnicode_FromWideChar(v, std::wcslen(v));
}

// --- std::string / std::wstring -------------------------------------------
// Python-str marshalling for std::string family. Note: cppyy's slow-path
// STLStringExecutor binds std::string return values as CPPInstance proxies
// (so std::string-valued attributes stay C++ objects on the Python side
// with identity semantics); these marshalers cover the catalog override
// path where a Python override returns a Python str and the thunk has to
// hand a std::string back to C++.
template <> inline PyObject*
MarshalToPy<std::string>(const std::string& v) noexcept {
  return PyUnicode_FromStringAndSize(v.data(), v.size());
}
template <> inline bool
MarshalFromPy<std::string>(PyObject* o, std::string& out) noexcept {
  if (PyBytes_Check(o)) {
    char* s = nullptr; Py_ssize_t n = 0;
    if (PyBytes_AsStringAndSize(o, &s, &n) < 0) return false;
    out.assign(s, n);
    return true;
  }
  if (PyUnicode_Check(o)) {
    Py_ssize_t n = 0;
    const char* s = PyUnicode_AsUTF8AndSize(o, &n);
    if (!s) return false;
    out.assign(s, n);
    return true;
  }
  PyErr_SetString(PyExc_TypeError, "str/bytes expected for std::string");
  return false;
}

template <> inline PyObject*
MarshalToPy<std::wstring>(const std::wstring& v) noexcept {
  return PyUnicode_FromWideChar(v.data(), v.size());
}
template <> inline bool
MarshalFromPy<std::wstring>(PyObject* o, std::wstring& out) noexcept {
  if (!PyUnicode_Check(o)) {
    PyErr_SetString(PyExc_TypeError, "str expected for std::wstring");
    return false;
  }
  Py_ssize_t n = PyUnicode_GetLength(o);
  out.resize(n);
  if (PyUnicode_AsWideChar(o, out.data(), n) < 0) return false;
  return true;
}


// --- MarshalFromPyStrict specializations -----------------------------------
// Lifted from the original CPyCppyy_PyLong_As* validators in Converters.cxx.
// gDefaultObject special-case mirrors the existing behavior: when cppyy
// passes its sentinel, silently coerce to zero.

namespace detail {
template <class T>
inline bool ToIntStrict(PyObject* o, T& out, long lo, long hi) noexcept {
    if (!(PyLong_Check(o) || PyLong_Check(o))) {
        if (o == CPyCppyy::gDefaultObject) { out = (T)0; return true; }
        PyErr_SetString(PyExc_TypeError,
                        "integer object expected for marshaling");
        return false;
    }
    long l = PyLong_AsLong(o);
    if (l == -1 && PyErr_Occurred()) return false;
    if (l < lo || hi < l) {
        PyErr_Format(PyExc_ValueError,
                     "integer %ld out of destination range", l);
        return false;
    }
    out = static_cast<T>(l);
    return true;
}
} // namespace detail

template <> inline bool
MarshalFromPyStrict<bool>(PyObject* o, bool& out) noexcept {
    long l = PyLong_AsLong(o);
    if (l == -1 && PyErr_Occurred()) return false;
    if ((l != 0 && l != 1) || PyFloat_Check(o)) {
        PyErr_SetString(PyExc_ValueError,
                        "boolean value should be bool, or integer 1 or 0");
        return false;
    }
    out = (l != 0);
    return true;
}

template <> inline bool
MarshalFromPyStrict<int8_t>(PyObject* o, int8_t& out) noexcept {
    return detail::ToIntStrict(o, out, SCHAR_MIN, SCHAR_MAX);
}
template <> inline bool
MarshalFromPyStrict<uint8_t>(PyObject* o, uint8_t& out) noexcept {
    return detail::ToIntStrict(o, out, 0, UCHAR_MAX);
}
template <> inline bool
MarshalFromPyStrict<short>(PyObject* o, short& out) noexcept {
    return detail::ToIntStrict(o, out, SHRT_MIN, SHRT_MAX);
}
template <> inline bool
MarshalFromPyStrict<unsigned short>(PyObject* o,
                                    unsigned short& out) noexcept {
    return detail::ToIntStrict(o, out, 0, USHRT_MAX);
}
template <> inline bool
MarshalFromPyStrict<int>(PyObject* o, int& out) noexcept {
    return detail::ToIntStrict(o, out, INT_MIN, INT_MAX);
}
template <> inline bool
MarshalFromPyStrict<unsigned int>(PyObject* o,
                                  unsigned int& out) noexcept {
    return detail::ToIntStrict(o, out, 0, UINT_MAX);
}

template <> inline bool
MarshalFromPyStrict<long>(PyObject* o, long& out) noexcept {
    if (!(PyLong_Check(o) || PyLong_Check(o))) {
        if (o == CPyCppyy::gDefaultObject) { out = 0; return true; }
        PyErr_SetString(PyExc_TypeError,
                        "integer object expected for marshaling");
        return false;
    }
    long l = PyLong_AsLong(o);
    if (l == -1 && PyErr_Occurred()) return false;
    out = l;
    return true;
}
template <> inline bool
MarshalFromPyStrict<unsigned long>(PyObject* o,
                                   unsigned long& out) noexcept {
    if (!(PyLong_Check(o) || PyLong_Check(o))) {
        if (o == CPyCppyy::gDefaultObject) { out = 0; return true; }
        PyErr_SetString(PyExc_TypeError,
                        "integer object expected for marshaling");
        return false;
    }
    unsigned long v = PyLong_AsUnsignedLong(o);
    if (v == (unsigned long)-1 && PyErr_Occurred()) return false;
    out = v;
    return true;
}

template <> inline bool
MarshalFromPyStrict<long long>(PyObject* o, long long& out) noexcept {
    if (!(PyLong_Check(o) || PyLong_Check(o))) {
        if (o == CPyCppyy::gDefaultObject) { out = 0; return true; }
        PyErr_SetString(PyExc_TypeError,
                        "integer object expected for marshaling");
        return false;
    }
    long long v = PyLong_AsLongLong(o);
    if (v == -1 && PyErr_Occurred()) return false;
    out = v;
    return true;
}
template <> inline bool
MarshalFromPyStrict<unsigned long long>(PyObject* o,
                                        unsigned long long& out) noexcept {
    if (!(PyLong_Check(o) || PyLong_Check(o))) {
        if (o == CPyCppyy::gDefaultObject) { out = 0; return true; }
        PyErr_SetString(PyExc_TypeError,
                        "integer object expected for marshaling");
        return false;
    }
    unsigned long long v = PyLong_AsUnsignedLongLong(o);
    if (v == (unsigned long long)-1 && PyErr_Occurred()) return false;
    out = v;
    return true;
}

// Float family: Python's PyFloat_AsDouble already accepts numeric tower
// (PyLong via __float__); strict() matches PyFloat_AsDouble behavior --
// no extra rejection.
template <> inline bool
MarshalFromPyStrict<float>(PyObject* o, float& out) noexcept {
    double v = PyFloat_AsDouble(o);
    if (v == -1.0 && PyErr_Occurred()) {
        if (o == CPyCppyy::gDefaultObject) { PyErr_Clear(); out = 0; return true; }
        return false;
    }
    out = static_cast<float>(v);
    return true;
}
template <> inline bool
MarshalFromPyStrict<double>(PyObject* o, double& out) noexcept {
    double v = PyFloat_AsDouble(o);
    if (v == -1.0 && PyErr_Occurred()) {
        if (o == CPyCppyy::gDefaultObject) { PyErr_Clear(); out = 0; return true; }
        return false;
    }
    out = v;
    return true;
}
template <> inline bool
MarshalFromPyStrict<PY_LONG_DOUBLE>(PyObject* o,
                                    PY_LONG_DOUBLE& out) noexcept {
    double v = PyFloat_AsDouble(o);
    if (v == -1.0 && PyErr_Occurred()) {
        if (o == CPyCppyy::gDefaultObject) { PyErr_Clear(); out = 0; return true; }
        return false;
    }
    out = static_cast<PY_LONG_DOUBLE>(v);
    return true;
}

} // namespace CPyCppyy

#endif // CPYCPPYY_MARSHALSCALAR_H
