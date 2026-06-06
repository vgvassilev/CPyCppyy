// Inventory layer over cppyy's dispatcher paths. Records, per class,
// which path InsertDispatcher took and basic metadata about that
// decision (signature, construction cost, instance count). The hot
// path itself stays where it already is.

#ifndef CPYCPPYY_CPPYY_DISPATCHER_H
#define CPYCPPYY_CPPYY_DISPATCHER_H

#include <Python.h>

#include <string>
#include <vector>

namespace CPyCppyy {

class CppyyDispatcher {
public:
    enum Kind { kStringTemplate };

    CppyyDispatcher(Kind k, std::string sig, double construction_ms)
      : fKind(k), fSignature(std::move(sig)), fConstructionMs(construction_ms) {}
    virtual ~CppyyDispatcher() = default;

    Kind GetKind() const { return fKind; }
    const char* KindString() const { return "string-template"; }
    const std::string& Signature() const { return fSignature; }
    double ConstructionTimeMs() const { return fConstructionMs; }
    unsigned long Instances() const { return fInstances; }
    void NoteInstance() { ++fInstances; }

private:
    Kind fKind;
    std::string fSignature;
    double fConstructionMs = 0.0;
    unsigned long fInstances = 0;
};

// The existing path: a JIT-emitted derived class identified by the
// TCppScope_t the string-template compile produced.
class StringTemplateDispatcher : public CppyyDispatcher {
public:
    StringTemplateDispatcher(void* disp_scope, std::string sig, double ms)
      : CppyyDispatcher(kStringTemplate, std::move(sig), ms),
        fDispScope(disp_scope) {}
    void* DispScope() const { return fDispScope; }
private:
    void* fDispScope;  // erased Cppyy::TCppScope_t to keep this header light
};

// Process-wide registry, populated from InsertDispatcher's tail. All
// public methods are mutex-protected: class creation is not hot, and
// the JIT path inside InsertDispatcher may release the GIL.
class DispatcherRegistry {
public:
    static DispatcherRegistry& Instance();
    void Register(std::string class_name, CppyyDispatcher* d);
    struct Entry { std::string class_name; CppyyDispatcher* dispatcher; };
    std::vector<Entry> Snapshot() const;
    void Reset();
private:
    std::vector<Entry> fEntries;
};

// Python entries: list-of-dicts snapshot, and a reset.
PyObject* DispatcherSummary(PyObject*, PyObject*);
PyObject* DispatcherSummaryReset(PyObject*, PyObject*);

} // namespace CPyCppyy

#endif // CPYCPPYY_CPPYY_DISPATCHER_H
