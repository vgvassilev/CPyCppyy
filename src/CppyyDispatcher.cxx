// Registry storage + the Python-facing accessor. The Register() call
// is invoked from InsertDispatcher's two branches.

#include "CppyyDispatcher.h"

namespace CPyCppyy {

DispatcherRegistry& DispatcherRegistry::Instance() {
    static DispatcherRegistry s_instance;
    return s_instance;
}

void DispatcherRegistry::Register(std::string class_name, CppyyDispatcher* d) {
    fEntries.push_back({std::move(class_name), d});
}

std::vector<DispatcherRegistry::Entry> DispatcherRegistry::Snapshot() const {
    return fEntries;
}

void DispatcherRegistry::Reset() {
    for (auto& e : fEntries) delete e.dispatcher;
    fEntries.clear();
}

PyObject* DispatcherSummary(PyObject*, PyObject*) {
    PyObject* out = PyList_New(0);
    auto snap = DispatcherRegistry::Instance().Snapshot();
    for (const auto& e : snap) {
        PyObject* item = PyDict_New();
        auto put_s = [&](const char* k, const std::string& v) {
            PyObject* pv = PyUnicode_FromString(v.c_str());
            PyDict_SetItemString(item, k, pv);
            Py_DECREF(pv);
        };
        auto put_kw = [&](const char* k, const char* v) {
            PyObject* pv = PyUnicode_FromString(v);
            PyDict_SetItemString(item, k, pv);
            Py_DECREF(pv);
        };
        auto put_f = [&](const char* k, double v) {
            PyObject* pv = PyFloat_FromDouble(v);
            PyDict_SetItemString(item, k, pv);
            Py_DECREF(pv);
        };
        auto put_u = [&](const char* k, unsigned long v) {
            PyObject* pv = PyLong_FromUnsignedLong(v);
            PyDict_SetItemString(item, k, pv);
            Py_DECREF(pv);
        };
        put_s("class_name", e.class_name);
        put_kw("kind", e.dispatcher->KindString());
        put_s("signature", e.dispatcher->Signature());
        put_f("construction_ms", e.dispatcher->ConstructionTimeMs());
        put_u("instances", e.dispatcher->Instances());
        PyList_Append(out, item);
        Py_DECREF(item);
    }
    return out;
}

PyObject* DispatcherSummaryReset(PyObject*, PyObject*) {
    DispatcherRegistry::Instance().Reset();
    Py_RETURN_NONE;
}

} // namespace CPyCppyy
