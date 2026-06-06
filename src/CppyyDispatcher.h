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
    enum Kind { kStringTemplate, kVTableProxy };

    CppyyDispatcher(Kind k, std::string sig, double construction_ms)
      : fKind(k), fSignature(std::move(sig)), fConstructionMs(construction_ms) {}
    virtual ~CppyyDispatcher() = default;

    Kind GetKind() const { return fKind; }
    const char* KindString() const {
        return fKind == kStringTemplate ? "string-template" : "vtable-proxy";
    }
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

// The fast path: a per-instance vtable overlay. Carries the per-class
// metadata (base size, method slot, method names) the constructor hook
// needs to install the catalog thunk at NewInstance time. Only one
// method slot is patched per class today (see Dispatcher.cxx).
class VTableProxyDispatcher : public CppyyDispatcher {
public:
    VTableProxyDispatcher(std::size_t base_size, int method_slot,
                          std::vector<std::string> method_names,
                          std::string sig, double ms)
      : CppyyDispatcher(kVTableProxy, std::move(sig), ms),
        fBaseSize(base_size), fMethodSlot(method_slot),
        fMethodNames(std::move(method_names)) {}
    std::size_t BaseSize() const { return fBaseSize; }
    int MethodSlot() const { return fMethodSlot; }
    const std::vector<std::string>& MethodNames() const { return fMethodNames; }
private:
    std::size_t fBaseSize;
    int fMethodSlot;
    std::vector<std::string> fMethodNames;
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
