// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KEVACOIN_NODE_ABORT_H
#define KEVACOIN_NODE_ABORT_H

#include <atomic>

struct bilingual_str;

namespace util {
class SignalInterrupt;
} // namespace util

namespace node {
void AbortNode(util::SignalInterrupt* shutdown, std::atomic<int>& exit_status, const bilingual_str& message);
} // namespace node

#endif // KEVACOIN_NODE_ABORT_H
