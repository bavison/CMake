/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include <memory>

#include "cmGlobalGenerator.h"
#include "cmGlobalGeneratorFactory.h"
#include "cmLocalGenerator.h"

class cmGlobalIarEwArmGenerator : public cmGlobalGenerator
{
public:
  cmGlobalIarEwArmGenerator(cmake* cm);
  bool IsMultiConfig() const override { return true; }
  static std::unique_ptr<cmGlobalGeneratorFactory> NewFactory();

  //! Get the name for the generator.
  std::string GetName() const override
  {
    return cmGlobalIarEwArmGenerator::GetActualName();
  }
  static std::string GetActualName() { return "IAR Embedded Workbench for Arm"; }

  /** Get the documentation entry for this generator.  */
  static void GetDocumentation(cmDocumentationEntry& entry);

  //! Create a local generator appropriate to this Global Generator
  std::unique_ptr<cmLocalGenerator> CreateLocalGenerator(
    cmMakefile* mf) override;

  bool FindMakeProgram(cmMakefile*) override;

  const std::string& FindLibraryPath(const std::string& name);

protected:
  void Generate() override;

private:
  class Factory;
};
