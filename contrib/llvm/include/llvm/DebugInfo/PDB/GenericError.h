//===- Error.h - system_error extensions for PDB ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_ERROR_H
#define LLVM_DEBUGINFO_PDB_ERROR_H

#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {

enum class generic_error_code {
  invalid_path = 1,
  dia_sdk_not_present,
  unspecified,
};

/// Base class for errors originating when parsing raw PDB files
class GenericError : public ErrorInfo<GenericError> {
public:
  static char ID;
  GenericError(generic_error_code C);
  GenericError(const std::string &Context);
  GenericError(generic_error_code C, const std::string &Context);

  void log(raw_ostream &OS) const override;
  const std::string &getErrorMessage() const;
  std::error_code convertToErrorCode() const override;

private:
  std::string ErrMsg;
  generic_error_code Code;
};
}
}
#endif
