#ifndef LLVM_TRANSFORMS_SCALAR_EXTRAPROTEIN_H
#define LLVM_TRANSFORMS_SCALAR_EXTRAPROTEIN_H

#include "llvm/Pass.h"

namespace llvm {
FunctionPass* createExtraProteinLegacyPass(uint32_t Duplicate = 2,
                                           uint32_t Amend = 0);
} // end namespace llvm

#endif
