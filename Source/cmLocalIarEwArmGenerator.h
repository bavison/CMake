/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "cmGlobalGenerator.h"
#include "cmLocalGenerator.h"
#include "cmMakefile.h"

/** \class cmLocalGhsMultiGenerator
 * \brief Write IAR Embedded Workbench for Arm project files.
 *
 * cmLocalIarEwArmGenerator produces a set of .ewp
 * file for each target in its mirrored directory.
 */
class cmLocalIarEwArmGenerator : public cmLocalGenerator
{
public:
  cmLocalIarEwArmGenerator(cmGlobalGenerator* gg, cmMakefile* mf);

  ~cmLocalIarEwArmGenerator() override;

  void Generate() override;

private:
  void GetDefines(cmGeneratorTarget const* target, std::string const& config,
                  std::string const& lang, std::set<std::string>& defines);

  void GetIncludes(std::string const& proj_dir,
                   cmGeneratorTarget const* target, std::string const& config,
                   std::string const& lang,
                   std::vector<std::string>& includes);
};
