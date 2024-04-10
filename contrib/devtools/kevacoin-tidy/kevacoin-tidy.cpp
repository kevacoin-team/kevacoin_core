// Copyright (c) 2023 Bitcoin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "logprintf.h"

#include <clang-tidy/ClangTidyModule.h>
#include <clang-tidy/ClangTidyModuleRegistry.h>

class KevacoinModule final : public clang::tidy::ClangTidyModule
{
public:
    void addCheckFactories(clang::tidy::ClangTidyCheckFactories& CheckFactories) override
    {
        CheckFactories.registerCheck<kevacoin::LogPrintfCheck>("kevacoin-unterminated-logprintf");
    }
};

static clang::tidy::ClangTidyModuleRegistry::Add<BitcoinModule>
    X("kevacoin-module", "Adds kevacoin checks.");

volatile int KevacoinModuleAnchorSource = 0;
