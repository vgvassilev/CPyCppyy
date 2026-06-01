#ifndef CPYCPPYY_MARSHALSCALAR_H
#define CPYCPPYY_MARSHALSCALAR_H

// Compile-time-dispatched scalar marshaling.
//
// Each Converter subclass in Converters.cxx contains a body that, for a
// statically known T, reduces to a one-liner (e.g. PyLong_FromLong(*(int*)a)).
// The slow path goes through Converter::FromMemory / ToMemory virtuals because
// the type is only known by string at runtime there. The thunk table has T at
// compile time and shouldn't pay for the virtual dispatch.
//
// MarshalToPy<T> / MarshalFromPy<T> are the single definition site for the
// scalar marshaling code. Specializations live alongside their corresponding
// Converter subclass (so adding a new scalar type stays a single edit). The
// thunk table calls these templates directly; the slow-path Converter virtuals
// are an opportunity to DRY them up in a follow-up by delegating to the same
// template.

#include <Python.h>

namespace CPyCppyy {

// Primary template: deliberately unimplemented so a non-scalar T at the
// thunk table row instantiation site fails to link with a recognizable symbol.
template <class T> PyObject* MarshalToPy(const T& v);
template <class T> bool      MarshalFromPy(PyObject* o, T& out);

// --- int ---------------------------------------------------------------------
template <> inline PyObject* MarshalToPy<int>(const int& v) {
  return PyLong_FromLong(v);
}
template <> inline bool MarshalFromPy<int>(PyObject* o, int& out) {
  if (!PyLong_Check(o)) {
    PyErr_SetString(PyExc_TypeError, "int expected");
    return false;
  }
  out = static_cast<int>(PyLong_AsLong(o));
  return true;
}

// --- double ------------------------------------------------------------------
// PyFloat_AsDouble also accepts PyLong (via __float__), matching Python's
// numeric tower; the fast path leans on that rather than reject int returns.
template <> inline PyObject* MarshalToPy<double>(const double& v) {
  return PyFloat_FromDouble(v);
}
template <> inline bool MarshalFromPy<double>(PyObject* o, double& out) {
  out = PyFloat_AsDouble(o);
  if (out == -1.0 && PyErr_Occurred())
    return false;
  return true;
}

// --- bool --------------------------------------------------------------------
// Py_True / Py_False are PyLong subclasses, so PyLong_Check covers both;
// PyObject_IsTrue handles the broader truthiness contract for return values.
template <> inline PyObject* MarshalToPy<bool>(const bool& v) {
  return PyBool_FromLong(v);
}
template <> inline bool MarshalFromPy<bool>(PyObject* o, bool& out) {
  int r = PyObject_IsTrue(o);
  if (r < 0)
    return false;
  out = (r != 0);
  return true;
}

} // namespace CPyCppyy

#endif // CPYCPPYY_MARSHALSCALAR_H
