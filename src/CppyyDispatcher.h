// Common abstraction over cppyy's two dispatcher paths -- the
// existing string-template emitter and the new synthesized-vtable
// fast path. The point is observability, not virtual dispatch on the
// hot path: a single registry of all class→dispatcher decisions lets
// us see thunk-table hit rates, per-path construction costs, and miss
// reasons in one place.
//
// Each path's wiring stays where it is. This header adds the
// classification, recording, and inspection layer on top.

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

// Wraps the existing path: a JIT-emitted derived class identified by
// the TCppScope_t the string-template compile produced.
class StringTemplateDispatcher : public CppyyDispatcher {
public:
    StringTemplateDispatcher(void* disp_scope, std::string sig, double ms)
      : CppyyDispatcher(kStringTemplate, std::move(sig), ms),
        fDispScope(disp_scope) {}
    void* DispScope() const { return fDispScope; }
private:
    void* fDispScope;  // erased Cppyy::TCppScope_t to keep this header light
};

// Wraps the synthesized-vtable path. Per-instance handler is built at
// NewInstance time; this object only carries the per-class metadata
// (base size, method slot, method names) needed to drive that.
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

// Singleton registry. Both paths call Register() at the end of a
// successful InsertDispatcher; the diagnostic Python entry reads it.
class DispatcherRegistry {
public:
    static DispatcherRegistry& Instance();
    void Register(std::string class_name, CppyyDispatcher* d);
    // Snapshot for inspection. Pointers stay owned by the registry.
    struct Entry { std::string class_name; CppyyDispatcher* dispatcher; };
    std::vector<Entry> Snapshot() const;
    void Reset();
private:
    std::vector<Entry> fEntries;
};

// Python entry: returns a list of dicts describing every recorded
// (class_name, kind, signature, construction_ms, instances) tuple.
PyObject* DispatcherSummary(PyObject*, PyObject*);
PyObject* DispatcherSummaryReset(PyObject*, PyObject*);

} // namespace CPyCppyy

#endif // CPYCPPYY_CPPYY_DISPATCHER_H
