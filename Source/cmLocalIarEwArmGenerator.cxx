/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmLocalIarEwArmGenerator.h"

#include "cmGeneratedFileStream.h"
#include "cmGeneratorExpression.h"
#include "cmGeneratorTarget.h"
#include "cmSourceFile.h"
#include "cmXMLWriter.h"

cmLocalIarEwArmGenerator::cmLocalIarEwArmGenerator(cmGlobalGenerator* gg,
                                                   cmMakefile* mf)
  : cmLocalGenerator(gg, mf)
{
}

cmLocalIarEwArmGenerator::~cmLocalIarEwArmGenerator() = default;

static void option(cmXMLWriter& xout, std::string const& name)
{
  xout.StartElement("option");
  xout.Element("name", name);
  xout.EndElement(); // option
}

template <typename T>
static void option(cmXMLWriter& xout, std::string const& name,
                   T const& state)
{
  xout.StartElement("option");
  xout.Element("name", name);
  xout.Element("state", state);
  xout.EndElement(); // option
}

template <>
static void option<std::set<std::string>>(
  cmXMLWriter& xout, std::string const& name,
  std::set<std::string> const& state)
{
  xout.StartElement("option");
  xout.Element("name", name);
  for (auto& s : state) {
    xout.Element("state", s);
  }
  xout.EndElement(); // option
}

template <>
static void option<std::vector<std::string>>(cmXMLWriter& xout,
                                             std::string const& name,
                                             std::vector<std::string> const& state)
{
  xout.StartElement("option");
  xout.Element("name", name);
  for (auto& s : state) {
    xout.Element("state", s);
  }
  xout.EndElement(); // option
}

template <typename T>
static void option(cmXMLWriter& xout, std::string const& name,
                   unsigned int version, T const& state)
{
  xout.StartElement("option");
  xout.Element("name", name);
  xout.Element("version", version);
  xout.Element("state", state);
  xout.EndElement(); // option
}

/* Convert an internal CMake path (e.g. C:/dir/dir/file.c) to the format
 * accepted by Embedded Workbench, if possible relative to $PROJ_DIR$
 * (e.g. $PROJ_DIR$\..\..\dir\file.c)
 */
static std::string canonicalise(std::string proj_dir, std::string path)
{
  std::string result;

  /* First add a trailing '/' to ensure we always match complete leafnames */
  proj_dir += '/';
  path += '/';

  /* Check they are on the same volume - if not, then relative path is not
   * possible */
  if (proj_dir[0] != path[0] || proj_dir[1] != ':' || path[1] != ':' ||
      proj_dir[2] != '/' || path[2] != '/') {
    result = path;
  }
  else {
    result = "$PROJ_DIR$/";
    for (;;) {
      if (std::strncmp(path.c_str(), proj_dir.c_str(), proj_dir.length()) ==
          0) {
        result += path.substr(proj_dir.length());
        break;
      }
      result += "../";
      proj_dir.erase(proj_dir.find_last_of("/", proj_dir.length() - 2) + 1);
    }
  }

  /* Remove the trailing '/' now we're done with it */
  result.erase(result.length() - 1);

  /* Finally convert path separators to DOS style */
  std::replace(result.begin(), result.end(), '/', '\\');

  return result;
}

/* Split a string by a separator character into a vector of strings */
static std::vector<std::string> split(std::string in, char sep)
{
  std::vector<std::string> result;
  if (in.empty())
    return result;
  for (size_t i = 0, j;; i = j + 1) {
    j = in.find(sep, i);
    if (j == std::string::npos) {
      result.push_back(in.substr(i));
      break;
    }
    result.push_back(in.substr(i, j-i));
  }
  return result;
}

/* Join a vector of strings into a single string, inserting separator characters */
static std::string join(std::vector<std::string> in, char sep)
{
  std::string result;
  for (auto s : in) {
    if (!result.empty())
      result += sep;
    result += s;
  }
  return result;
}

