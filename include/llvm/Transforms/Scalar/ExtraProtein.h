#ifndef LLVM_TRANSFORMS_SCALAR_EXTRAPROTEIN_H
#define LLVM_TRANSFORMS_SCALAR_EXTRAPROTEIN_H

#include "llvm/Pass.h"

namespace llvm {
FunctionPass* createExtraProteinLegacyPass();
} // end namespace llvm

#endif
