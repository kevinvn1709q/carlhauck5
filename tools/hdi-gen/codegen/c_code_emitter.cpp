/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 *
 * HDF is dual licensed: you can use it either under the terms of
 * the GPL, or the BSD license, at your option.
 * See the LICENSE file in the root of this repository for complete details.
 */

#include "codegen/c_code_emitter.h"
#include <cctype>
#include <sys/stat.h>
#include <unistd.h>

namespace OHOS {
namespace HDI {
bool CCodeEmitter::OutPut(const AutoPtr<AST>& ast, const String& targetDirectory)
{
    if (!Reset(ast, targetDirectory)) {
        return false;
    }

    EmitCode();
    return true;
}

bool CCodeEmitter::Reset(const AutoPtr<AST>& ast, const String& targetDirectory)
{
    if (ast == nullptr) {
        return false;
    }

    if (targetDirectory.Equals("")) {
        return false;
    }

    CleanData();
    ast_ = ast;
    if (ast_->GetASTFileType() == ASTFileType::AST_IFACE || ast_->GetASTFileType() == ASTFileType::AST_ICALLBACK) {
        interface_ = ast_->GetInterfaceDef();
        interfaceName_ = interface_->GetName();
        interfaceFullName_ = interface_->GetNamespace()->ToString() + interfaceName_;
        infName_ = interfaceName_.StartsWith("I") ? interfaceName_.Substring(1) : interfaceName_;
        proxyName_ = infName_ + "Proxy";
        proxyFullName_ = interface_->GetNamespace()->ToString() + proxyName_;

        stubName_ = infName_ + "Stub";
        stubFullName_ = interface_->GetNamespace()->ToString() + stubName_;

        implName_ = infName_ + "Service";
        implFullName_ = interface_->GetNamespace()->ToString() + implName_;
    } else if (ast_->GetASTFileType() == ASTFileType::AST_TYPES) {
        infName_ = ast_->GetName();
    }

    if (!ResolveDirectory(targetDirectory)) {
        return false;
    }

    return true;
}

void CCodeEmitter::CleanData()
{
    ast_ = nullptr;
    interface_ = nullptr;
    directory_ = "";
    interfaceName_ = "";
    interfaceFullName_ = "";
    infName_ = "";
    proxyName_ = "";
    proxyFullName_ = "";
    stubName_ = "";
    stubFullName_ = "";
    implName_ = "";
    implFullName_ = "";
}

String CCodeEmitter::FileName(const String& name)
{
    if (name.IsEmpty()) {
        return name;
    }

    StringBuilder sb;

    for (int i = 0; i < name.GetLength(); i++) {
        char c = name[i];
        if (isupper(c) != 0) {
            // 2->Index of the last char array.
            if (i > 1 && name[i - 1] != '.' && name[i - 2] != '.') {
                sb.Append('_');
            }
            sb.Append(tolower(c));
        } else {
            sb.Append(c);
        }
    }

    return sb.ToString().Replace('.', '/');
}

void CCodeEmitter::EmitInterfaceMethodCommands(StringBuilder& sb)
{
    sb.Append("enum {\n");
    for (size_t i = 0; i < interface_->GetMethodNumber(); i++) {
        AutoPtr<ASTMethod> method = interface_->GetMethod(i);
        sb.Append(g_tab).AppendFormat("CMD_%s,\n", ConstantName(method->GetName()).string());
    }
    sb.Append("};\n");
}

void CCodeEmitter::EmitInterfaceMethodParameter(const AutoPtr<ASTParameter>& parameter, StringBuilder& sb,
    const String& prefix)
{
    AutoPtr<ASTType> type = parameter->GetType();
    sb.Append(prefix).Append(parameter->EmitCParameter());
}

void CCodeEmitter::EmitErrorHandle(const AutoPtr<ASTMethod>& method, const String& gotoLabel, bool isClient,
    StringBuilder& sb, const String& prefix)
{
    if (!isClient) {
        sb.Append(prefix).AppendFormat("%s:\n", gotoLabel.string());
        for (int i = 0; i < method->GetParameterNumber(); i++) {
            AutoPtr<ASTParameter> param = method->GetParameter(i);
            AutoPtr<ASTType> paramType = param->GetType();
            paramType->EmitMemoryRecycle(param->GetName(), isClient, true, sb, prefix + g_tab);
        }
        return;
    }

    bool errorLabel = false;
    for (size_t i = 0; i < method->GetParameterNumber(); i++) {
        AutoPtr<ASTParameter> param = method->GetParameter(i);
        AutoPtr<ASTType> paramType = param->GetType();
        if (param->GetAttribute() == ParamAttr::PARAM_OUT &&
            (paramType->GetTypeKind() == TypeKind::TYPE_STRING
            || paramType->GetTypeKind() == TypeKind::TYPE_ARRAY
            || paramType->GetTypeKind() == TypeKind::TYPE_LIST
            || paramType->GetTypeKind() == TypeKind::TYPE_STRUCT
            || paramType->GetTypeKind() == TypeKind::TYPE_UNION)) {
            if (!errorLabel) {
                sb.Append(prefix + g_tab).Append("goto finished;\n");
                sb.Append("\n");
                sb.Append(prefix).AppendFormat("%s:\n", gotoLabel.string());
                errorLabel = true;
            }

            paramType->EmitMemoryRecycle(param->GetName(), isClient, true, sb, prefix + g_tab);
            sb.Append("\n");
        }
    }
}

void CCodeEmitter::EmitLicense(StringBuilder& sb)
{
    if (ast_->GetLicense().IsEmpty()) {
        return;
    }
    sb.Append(ast_->GetLicense()).Append("\n\n");
}

void CCodeEmitter::EmitHeadMacro(StringBuilder& sb, const String& fullName)
{
    String macroName = MacroName(fullName);
    sb.Append("#ifndef ").Append(macroName).Append("\n");
    sb.Append("#define ").Append(macroName).Append("\n");
}

void CCodeEmitter::EmitTailMacro(StringBuilder& sb, const String& fullName)
{
    String macroName = MacroName(fullName);
    sb.Append("#endif // ").Append(macroName);
}

void CCodeEmitter::EmitHeadExternC(StringBuilder& sb)
{
    sb.Append("#ifdef __cplusplus\n");
    sb.Append("extern \"C\" {\n");
    sb.Append("#endif /* __cplusplus */\n");
}

void CCodeEmitter::EmitTailExternC(StringBuilder& sb)
{
    sb.Append("#ifdef __cplusplus\n");
    sb.Append("}\n");
    sb.Append("#endif /* __cplusplus */\n");
}

String CCodeEmitter::MacroName(const String& name)
{
    if (name.IsEmpty()) {
        return name;
    }

    String macro = name.Replace('.', '_').ToUpperCase() + "_H";
    return macro;
}

String CCodeEmitter::ConstantName(const String& name)
{
    if (name.IsEmpty()) {
        return name;
    }

    StringBuilder sb;

    for (int i = 0; i < name.GetLength(); i++) {
        char c = name[i];
        if (isupper(c) != 0) {
            if (i > 1) {
                sb.Append('_');
            }
            sb.Append(c);
        } else {
            sb.Append(toupper(c));
        }
    }

    return sb.ToString();
}

String CCodeEmitter::SpecificationParam(StringBuilder& paramSb, const String& prefix)
{
    int maxLineLen = 120;
    int replaceLen = 2;
    String paramStr = paramSb.ToString();
    int preIndex = 0;
    int curIndex = 0;

    String insertStr = String::Format("\n%s", prefix.string());
    for (; curIndex < paramStr.GetLength(); curIndex++) {
        if (curIndex == maxLineLen && preIndex > 0) {
            paramStr.Replace(preIndex, replaceLen, ",");
            paramStr.insert(preIndex + 1, insertStr);
        } else {
            if (paramStr[curIndex] == ',') {
                preIndex = curIndex;
            }
        }
    }
    return paramStr;
}
} // namespace HDI
} // namespace OHOS