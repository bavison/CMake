/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmGlobalIarEwArmGenerator.h"

#include "cmDocumentationEntry.h"
#include "cmGeneratedFileStream.h"
#include "cmGeneratorTarget.h"
#include "cmGlobalGeneratorFactory.h"
#include "cmLocalGenerator.h"
#include "cmLocalIarEwArmGenerator.h"
#include "cmMakefile.h"
#include "cmStateTypes.h"
#include "cmXMLWriter.h"

class cmGlobalIarEwArmGenerator::Factory : public cmGlobalGeneratorFactory
{
public:
  std::unique_ptr<cmGlobalGenerator> CreateGlobalGenerator(
    const std::string& name, bool allowArch, cmake* cm) const override;

  void GetDocumentation(cmDocumentationEntry& entry) const override
  {
    cmGlobalIarEwArmGenerator::GetDocumentation(entry);
  }

  std::vector<std::string> GetGeneratorNames() const override
  {
    std::vector<std::string> names;
    names.push_back(cmGlobalIarEwArmGenerator::GetActualName());
    return names;
  }

  std::vector<std::string> GetGeneratorNamesWithPlatform() const override
  {
    return std::vector<std::string>();
  }

  bool SupportsToolset() const override { return true; }
  bool SupportsPlatform() const override { return false; }

  std::vector<std::string> GetKnownPlatforms() const override
  {
    return std::vector<std::string>();
  }

  std::string GetDefaultPlatformName() const override { return std::string(); }
};

cmGlobalIarEwArmGenerator::cmGlobalIarEwArmGenerator(cmake* cm) : cmGlobalGenerator(cm)
{
  // TODO
}

std::unique_ptr<cmGlobalGeneratorFactory> cmGlobalIarEwArmGenerator::NewFactory()
{
  return std::unique_ptr<cmGlobalGeneratorFactory>(new Factory);
}

void cmGlobalIarEwArmGenerator::GetDocumentation(cmDocumentationEntry& entry)
{
  entry.Name = cmGlobalIarEwArmGenerator::GetActualName();
  entry.Brief = "Generate IAR Embedded Workbench for Arm project files.";
}

std::unique_ptr<cmLocalGenerator> cmGlobalIarEwArmGenerator::CreateLocalGenerator(
  cmMakefile* mf)
{
  return cm::make_unique<cmLocalIarEwArmGenerator>(this, mf);
}

bool cmGlobalIarEwArmGenerator::FindMakeProgram(cmMakefile*)
{
  // The IarEwArm generator doesn't use a make tool
  return true;
}

void cmGlobalIarEwArmGenerator::Generate()
{
  // First, call Generate() on the base class
  this->cmGlobalGenerator::Generate();

  // Only the first item in the project map makes sense to use to generate an EWW file
  auto project = this->ProjectMap.begin();
  // Build up a list of EWP file paths in a vector
  std::vector<std::string> projects;
  // For each configuration, build up a list of targets that use it, with
  // static libraries segragated from executables
  std::map<std::string,
           std::map<cmStateEnums::TargetType, std::vector<std::string>>>
    config_projects;
  // Remember the absolute path to the top binary dir so we can construct relative paths
  const std::string& top_binary_dir =
    project->second[0]->GetCurrentBinaryDirectory();
  // Go through all all targets, looking for binaries
  for (auto& local_generator : project->second) {
    auto& configs = local_generator->GetMakefile()->GetGeneratorConfigs(
      cmMakefile::IncludeEmptyConfig);
    for (auto& target : local_generator->GetMakefile()->GetTargets()) {
      if (target.second.GetType() == cmStateEnums::EXECUTABLE ||
          target.second.GetType() == cmStateEnums::STATIC_LIBRARY) {
        // Construct path to EWP file in format expected by EWARM
        std::string path = local_generator->GetCurrentBinaryDirectory().substr(top_binary_dir.length());
        std::replace(path.begin(), path.end(), '/', '\\');
        path = "$WS_DIR$" + path + "\\" + target.first + ".ewp";

        projects.push_back(path);

        for (auto const& config : configs)
          config_projects[config][target.second.GetType()].push_back(
            target.first);
      }
    }
  }

  // Write out EWW file
  cmGeneratedFileStream fout(top_binary_dir + "/" + project->first + ".eww");
  fout.SetCopyIfDifferent(true);
  if (!fout) {
    return;
  }
  cmXMLWriter xout(fout);
  xout.SetIndentationElement("    ");
  xout.StartDocument();
  xout.StartElement("workspace");
  for (auto& p : projects) {
    xout.StartElement("project");
    xout.Element("path", p);
    xout.EndElement(); // project
  }
  xout.StartElement("batchBuild");
  for (auto const& batch : config_projects) {
    xout.StartElement("batchDefinition");
    xout.Element("name", "All - " + batch.first);
    for (auto const& p : batch.second.at(cmStateEnums::STATIC_LIBRARY)) {
      xout.StartElement("member");
      xout.Element("project", p);
      xout.Element("configuration", batch.first);
      xout.EndElement(); // member
    }
    for (auto const& p : batch.second.at(cmStateEnums::EXECUTABLE)) {
      xout.StartElement("member");
      xout.Element("project", p);
      xout.Element("configuration", batch.first);
      xout.EndElement(); // member
    }
    xout.EndElement(); // batchDefinition
  }
  xout.EndElement(); // batchBuild
  xout.EndElement(); // workspace
  xout.EndDocument();
}

const std::string& cmGlobalIarEwArmGenerator::FindLibraryPath(
  const std::string& name)
{
  for (auto& local_generator : this->LocalGenerators) {
    auto targets = GetLocalGeneratorTargetsInOrder(local_generator.get());
    for (auto& target : targets) {
      if (target->GetType() == cmStateEnums::STATIC_LIBRARY &&
          target->Target->GetName() == name)
        return target->Target->GetInstallPath();
    }
  }
  static const std::string empty = "";
  return empty;
}

std::unique_ptr<cmGlobalGenerator> cmGlobalIarEwArmGenerator::Factory::CreateGlobalGenerator(
  const std::string& name, bool allowArch, cmake* cm) const
{
  (void) name;
  (void) allowArch;
  (void) cm;
  return std::unique_ptr<cmGlobalGenerator>(cm::make_unique<cmGlobalIarEwArmGenerator>(cm));
}
