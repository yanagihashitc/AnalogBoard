#pragma once

namespace DialogMainBindingPolicy
{
    template <typename T>
    inline T* ResolveMainDialog(T* currentMainDialog, T* ownerMainDialog)
    {
        return currentMainDialog != nullptr ? currentMainDialog : ownerMainDialog;
    }
}