void cmLocalIarEwArmGenerator::GetDefines(cmGeneratorTarget const* target,
                                          std::string const& config,
                                          std::string const& lang,
                                          std::set<std::string>& defines)
{
  // Most of the preprocessor defines can be obtained using GetTargetDefines()
  this->GetTargetDefines(target, config, lang, defines);
  // But some are hiding in the FLAGS variables
  // For simplicity, assume spaces don't occur inside any string variable defintions
  std::string flags_var = config;
  std::transform(flags_var.begin(), flags_var.end(), flags_var.begin(),
                 ::toupper);
  flags_var = "CMAKE_" + lang + "_FLAGS_" + flags_var;
  std::string flags = " " + *this->Makefile->GetDefinition(flags_var).Get();
  const char *start = flags.c_str(), *end = start;
  while (start && end) {
    start = std::strstr(end, " -D");
    if (start) {
      end = std::strchr(start + 3, ' ');
      if (end)
        defines.emplace(start + 3, end - start - 3);
      else
        defines.emplace(start + 3);
    }
  }
}

void cmLocalIarEwArmGenerator::GetIncludes(std::string const& proj_dir,
                                           cmGeneratorTarget const* target,
                                           std::string const& config,
                                           std::string const& lang,
                                           std::vector<std::string>& includes)
{
  // Most of the preprocessor defines can be obtained using GetTargetDefines()
  this->GetIncludeDirectories(includes, target, lang, config);
  for (auto& dir : includes) {
    dir = canonicalise(proj_dir, dir);
  }
}

struct Dir
{
  std::map<std::string, std::unique_ptr<Dir>> subdirs;
  std::set<std::string> files;
};

