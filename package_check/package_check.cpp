#include "stdafx.h"
#include "package_check.h"

#include "hostQt.h"
#include "nrxgate/nc2ac.h"

namespace {

void package_check_cmd()
{
    QMessageBox::information(
        nullptr,
        QString::fromWCharArray(L"package_check"),
        QString::fromWCharArray(L"Пустой шаблон модуля."));
}

}

package_check::package_check()
{
}

extern "C" __declspec(dllexport) AcRx::AppRetCode
acrxEntryPoint(AcRx::AppMsgCode msg, void* appId)
{
    switch (msg)
    {
    case AcRx::kInitAppMsg:
        acrxDynamicLinker->unlockApplication(appId);
        acrxDynamicLinker->registerAppMDIAware(appId);
        acedRegCmds->addCommand(
            L"PACKAGE_CHECK",
            L"_PACKAGE_CHECK",
            L"PACKAGE_CHECK",
            ACRX_CMD_TRANSPARENT,
            package_check_cmd);
        break;
    case AcRx::kUnloadAppMsg:
        acedRegCmds->removeGroup(L"PACKAGE_CHECK");
        break;
    default:
        break;
    }

    return AcRx::kRetOK;
}
