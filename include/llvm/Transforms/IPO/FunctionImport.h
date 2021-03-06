//===- llvm/Transforms/IPO/FunctionImport.h - ThinLTO importing -*- C++ -*-===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_FUNCTIONIMPORT_H
#define LLVM_TRANSFORMS_IPO_FUNCTIONIMPORT_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Error.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace llvm {

class Module;

/// The function importer is automatically importing function from other modules
/// based on the provided summary informations.
class FunctionImporter {
public:
  /// Set of functions to import from a source module. Each entry is a set
  /// containing all the GUIDs of all functions to import for a source module.
  using FunctionsToImportTy = std::unordered_set<GlobalValue::GUID>;

  /// Map of callee GUID considered for import into a given module to a pair
  /// consisting of the largest threshold applied when deciding whether to
  /// import it and, if we decided to import, a pointer to the summary instance
  /// imported. If we decided not to import, the summary will be nullptr.
  using ImportThresholdsTy =
      DenseMap<GlobalValue::GUID,
               std::pair<unsigned, const GlobalValueSummary *>>;

  /// The map contains an entry for every module to import from, the key being
  /// the module identifier to pass to the ModuleLoader. The value is the set of
  /// functions to import.
  using ImportMapTy = StringMap<FunctionsToImportTy>;

  /// The set contains an entry for every global value the module exports.
  using ExportSetTy = std::unordered_set<GlobalValue::GUID>;

  /// A function of this type is used to load modules referenced by the index.
  using ModuleLoaderTy =
      std::function<Expected<std::unique_ptr<Module>>(StringRef Identifier)>;

  /// Create a Function Importer.
  FunctionImporter(const ModuleSummaryIndex &Index, ModuleLoaderTy ModuleLoader)
      : Index(Index), ModuleLoader(std::move(ModuleLoader)) {}

  /// Import functions in Module \p M based on the supplied import list.
  Expected<bool> importFunctions(Module &M, const ImportMapTy &ImportList);

private:
  /// The summaries index used to trigger importing.
  const ModuleSummaryIndex &Index;

  /// Factory function to load a Module for a given identifier
  ModuleLoaderTy ModuleLoader;
};

/// The function importing pass
class FunctionImportPass : public PassInfoMixin<FunctionImportPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Compute all the imports and exports for every module in the Index.
///
/// \p ModuleToDefinedGVSummaries contains for each Module a map
/// (GUID -> Summary) for every global defined in the module.
///
/// \p ImportLists will be populated with an entry for every Module we are
/// importing into. This entry is itself a map that can be passed to
/// FunctionImporter::importFunctions() above (see description there).
///
/// \p ExportLists contains for each Module the set of globals (GUID) that will
/// be imported by another module, or referenced by such a function. I.e. this
/// is the set of globals that need to be promoted/renamed appropriately.
void ComputeCrossModuleImport(
    const ModuleSummaryIndex &Index,
    const StringMap<GVSummaryMapTy> &ModuleToDefinedGVSummaries,
    StringMap<FunctionImporter::ImportMapTy> &ImportLists,
    StringMap<FunctionImporter::ExportSetTy> &ExportLists);

/// Compute all the imports for the given module using the Index.
///
/// \p ImportList will be populated with a map that can be passed to
/// FunctionImporter::importFunctions() above (see description there).
void ComputeCrossModuleImportForModule(
    StringRef ModulePath, const ModuleSummaryIndex &Index,
    FunctionImporter::ImportMapTy &ImportList);

/// Mark all external summaries in \p Index for import into the given module.
/// Used for distributed builds using a distributed index.
///
/// \p ImportList will be populated with a map that can be passed to
/// FunctionImporter::importFunctions() above (see description there).
void ComputeCrossModuleImportForModuleFromIndex(
    StringRef ModulePath, const ModuleSummaryIndex &Index,
    FunctionImporter::ImportMapTy &ImportList);

/// PrevailingType enum used as a return type of callback passed
/// to computeDeadSymbols. Yes and No values used when status explicitly
/// set by symbols resolution, otherwise status is Unknown.
enum class PrevailingType { Yes, No, Unknown };

/// Compute all the symbols that are "dead": i.e these that can't be reached
/// in the graph from any of the given symbols listed in
/// \p GUIDPreservedSymbols. Non-prevailing symbols are symbols without a
/// prevailing copy anywhere in IR and are normally dead, \p isPrevailing
/// predicate returns status of symbol.
void computeDeadSymbols(
    ModuleSummaryIndex &Index,
    const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols,
    function_ref<PrevailingType(GlobalValue::GUID)> isPrevailing);

/// Converts value \p GV to declaration, or replaces with a declaration if
/// it is an alias. Returns true if converted, false if replaced.
bool convertToDeclaration(GlobalValue &GV);

/// Compute the set of summaries needed for a ThinLTO backend compilation of
/// \p ModulePath.
//
/// This includes summaries from that module (in case any global summary based
/// optimizations were recorded) and from any definitions in other modules that
/// should be imported.
//
/// \p ModuleToSummariesForIndex will be populated with the needed summaries
/// from each required module path. Use a std::map instead of StringMap to get
/// stable order for bitcode emission.
void gatherImportedSummariesForModule(
    StringRef ModulePath,
    const StringMap<GVSummaryMapTy> &ModuleToDefinedGVSummaries,
    const FunctionImporter::ImportMapTy &ImportList,
    std::map<std::string, GVSummaryMapTy> &ModuleToSummariesForIndex);

/// Emit into \p OutputFilename the files module \p ModulePath will import from.
std::error_code EmitImportsFiles(
    StringRef ModulePath, StringRef OutputFilename,
    const std::map<std::string, GVSummaryMapTy> &ModuleToSummariesForIndex);

/// Resolve WeakForLinker values in \p TheModule based on the information
/// recorded in the summaries during global summary-based analysis.
void thinLTOResolveWeakForLinkerModule(Module &TheModule,
                                       const GVSummaryMapTy &DefinedGlobals);

/// Internalize \p TheModule based on the information recorded in the summaries
/// during global summary-based analysis.
void thinLTOInternalizeModule(Module &TheModule,
                              const GVSummaryMapTy &DefinedGlobals);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_FUNCTIONIMPORT_H