void cmLocalIarEwArmGenerator::Generate()
{
  // First, call Generate() on the base class
  cmLocalGenerator::Generate();

  // Filter for executable targets
  for (auto& target : this->GetGeneratorTargets()) {
    if (target->GetType() == cmStateEnums::EXECUTABLE) {

      // Gather config-invariant information
      std::string runtime_lib_select_var =
        this->Makefile->GetDefinition("CMAKE_IAR_RUNTIME_LIB_SELECT");
      int runtime_lib_select =
        runtime_lib_select_var.empty() ? 1 : stoi(runtime_lib_select_var);
      std::string runtime_config_path;
      std::string runtime_config_description;
      switch (runtime_lib_select) {
        case 0:
            runtime_config_description =
                   "Do not link with a runtime library.";
            break;
        case 1:
          runtime_config_path = "$TOOLKIT_DIR$\\inc\\c\\DLib_Config_Normal.h";
          runtime_config_description =
                   "Use the normal configuration of the C/C++ runtime "
                   "library. No locale interface, C locale, no file "
                   "descriptor support, no multibytes in printf and "
                   "scanf, and no hex floats in strtod.";
          break;
        case 2:
          runtime_config_path = "$TOOLKIT_DIR$\\inc\\c\\DLib_Config_Full.h";
          runtime_config_description =
                   "Use the full configuration of the C/C++ runtime "
                   "library. Full locale interface, C locale, file "
                   "descriptor support, multibytes in printf and scanf, "
                   "and hex floats in strtod.";
          break;
        case 3:
          runtime_config_path =
            this->Makefile->GetDefinition("CMAKE_IAR_RUNTIME_CONFIG_PATH");
          runtime_config_description =
                   "Use a customized C/C++ runtime library.";
          break;
      }

      // When a .EWP file refers to a chip, it's in the form of
      // <chip><tab><vendor><space><chip>
      // To keep our CMake variable simple, only require it to hold
      // <vendor><space><chip>
      // and construct the full version from that.
      std::string chip_select_var =
        this->Makefile->GetDefinition("CMAKE_IAR_CHIP_SELECT");
      std::string chip_select;
      if (!chip_select_var.empty()) {
        std::string::reverse_iterator c = chip_select_var.rbegin();
        while (c != chip_select_var.rend() && !std::isspace(*c))
          ++c;
        while (c != chip_select_var.rbegin())
          chip_select += *--c;
        chip_select += "\t" + chip_select_var;
      }

      std::string c_diag_suppress =
        this->Makefile->GetDefinition("CMAKE_IAR_C_DIAG_SUPPRESS");
      std::string asm_diag_suppress =
        this->Makefile->GetDefinition("CMAKE_IAR_ASM_DIAG_SUPPRESS");
      std::string asm_diag_suppress_range_1;
      std::string asm_diag_suppress_range_2;
      bool asm_diag_suppress_range =
        asm_diag_suppress.find_first_of("-") != std::string::npos;
      if (asm_diag_suppress_range) {
        asm_diag_suppress_range_1 =
          asm_diag_suppress.substr(0, asm_diag_suppress.find_first_of("-"));
        asm_diag_suppress_range_2 =
          asm_diag_suppress.substr(asm_diag_suppress.find_first_of("-") + 1);
      }
      std::string custom_extensions =
        this->Makefile->GetDefinition("CMAKE_IAR_CUSTOM_EXTENSIONS");
      std::vector<std::string> custom_cmdline_vector =
        split(this->Makefile->GetDefinition("CMAKE_IAR_CUSTOM_CMDLINE"), ' ');
      // Assume any element of the custom command line may be a filespec
      for (auto& f : custom_cmdline_vector)
        f = canonicalise(this->GetCurrentBinaryDirectory(), f);
      std::string custom_cmdline = join(custom_cmdline_vector, ' ');
      std::string custom_build_sequence =
        this->Makefile->GetDefinition("CMAKE_IAR_CUSTOM_BUILD_SEQUENCE");
      if (custom_build_sequence.empty())
        custom_build_sequence = "inputOutputBased";
      std::vector<std::string> custom_outputs =
        split(this->Makefile->GetDefinition("CMAKE_IAR_CUSTOM_OUTPUTS"), ' ');
      for (auto& f : custom_outputs)
        f = canonicalise(this->GetCurrentBinaryDirectory(), f);
      std::vector<std::string> custom_inputs =
        split(this->Makefile->GetDefinition("CMAKE_IAR_CUSTOM_INPUTS"), ' ');
      for (auto& f : custom_inputs)
        f = canonicalise(this->GetCurrentBinaryDirectory(), f);
      std::string ilink_keep_symbols =
        this->Makefile->GetDefinition("CMAKE_IAR_ILINK_KEEP_SYMBOLS");
      std::string ilink_icf_file_expr =
        this->Makefile->GetDefinition("CMAKE_IAR_ILINK_ICF_FILE");
      std::string ilink_program_entry_label =
        this->Makefile->GetDefinition("CMAKE_IAR_ILINK_PROGRAM_ENTRY_LABEL");
      std::string do_fill =
        this->Makefile->GetDefinition("CMAKE_IAR_DO_FILL");
      std::string filler_byte =
        this->Makefile->GetDefinition("CMAKE_IAR_FILLER_BYTE");
      std::string filler_start =
        this->Makefile->GetDefinition("CMAKE_IAR_FILLER_START");
      std::string filler_end =
        this->Makefile->GetDefinition("CMAKE_IAR_FILLER_END");
      std::string crc_size =
        this->Makefile->GetDefinition("CMAKE_IAR_CRC_SIZE");
      std::string crc_initial_value =
        this->Makefile->GetDefinition("CMAKE_IAR_CRC_INITIAL_VALUE");
      std::string do_crc =
        this->Makefile->GetDefinition("CMAKE_IAR_DO_CRC");
      std::string ilink_crc_use_as_input =
        this->Makefile->GetDefinition("CMAKE_IAR_ILINK_CRC_USE_AS_INPUT");
      std::string crc_algorithm =
        this->Makefile->GetDefinition("CMAKE_IAR_CRC_ALGORITHM");

      // Write out EWP file
      cmGeneratedFileStream fout(this->GetCurrentBinaryDirectory() + "/" +
                                 target->GetName() + ".ewp");
      fout.SetCopyIfDifferent(true);
      if (!fout) {
        return;
      }
      cmXMLWriter xout(fout);
      xout.SetIndentationElement("    ");
      xout.StartDocument();
      xout.StartElement("project");
      xout.Element("fileVersion", 3);
      for (auto const& config : this->Makefile->GetGeneratorConfigs(
             cmMakefile::IncludeEmptyConfig)) {

        // Gather config-dependent information
        std::set<std::string> c_defines;
        GetDefines(target.get(), config, "C", c_defines);
        std::vector<std::string> c_includes;
        GetIncludes(this->GetCurrentBinaryDirectory(), target.get(), config,
                    "C", c_includes);
        std::set<std::string> asm_defines;
        GetDefines(target.get(), config, "ASM", asm_defines);
        std::vector<std::string> asm_includes;
        GetIncludes(this->GetCurrentBinaryDirectory(), target.get(), config,
                    "ASM", asm_includes);
        std::string ilink_icf_file;
        if (!ilink_icf_file_expr.empty()) {
          ilink_icf_file = cmGeneratorExpression::Evaluate(
            ilink_icf_file_expr, this, config, target.get());
        }

        xout.StartElement("configuration");
        xout.Element("name", config);
        xout.StartElement("toolchain");
        xout.Element("name", "ARM");
        xout.EndElement(); // toolchain
        // "debug" appears to reflect the "Factory settings" selected when the
        // configuration is created, and apparently cannot be changed thereafter
        xout.Element("debug", config == "Debug" ? 1 : 0);

        xout.StartElement("settings");
        xout.Element("name", "General");
        xout.Element("archiveVersion", 3);
        xout.StartElement("data");
        xout.Element("version", 34);
        xout.Element("wantNonLocal", 1);
        xout.Element("debug", config == "Debug" ? 1 : 0);
        option(xout, "ExePath", config + "\\Exe");
        option(xout, "ObjPath", config + "\\Obj");
        option(xout, "ListPath", config + "\\List");
        option(xout, "BrowseInfoPath", config + "\\BrowseInfo");
        option(xout, "GEndianMode", 0);
        option(xout, "Input description", "Automatic choice of formatter, without multibyte support.");
        option(xout, "Output description", "Automatic choice of formatter, without multibyte support.");
        option(xout, "GOutputBinary", 0);
        option(xout, "OGCoreOrChip", chip_select.empty() ? 0 : 1);
        option(xout, "GRuntimeLibSelect", 0, runtime_lib_select);
        option(xout, "GRuntimeLibSelectSlave", 0, runtime_lib_select);
        option(xout, "RTDescription", runtime_config_description);
        option(xout, "OGProductVersion", "9.20.4.46976");
        option(xout, "OGLastSavedByProductVersion", "9.20.4.46976");
        option(xout, "OGChipSelectEditMenu", chip_select);
        option(xout, "GenLowLevelInterface", 1);
        option(xout, "GEndianModeBE", 1);
        option(xout, "OGBufferedTerminalOutput", 0);
        option(xout, "GenStdoutInterface", 0);
        option(xout, "RTConfigPath2", runtime_config_path);
        option(xout, "GBECoreSlave", 31, 35);
        option(xout, "OGUseCmsis", 0);
        option(xout, "OGUseCmsisDspLib", 0);
        option(xout, "GRuntimeLibThreads", 0);
        option(xout, "CoreVariant", 31, 35);
        option(xout, "GFPUDeviceSlave", chip_select);
        option(xout, "FPU2", 0, 0);
        option(xout, "NrRegs", 0, 0);
        option(xout, "NEON", 0);
        option(xout, "GFPUCoreSlave2", 31, 35);
        option(xout, "OGCMSISPackSelectDevice");
        option(xout, "OgLibHeap", 0);
        option(xout, "OGLibAdditionalLocale", 0);
        option(xout, "OGPrintfVariant", 0, 0);
        option(xout, "OGPrintfMultibyteSupport", 0);
        option(xout, "OGScanfVariant", 0, 0);
        option(xout, "OGScanfMultibyteSupport", 0);
        option(xout, "GenLocaleTags", "");
        option(xout, "GenLocaleDisplayOnly", "");
        option(xout, "DSPExtension", "0");
        option(xout, "TrustZone", 0);
        option(xout, "TrustZoneModes", 0, 0);
        option(xout, "OGAarch64Abi", 0);
        option(xout, "OG_32_64Device", 0);
        option(xout, "BuildFilesPath", config + "\\");
        xout.EndElement(); // data
        xout.EndElement(); // settings

        xout.StartElement("settings");
        xout.Element("name", "ICCARM");
        xout.Element("archiveVersion", 2);
        xout.StartElement("data");
        xout.Element("version", 37);
        xout.Element("wantNonLocal", 1);
        xout.Element("debug", config == "Debug" ? 1 : 0);
        option(xout, "CCDefines", c_defines);
        option(xout, "CCPreprocFile", 0);
        option(xout, "CCPreprocComments", 0);
        option(xout, "CCPreprocLine", 1);
        option(xout, "CCListCFile", 0);
        option(xout, "CCListCMnemonics", 0);
        option(xout, "CCListCMessages", 0);
        option(xout, "CCListAssFile", 0);
        option(xout, "CCListAssSource", 0);
        option(xout, "CCEnableRemarks", 0);
        option(xout, "CCDiagSuppress", c_diag_suppress);
        option(xout, "CCDiagRemark", "");
        option(xout, "CCDiagWarning", "");
        option(xout, "CCDiagError", "");
        option(xout, "CCObjPrefix", 1);
        option(xout, "CCAllowList", 1, "00000000");
        option(xout, "CCDebugInfo", 1);
        option(xout, "IEndianMode", 1);
        option(xout, "IProcessor", 1);
        option(xout, "IExtraOptionsCheck", 0);
        option(xout, "IExtraOptions", "");
        option(xout, "CCLangConformance", 0);
        option(xout, "CCSignedPlainChar", 1);
        option(xout, "CCRequirePrototypes", 0);
        option(xout, "CCDiagWarnAreErr", 0);
        option(xout, "CCCompilerRuntimeInfo", 0);
        option(xout, "IFpuProcessor", 1);
        option(xout, "OutputFile", "$FILE_BNAME$.o");
        option(xout, "CCLibConfigHeader", 1);
        option(xout, "PreInclude", "");
        option(xout, "CCIncludePath2", c_includes);
        option(xout, "CCStdIncCheck", 0);
        option(xout, "CCCodeSection", ".text");
        option(xout, "IProcessorMode2", 1);
        option(xout, "CCOptLevel", 1);
        option(xout, "CCOptStrategy", 0, 0);
        option(xout, "CCOptLevelSlave", 1);
        option(xout, "CCPosIndRopi", 0);
        option(xout, "CCPosIndRwpi", 0);
        option(xout, "CCPosIndNoDynInit", 0);
        option(xout, "IccLang", 2);
        option(xout, "IccCDialect", 1);
        option(xout, "IccAllowVLA", 0);
        option(xout, "IccStaticDestr", 1);
        option(xout, "IccCppInlineSemantics", 0);
        option(xout, "IccCmsis", 1);
        option(xout, "IccFloatSemantics", 0);
        option(xout, "CCOptimizationNoSizeConstraints", 0);
        option(xout, "CCNoLiteralPool", 0);
        option(xout, "CCOptStrategySlave", 0, 0);
        option(xout, "CCGuardCalls", 1);
        option(xout, "CCEncSource", 0);
        option(xout, "CCEncOutput", 0);
        option(xout, "CCEncOutputBom", 1);
        option(xout, "CCEncInput", 0);
        option(xout, "IccExceptions2", 0);
        option(xout, "IccRTTI2", 0);
        option(xout, "OICompilerExtraOption", 1);
        option(xout, "CCStackProtection", 0);
        xout.EndElement(); // data
        xout.EndElement(); // settings

        xout.StartElement("settings");
        xout.Element("name", "AARM");
        xout.Element("archiveVersion", 2);
        xout.StartElement("data");
        xout.Element("version", 11);
        xout.Element("wantNonLocal", 1);
        xout.Element("debug", config == "Debug" ? 1 : 0);
        option(xout, "AObjPrefix", 1);
        option(xout, "AEndian", 1);
        option(xout, "ACaseSensitivity", 1);
        option(xout, "MacroChars", 0, 0);
        option(xout, "AWarnEnable", asm_diag_suppress.empty() ? 0 : 1);
        option(xout, "AWarnWhat", asm_diag_suppress.empty() ? 0 : asm_diag_suppress_range ? 2 : 1);
        option(xout, "AWarnOne", asm_diag_suppress_range ? "" : asm_diag_suppress);
        option(xout, "AWarnRange1", asm_diag_suppress_range_1);
        option(xout, "AWarnRange2", asm_diag_suppress_range_2);
        option(xout, "ADebug", 1);
        option(xout, "AltRegisterNames", 0);
        option(xout, "ADefines", asm_defines);
        option(xout, "AList", 0);
        option(xout, "AListHeader", 1);
        option(xout, "AListing", 1);
        option(xout, "Includes", 0);
        option(xout, "MacDefs", 0);
        option(xout, "MacExps", 1);
        option(xout, "MacExec", 0);
        option(xout, "OnlyAssed", 0);
        option(xout, "MultiLine", 0);
        option(xout, "PageLengthCheck", 0);
        option(xout, "PageLength", 80);
        option(xout, "TabSpacing", 8);
        option(xout, "AXRef", 0);
        option(xout, "AXRefDefines", 0);
        option(xout, "AXRefInternal", 0);
        option(xout, "AXRefDual", 0);
        option(xout, "AProcessor", 1);
        option(xout, "AFpuProcessor", 1);
        option(xout, "AOutputFile", "$FILE_BNAME$.o");
        option(xout, "ALimitErrorsCheck", 0);
        option(xout, "ALimitErrorsEdit", 100);
        option(xout, "AIgnoreStdInclude", 0);
        option(xout, "AUserIncludes", asm_includes);
        option(xout, "AExtraOptionsCheckV2", 0);
        option(xout, "AExtraOptionsV2", "");
        option(xout, "AsmNoLiteralPool", 0);
        option(xout, "PreInclude", "");
        xout.EndElement(); // data
        xout.EndElement(); // settings

        xout.StartElement("settings");
        xout.Element("name", "OBJCOPY");
        xout.Element("archiveVersion", 0);
        xout.StartElement("data");
        xout.Element("version", 1);
        xout.Element("wantNonLocal", 1);
        xout.Element("debug", config == "Debug" ? 1 : 0);
        option(xout, "OOCOutputFormat", 3, 0);
        option(xout, "OCOutputOverride", 0);
        option(xout, "OOCOutputFile", target->GetName() + ".srec");
        option(xout, "OOCCommandLineProducer", 1);
        option(xout, "OOCObjCopyEnable", 0);
        xout.EndElement(); // data
        xout.EndElement(); // settings

        xout.StartElement("settings");
        xout.Element("name", "CUSTOM");
        xout.Element("archiveVersion", 3);
        xout.StartElement("data");
        xout.Element("extensions", custom_extensions);
        xout.Element("cmdline", custom_cmdline);
        xout.Element("hasPrio", 1);
        xout.Element("buildSequence", custom_build_sequence);
        if (!custom_outputs.empty()) {
          xout.StartElement("outputs");
          for (auto& f : custom_outputs) {
            xout.StartElement("file");
            xout.Element("name", f);
            xout.EndElement(); // file
          }
          xout.EndElement(); // outputs
        }
        if (!custom_inputs.empty()) {
          xout.StartElement("inputs");
          for (auto& f : custom_inputs) {
            xout.StartElement("file");
            xout.Element("name", f);
            xout.EndElement(); // file
          }
          xout.EndElement(); // outputs
        }
        xout.EndElement(); // data
        xout.EndElement(); // settings

        xout.StartElement("settings");
        xout.Element("name", "BUILDACTION");
        xout.Element("archiveVersion", 1);
        xout.StartElement("data");
        xout.Element("prebuild", "");
        xout.Element("postbuild", "");
        xout.EndElement(); // data
        xout.EndElement(); // settings

        xout.StartElement("settings");
        xout.Element("name", "ILINK");
        xout.Element("archiveVersion", 0);
        xout.StartElement("data");
        xout.Element("version", 26);
        xout.Element("wantNonLocal", 1);
        xout.Element("debug", config == "Debug" ? 1 : 0);
        option(xout, "IlinkLibIOConfig", 1);
        option(xout, "IlinkInputFileSlave", 0);
        option(xout, "IlinkOutputFile", target->GetName() + ".out");
        option(xout, "IlinkDebugInfoEnable", 1);
        option(xout, "IlinkKeepSymbols", ilink_keep_symbols);
        option(xout, "IlinkRawBinaryFile", "");
        option(xout, "IlinkRawBinarySymbol", "");
        option(xout, "IlinkRawBinarySegment", "");
        option(xout, "IlinkRawBinaryAlign", "");
        option(xout, "IlinkDefines", "");
        option(xout, "IlinkConfigDefines", "");
        option(xout, "IlinkMapFile", 1);
        option(xout, "IlinkLogFile", 0);
        option(xout, "IlinkLogInitialization", 0);
        option(xout, "IlinkLogModule", 0);
        option(xout, "IlinkLogSection", 0);
        option(xout, "IlinkLogVeneer", 0);
        option(xout, "IlinkIcfOverride", ilink_icf_file_expr.empty() ? 0 : 1);
        option(xout, "IlinkIcfFile",
          ilink_icf_file_expr.empty()
            ? "lnk0t.icf"
            : canonicalise(this->GetCurrentBinaryDirectory(), ilink_icf_file));
        option(xout, "IlinkIcfFileSlave", "");
        option(xout, "IlinkEnableRemarks", 0);
        option(xout, "IlinkSuppressDiags", "");
        option(xout, "IlinkTreatAsRem", "");
        option(xout, "IlinkTreatAsWarn", "");
        option(xout, "IlinkTreatAsErr", "");
        option(xout, "IlinkWarningsAreErrors", 0);
        option(xout, "IlinkUseExtraOptions", 0);
        option(xout, "IlinkExtraOptions", "");
        option(xout, "IlinkLowLevelInterfaceSlave", 1);
        option(xout, "IlinkAutoLibEnable", 1);
        option(xout, "IlinkAdditionalLibs", "");
        option(xout, "IlinkOverrideProgramEntryLabel", ilink_program_entry_label.empty() ? 0 : 1);
        option(xout, "IlinkProgramEntryLabelSelect", 0);
        option(xout, "IlinkProgramEntryLabel", ilink_program_entry_label);
        option(xout, "DoFill", do_fill.empty() ? "0" : do_fill);
        option(xout, "FillerByte", filler_byte.empty() ? "0xFF" : filler_byte);
        option(xout, "FillerStart", filler_start.empty() ? "0x0" : filler_start);
        option(xout, "FillerEnd", filler_end.empty() ? "0x0" : filler_end);
        option(xout, "CrcSize", 0, crc_size.empty() ? "1" : crc_size);
        option(xout, "CrcAlign", 1);
        option(xout, "CrcPoly", "0x11021");
        option(xout, "CrcCompl", 0, 0);
        option(xout, "CrcBitOrder", 0, 0);
        option(xout, "CrcInitialValue", crc_initial_value.empty() ? "0x0" : crc_initial_value);
        option(xout, "DoCrc", do_crc.empty() ? "0" : do_crc);
        option(xout, "IlinkBE8Slave", 1);
        option(xout, "IlinkBufferedTerminalOutput", 1);
        option(xout, "IlinkStdoutInterfaceSlave", 1);
        option(xout, "CrcFullSize", 0);
        option(xout, "IlinkIElfToolPostProcess", 0);
        option(xout, "IlinkLogAutoLibSelect", 0);
        option(xout, "IlinkLogRedirSymbols", 0);
        option(xout, "IlinkLogUnusedFragments", 0);
        option(xout, "IlinkCrcReverseByteOrder", 0);
        option(xout, "IlinkCrcUseAsInput", ilink_crc_use_as_input.empty() ? "1" : ilink_crc_use_as_input);
        option(xout, "IlinkOptInline", "0");
        option(xout, "IlinkOptExceptionsAllow", 1);
        option(xout, "IlinkOptExceptionsForce", 0);
        option(xout, "IlinkCmsis", 1);
        option(xout, "IlinkOptMergeDuplSections", 0);
        option(xout, "IlinkOptUseVfe", 1);
        option(xout, "IlinkOptForceVfe", 0);
        option(xout, "IlinkStackAnalysisEnable", 0);
        option(xout, "IlinkStackControlFile", "");
        option(xout, "IlinkStackCallGraphFile", "");
        option(xout, "CrcAlgorithm", 1, crc_algorithm.empty() ? "1" : crc_algorithm);
        option(xout, "CrcUnitSize", 0, 0);
        option(xout, "IlinkThreadsSlave", 1);
        option(xout, "IlinkLogCallGraph", 0);
        option(xout, "IlinkIcfFile_AltDefault", "");
        option(xout, "IlinkEncInput", 0);
        option(xout, "IlinkEncOutput", 0);
        option(xout, "IlinkEncOutputBom", 1);
        option(xout, "IlinkHeapSelect", 1);
        option(xout, "IlinkLocaleSelect", 1);
        option(xout, "IlinkTrustzoneImportLibraryOut", target->GetName() + "_import_lib.o");
        option(xout, "OILinkExtraOption", 1);
        option(xout, "IlinkRawBinaryFile2", "");
        option(xout, "IlinkRawBinarySymbol2", "");
        option(xout, "IlinkRawBinarySegment2", "");
        option(xout, "IlinkRawBinaryAlign2", "");
        option(xout, "IlinkLogCrtRoutineSelection", 0);
        option(xout, "IlinkLogFragmentInfo", 0);
        option(xout, "IlinkLogInlining", 0);
        option(xout, "IlinkLogMerging", 0);
        option(xout, "IlinkDemangle", 0);
        option(xout, "IlinkWrapperFileEnable", 0);
        option(xout, "IlinkWrapperFile", "");
        xout.EndElement(); // data
        xout.EndElement(); // settings

        xout.StartElement("settings");
        xout.Element("name", "IARCHIVE");
        xout.Element("archiveVersion", 0);
        xout.StartElement("data");
        xout.Element("version", 0);
        xout.Element("wantNonLocal", 1);
        xout.Element("debug", config == "Debug" ? 1 : 0);
        option(xout, "IarchiveInputs", "");
        option(xout, "IarchiveOverride", 0);
        option(xout, "IarchiveOutput", "###Unitialized###");
        xout.EndElement(); // data
        xout.EndElement(); // settings
        xout.EndElement(); // configuration
      }

      // IarEwArm doesn't support differing sets of source files between
      // configurations, so just pick the first configuration arbitrarily.
      const std::string & config = this->Makefile->GetGeneratorConfigs(
             cmMakefile::IncludeEmptyConfig)[0];
      std::vector<cmSourceFile*> sources;
      target->GetSourceFiles(sources, config);

      // Source files need to be grouped to permit multiple source files
      // with the same leafname to co-exist in the same project. These
      // groups also appear in the IDE, so to make it as simple to navigate
      // as possible, delete the common root from all pathnames before
      // using them to determine the group structure.
      Dir root;
      for (auto& source : sources) {
        Dir* d = &root;
        std::string path = source->GetFullPath();
        std::string canon =
          canonicalise(this->GetCurrentBinaryDirectory(), path);
        for (;;) {
          size_t i = path.find_first_of("/");
          if (i == std::string::npos) {
            // Reached leaf
            d->files.emplace(canon);
            break;
          }
          std::string subdir = path.substr(0, i);
          path = path.substr(i + 1);
          d = d->subdirs.emplace(subdir, std::make_unique<Dir>())
                .first->second.get();
        }
      }
      Dir* d = &root;
      while (d->subdirs.size() == 1 && d->files.size() == 0)
        d = d->subdirs.begin()->second.get();
      std::map<std::string, std::unique_ptr<Dir>>::iterator i =
        d->subdirs.begin();
      std::vector<
        std::pair<Dir*, std::map<std::string, std::unique_ptr<Dir>>::iterator>>
        breadcrumbs;
      for (;;) {
        if (i != d->subdirs.end()) {
          xout.StartElement("group");
          xout.Element("name", i->first);
          breadcrumbs.push_back(std::make_pair(d, i));
          d = i->second.get();
          i = d->subdirs.begin();
        } else {
          for (auto& file : d->files) {
            xout.StartElement("file");
            xout.Element("name", file);
            xout.EndElement(); // file
          }
          if (breadcrumbs.empty())
            break;
          xout.EndElement(); // group
          d = breadcrumbs.back().first;
          i = breadcrumbs.back().second;
          breadcrumbs.pop_back();
          ++i;
        }
      }

      xout.EndElement(); // project
      xout.EndDocument();
    }
  }
}
