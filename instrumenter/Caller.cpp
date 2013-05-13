#include "Caller.h"

using namespace llvm;

namespace assertions {

  /// getIntrinsicID - This method returns the ID number of the specified
  /// function, or Intrinsic::not_intrinsic if the function is not an
  /// intrinsic, or if the pointer is null.  This value is always defined to be
  /// zero to allow easy checking for whether a function is intrinsic or not.
  /// The particular intrinsic functions which correspond to this value are
  /// defined in llvm/Intrinsics.h.  Results are cached in the LLVM context,
  /// subsequent requests for the same ID return results much faster from the
  /// cache.
  ///
  // unsigned getIntrinsicID() const LLVM_READONLY;
  // bool isIntrinsic() const { return getName().startswith("llvm."); }

}