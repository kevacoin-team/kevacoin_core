// Copyright (c) 2014-2017 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Copyright (c) 2018-2020 the Kevacoin Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <coins.h>
#include <init.h>
#include <keva/common.h>
#include <keva/main.h>
#include <key_io.h>
#include <node/context.h>
#include <script/keva.h>
#include <primitives/transaction.h>
#include <random.h>
#include <rpc/mining.h>
// #include <rpc/safemode.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <script/keva.h>
#include <txmempool.h>
#include <univalue.h>
// #include <util.h>
#include <validation.h>
// #include "utilstrencodings.h"
#include <boost/xpressive/xpressive_dynamic.hpp>

using node::NodeContext;

/**
 * Utility routine to construct a "keva info" object to return.  This is used
 * for keva_filter.
 * @param key The key.
 * @param value The key's value.
 * @param outp The last update's outpoint.
 * @param height The key's last update height.
 * @return A JSON object to return.
 */
UniValue getKevaInfo(const valtype& key, const valtype& value, const COutPoint& outp,
             int height, const valtype& nameSpace)
{
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("key", ValtypeToString(key));
    obj.pushKV("value", ValtypeToString(value));
    obj.pushKV("txid", outp.hash.GetHex());
    obj.pushKV("vout", static_cast<int>(outp.n));
    obj.pushKV("height", height);
    if (nameSpace.size() > 0) {
    obj.pushKV("namespace", EncodeBase58Check(nameSpace));
    }

    return obj;
}

/**
 * Return keva info object for a CKevaData object.
 * @param key The key.
 * @param data The key's data.
 * @return A JSON object to return.
 */
UniValue getKevaInfo(const valtype& key, const CKevaData& data, const valtype& nameSpace=valtype())
{
    return getKevaInfo(key, data.getValue(), data.getUpdateOutpoint(),
                        data.getHeight(), nameSpace);
}

static RPCHelpMan keva_get()
{
    return RPCHelpMan{"keva_get",
        "\nGet value of the given key.\n",
        {
            {"namespace", RPCArg::Type::STR, RPCArg::Optional::NO, "The namespace to get the value of the key"},
            {"key", RPCArg::Type::STR, RPCArg::Optional::NO, "Value for the key"},
        },
        {
            RPCResult{RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "value", "The value associated with the key."},
            }},
        },
        RPCExamples{
                HelpExampleCli("keva_get", "\"namespace_id\", \"key\"")
        + HelpExampleRpc("keva_get", "\"namespace_id\", \"key\"")
            },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    NodeContext& node = EnsureAnyNodeContext(request.context);
    ChainstateManager& chainman = EnsureChainman(node);
    CCoinsViewCache& view = chainman.ActiveChainstate().CoinsTip();
    CTxMemPool& mempool = EnsureMemPool(node);

    if (!request.params[0].isStr()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid namespace id");
    }
    if (!request.params[1].isStr()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid key");
    }
    // ObserveSafeMode();

    const std::string namespaceStr = request.params[0].get_str();
    valtype nameSpace;
    if (!DecodeKevaNamespace(namespaceStr, Params(), nameSpace)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid namespace id");
    }
    if (nameSpace.size() > MAX_NAMESPACE_LENGTH)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "the namespace is too long");

    const std::string keyStr = request.params[1].get_str();
    const valtype key = ValtypeFromString(keyStr);
    if (key.size() > MAX_KEY_LENGTH)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "the key is too long");

    // If there is unconfirmed one, return its value.
    {
        // LOCK (mempool.cs);
        valtype val;
        if (mempool.getUnconfirmedKeyValue(nameSpace, key, val)) {
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("key", keyStr);
            obj.pushKV("value", ValtypeToString(val));
            obj.pushKV("height", -1);
            return obj;
        }
    }

    // Otherwise, return the confirmed value.
    {
        LOCK(cs_main);
        CKevaData data;
        if (view.GetName(nameSpace, key, data)) {
            return getKevaInfo(key, data);
        }
    }

    // Empty value
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("key", keyStr);
    obj.pushKV("value", "");
    return obj;
},
    };
}

enum InitiatorType : int
{
    INITIATOR_TYPE_ALL,
    INITIATOR_TYPE_SELF,
    INITIATOR_TYPE_OTHER
};


// Check if the key has the format _g:NamespaceId. If yes,
// return the namespace.
bool isNamespaceGroup(const valtype& key, valtype& targetNS)
{
    std::string keyStr = ValtypeToString(key);
    if (keyStr.rfind(CKevaData::ASSOCIATE_PREFIX, 0) != 0) {
        return false;
    }
    keyStr.erase(0, CKevaData::ASSOCIATE_PREFIX.length());
    if (!DecodeKevaNamespace(keyStr, Params(), targetNS)) {
        return false;
    }
    return true;
}

void getNamespaceGroup(const valtype& nameSpace, std::set<valtype>& namespaces, const InitiatorType type, CCoinsViewCache& view, CTxMemPool& mempool)
{
    CKevaData data;

    // NodeContext& node = EnsureAnyNodeContext(request.context);
    // ChainstateManager& chainman = EnsureChainman(node);
    // CCoinsViewCache& view = chainman.ActiveChainstate().CoinsTip();
    // CTxMemPool& mempool = EnsureMemPool(node);
    // Find the namespace connection initialized by others.
    if (type == INITIATOR_TYPE_ALL || type == INITIATOR_TYPE_OTHER) {
        valtype ns;
        std::unique_ptr<CKevaIterator> iter(view.IterateAssociatedNamespaces(nameSpace));
        while (iter->next(ns, data)) {
            namespaces.insert(ns);
        }
    }

    if (type == INITIATOR_TYPE_OTHER) {
        return;
    }

    // Find the namespace connection initialized by us, and not confirmed yet.
    {
        // LOCK (mempool.cs);
        std::vector<std::tuple<valtype, valtype, valtype, uint256>> unconfirmedKeyValueList;
        mempool.getUnconfirmedKeyValueList(unconfirmedKeyValueList, nameSpace);
        std::set<valtype> nsList;
        valtype targetNS;
        for (auto entry: unconfirmedKeyValueList) {
            const valtype ns = std::get<0>(entry);
            if (ns != nameSpace) {
                continue;
            }
            const valtype key = std::get<1>(entry);
            // Find the value with the format _g:NamespaceId
            if (!isNamespaceGroup(key, targetNS)) {
                continue;
            }
            if (nsList.find(targetNS) != nsList.end()) {
                continue;
            }
            valtype val;
            if (mempool.getUnconfirmedKeyValue(nameSpace, key, val) && val.size() > 0) {
                namespaces.insert(targetNS);
                nsList.insert(targetNS);
            }
        }
    }

    // Find the namespace connection initialized by us.
    std::unique_ptr<CKevaIterator> iterKeys(view.IterateKeys(nameSpace));
    valtype key;
    while (iterKeys->next(key, data)) {
        valtype targetNS;
        // Find the value with the format _g:NamespaceId
        if (isNamespaceGroup(key, targetNS)) {
            // If it has been removed but not yet confirmed, skip it anyway.
            {
            // LOCK (mempool.cs);
            valtype val;
            if (mempool.getUnconfirmedKeyValue(nameSpace, key, val) && val.size() == 0) {
                continue;
            }
            }
            namespaces.insert(targetNS);
        }
    }

}

static RPCHelpMan keva_group_get()
{
    return RPCHelpMan{"keva_group_get",
        "\nGet value of the given key from the namespace and other namespaces in the same group.\n",
        {
            {"namespace", RPCArg::Type::STR, RPCArg::Optional::NO, "The namespace to get the value of the key"},
            {"key", RPCArg::Type::STR, RPCArg::Optional::NO, "Value for the key"},
            {"initiator", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Options are \"all\", \"self\" and \"other\", default is \"all\". \"all\": all the namespaces, whose participation in the group is initiated by this namespace or other namespaces. \"self\": only the namespace whose participation is initiated by this namespace. \"other\": only the namespace whose participation is initiated by other namespaces."},
        },
        {
            RPCResult{RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "value", "The value associated with the key."},
            }},
        },
        RPCExamples{
                HelpExampleCli("keva_group_get", "\"namespace_id\", \"key\"")
                + HelpExampleRpc("keva_group_get", "\"namespace_id\", \"key\"")
            },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    NodeContext& node = EnsureAnyNodeContext(request.context);
    ChainstateManager& chainman = EnsureChainman(node);
    CCoinsViewCache& view = chainman.ActiveChainstate().CoinsTip();
    CTxMemPool& mempool = EnsureMemPool(node);

    if (!request.params[0].isStr()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid namespace id");
    }
    if (!request.params[1].isStr()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid key");
    }
    InitiatorType initiatorType = INITIATOR_TYPE_ALL;
    if (request.params.size() == 3) {
        if (!request.params[2].isStr()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid initiator");
        }
        const std::string initiator = request.params[2].get_str();
        if (initiator == "all") {
            initiatorType = INITIATOR_TYPE_ALL;
        } else if (initiator == "self") {
            initiatorType = INITIATOR_TYPE_SELF;
        } else if (initiator == "other") {
            initiatorType = INITIATOR_TYPE_SELF;
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid initiator type");
        }
    }

    // ObserveSafeMode();

    const std::string namespaceStr = request.params[0].get_str();
    valtype nameSpace;
    if (!DecodeKevaNamespace(namespaceStr, Params(), nameSpace)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid namespace id");
    }
    if (nameSpace.size() > MAX_NAMESPACE_LENGTH)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "the namespace is too long");

    const std::string keyStr = request.params[1].get_str();
    const valtype key = ValtypeFromString(keyStr);
    if (key.size() > MAX_KEY_LENGTH)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "the key is too long");

    std::set<valtype> namespaces;
    {
        LOCK(cs_main);
        namespaces.insert(nameSpace);
        getNamespaceGroup(nameSpace, namespaces, initiatorType, view, mempool);
    }

    // If there is unconfirmed one, return its value.
    {
        // LOCK(mempool);
        valtype val;
        for (auto iter = namespaces.begin(); iter != namespaces.end(); ++iter) {
            if (mempool.getUnconfirmedKeyValue(*iter, key, val)) {
            // Return the first unconfirmed one.
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("key", keyStr);
            obj.pushKV("value", ValtypeToString(val));
            obj.pushKV("height", -1);
            obj.pushKV("namespace", EncodeBase58Check(*iter));
            return obj;
            }
        }
    }

  // Otherwise, return the confirmed value.
  {
    LOCK(cs_main);
    unsigned currentHeight = 0;
    CKevaData data;
    CKevaData currentData;
    valtype ns;
    for (auto iter = namespaces.begin(); iter != namespaces.end(); ++iter) {
      if (view.GetName(*iter, key, currentData)) {
        if (currentData.getHeight() > currentHeight) {
          currentHeight = currentData.getHeight();
          data = currentData;
          ns = *iter;
        }
      }
    }
    if (currentHeight > 0) {
      return getKevaInfo(key, data, ns);
    }
  }

  // Empty value
  UniValue obj(UniValue::VOBJ);
  obj.pushKV("key", keyStr);
  obj.pushKV("value", "");
  return obj;
},
    };
}

static RPCHelpMan keva_group_filter()
{
    return RPCHelpMan{"keva_group_filter",
        "\nScan and list keys matching a regular expression.\n",
        {
            {"namespace", RPCArg::Type::STR, RPCArg::Optional::NO, "The namespace Id"},
            {"initiator", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Options are \"all\", \"self\" and \"other\", default is \"all\". \"all\": all the namespaces, whose participation in the group is initiated by this namespace or other namespaces. \"self\": only the namespace whose participation is initiated by this namespace. \"other\": only the namespace whose participation is initiated by other namespaces."},
            {"regexp", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "filter keys with this regexp"},
            {"maxage", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Only consider names updated in the last \"maxage\" blocks; 0 means all names, default=96000"},
            {"from", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Return from this position onward; index starts at 0, default=0"},
            {"nb", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Return only \"nb\" entries; 0 means all, default=0"},
            {"stat", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "If set to the string \"stat\", print statistics instead of returning the names"},
        },
        {
            RPCResult{RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "key", "The requested key."},
                {RPCResult::Type::STR, "value", "The key's current value."},
                {RPCResult::Type::STR, "txid", "The key's last update tx."},
                {RPCResult::Type::NUM, "height", "The key's last update height."},
            }},
        },
        RPCExamples{
                HelpExampleCli("keva_group_filter", "\"namespaceId\" \"all\"")
            + HelpExampleCli("keva_group_filter", "\"namespaceId\" \"self\" 96000 0 0 \"stat\"")
            + HelpExampleRpc("keva_group_filter", "\"namespaceId\"")
            },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    NodeContext& node = EnsureAnyNodeContext(request.context);
    ChainstateManager& chainman = EnsureChainman(node);
    CCoinsViewCache& view = chainman.ActiveChainstate().CoinsTip();
    CTxMemPool& mempool = EnsureMemPool(node);

    if (chainman.IsInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                        "Kevacoin is downloading blocks...");
    }

    // ObserveSafeMode();

    bool haveRegexp(false);
    boost::xpressive::sregex regexp;

    valtype nameSpace;
    int maxage(96000), from(0), nb(0);
    bool stats(false);
    InitiatorType initiatorType = INITIATOR_TYPE_ALL;

    if (request.params.size() >= 1) {
        if (!request.params[0].isStr()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid namespace id");
        }
        const std::string namespaceStr = request.params[0].get_str();
        if (!DecodeKevaNamespace(namespaceStr, Params(), nameSpace)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid namespace id");
        }
    }

    if (request.params.size() >= 2) {
        if (!request.params[1].isStr()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid initiator");
        }
        const std::string initiator = request.params[1].get_str();
        if (initiator == "all") {
            initiatorType = INITIATOR_TYPE_ALL;
        } else if (initiator == "self") {
            initiatorType = INITIATOR_TYPE_SELF;
        } else if (initiator == "other") {
            initiatorType = INITIATOR_TYPE_OTHER;
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid initiator");
        }
    }

    if (request.params.size() >= 3) {
        if (!request.params[2].isStr()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid regex");
        }
        haveRegexp = true;
        regexp = boost::xpressive::sregex::compile(request.params[2].get_str());
    }

    if (request.params.size() >= 4) {
        if (!request.params[3].isNum()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid maxage");
        }
        maxage = request.params[3].getInt<int>();
    }
    if (maxage < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                        "'maxage' should be non-negative");

    if (request.params.size() >= 5) {
        if (!request.params[4].isNum()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid from");
        }
        from = request.params[4].getInt<int>();
    }
    if (from < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "'from' should be non-negative");

    if (request.params.size() >= 6) {
        if (!request.params[5].isNum()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid nb");
        }
        nb = request.params[5].getInt<int>();
    }
    if (nb < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "'nb' should be non-negative");

    if (request.params.size() >= 7) {
        if (!request.params[6].isStr()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid stat");
        }
        if (request.params[6].get_str() != "stat")
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                            "fifth argument must be the literal string 'stat'");
        stats = true;
    }

    UniValue uniKeys(UniValue::VARR);
    unsigned count(0);
    std::map<valtype, std::tuple<CKevaData, valtype>> keys;

    LOCK (cs_main);
    std::set<valtype> namespaces;
    namespaces.insert(nameSpace);
    getNamespaceGroup(nameSpace, namespaces, initiatorType, view, mempool);

    valtype key;
    CKevaData data;
    valtype displayKey = ValtypeFromString(CKevaScript::KEVA_DISPLAY_NAME_KEY);
    for (auto iterNS = namespaces.begin(); iterNS != namespaces.end(); ++iterNS) {
        std::unique_ptr<CKevaIterator> iter(view.IterateKeys(*iterNS));
        while (iter->next(key, data)) {
            if (key == displayKey) {
                continue;
            }
            const int age = chainman.ActiveHeight() - data.getHeight();
            assert(age >= 0);
            if (maxage != 0 && age >= maxage) {
                continue;
            }

            if (haveRegexp) {
                const std::string keyStr = ValtypeToString(key);
                boost::xpressive::smatch matches;
                if (!boost::xpressive::regex_search(keyStr, matches, regexp))
                    continue;
                }

                if (from > 0) {
                    --from;
                    continue;
                }
                assert(from == 0);

            if (stats) {
                ++count;
            } else {
                auto it = keys.find(key);
                if (it == keys.end()) {
                    keys.insert(std::make_pair(key, std::make_tuple(data, *iterNS)));
                } else if (data.getHeight() > std::get<0>(it->second).getHeight()) {
                    it->second = std::make_tuple(data, *iterNS);
                }
            }

            if (nb > 0) {
                --nb;
                if (nb == 0)
                    break;
            }
        }
    }

    if (stats) {
        UniValue res(UniValue::VOBJ);
        res.pushKV("blocks", chainman.ActiveHeight());
        res.pushKV("count", static_cast<int>(count));
        return res;
    }

    for (auto e = keys.begin(); e != keys.end(); ++e) {
        uniKeys.push_back(getKevaInfo(e->first, std::get<0>(e->second), std::get<1>(e->second)));
    }

    return uniKeys;
},
    };
}


static RPCHelpMan keva_filter()
{
    return RPCHelpMan{"keva_filter",
        "\nScan and list keys matching a regular expression.\n",
        {
            {"namespace", RPCArg::Type::STR, RPCArg::Optional::NO, "The namespace Id"},
            {"regexp", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "filter keys with this regexp"},
            {"maxage", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Only consider names updated in the last \"maxage\" blocks; 0 means all names, default=96000"},
            {"from", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Return from this position onward; index starts at 0, default=0"},
            {"nb", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Return only \"nb\" entries; 0 means all, default=0"},
            {"stat", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "If set to the string \"stat\", print statistics instead of returning the names"},
        },
        {
            RPCResult{RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "key", "The requested key."},
                {RPCResult::Type::STR, "value", "The key's current value."},
                {RPCResult::Type::STR, "txid", "The key's last update tx."},
                {RPCResult::Type::NUM, "height", "The key's last update height."},
            }},
        },
        RPCExamples{
                HelpExampleCli("keva_filter", "\"^id/\"")
            + HelpExampleCli("keva_filter", "\"^id/\" 96000 0 0 \"stat\"")
            + HelpExampleRpc("keva_filter", "\"^d/\"")
            },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    NodeContext& node = EnsureAnyNodeContext(request.context);
    ChainstateManager& chainman = EnsureChainman(node);
    // std::unique_ptr<CCoinsViewCache> pcoinsTip;
    // CCoinsViewCache view(pcoinsTip->)
    LOCK (cs_main);
    CCoinsViewCache& view = chainman.ActiveChainstate().CoinsTip();

    if (chainman.IsInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                        "Kevacoin is downloading blocks...");
    }

    // ObserveSafeMode();

    /* ********************** */
    /* Interpret parameters.  */

    bool haveRegexp(false);
    boost::xpressive::sregex regexp;

    valtype nameSpace;
    int maxage(96000), from(0), nb(0);
    bool stats(false);

    if (request.params.size() >= 1) {
        if (!request.params[0].isStr()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid namespace id");
        }
        const std::string namespaceStr = request.params[0].get_str();
        if (!DecodeKevaNamespace(namespaceStr, Params(), nameSpace)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid namespace id");
        }
    }

    if (request.params.size() >= 2) {
        if (!request.params[1].isStr()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid regex");
        }
        haveRegexp = true;
        regexp = boost::xpressive::sregex::compile(request.params[1].get_str());
    }

    if (request.params.size() >= 3) {
        if (!request.params[2].isNum()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid maxage");
        }
        maxage = request.params[2].getInt<int>();
    }
    if (maxage < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                        "'maxage' should be non-negative");

    if (request.params.size() >= 4) {
        if (!request.params[3].isNum()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid from");
        }
        from = request.params[3].getInt<int>();
    }
    if (from < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "'from' should be non-negative");

    if (request.params.size() >= 5) {
        if (!request.params[4].isNum()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid nb");
        }
        nb = request.params[4].getInt<int>();
    }
    if (nb < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "'nb' should be non-negative");

    if (request.params.size() >= 6) {
        if (!request.params[5].isStr()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid stat");
        }
        if (request.params[5].get_str() != "stat")
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                            "fifth argument must be the literal string 'stat'");
        stats = true;
    }

    /* ******************************************* */
    /* Iterate over names to build up the result.  */

    UniValue keys(UniValue::VARR);
    unsigned count(0);

    LOCK (cs_main);

    valtype key;
    CKevaData data;
    std::unique_ptr<CKevaIterator> iter(view.IterateKeys(nameSpace));
    while (iter->next(key, data)) {
        const int age = chainman.ActiveHeight() - data.getHeight();
        assert(age >= 0);
        if (maxage != 0 && age >= maxage)
            continue;

        if (haveRegexp) {
            const std::string keyStr = ValtypeToString(key);
            boost::xpressive::smatch matches;
            if (!boost::xpressive::regex_search(keyStr, matches, regexp))
                continue;
        }

        if (from > 0) {
            --from;
            continue;
        }
        assert(from == 0);

        if (stats)
            ++count;
        else
            keys.push_back(getKevaInfo(key, data));

        if (nb > 0) {
            --nb;
            if (nb == 0)
                break;
        }
    }

    /* ********************************************************** */
    /* Return the correct result (take stats mode into account).  */

    if (stats) {
        UniValue res(UniValue::VOBJ);
        res.pushKV("blocks", chainman.ActiveHeight());
        res.pushKV("count", static_cast<int>(count));
        return res;
    }

    return keys;
},
    };
}

/**
 * Utility routine to construct a "namespace info" object to return.  This is used
 * for keva_group.
 * @param namespaceId The namespace Id.
 * @param name The display name of the namespace.
 * @param outp The last update's outpoint.
 * @param height The height at which the namespace joins the group.
 * @param initiator If true, the namespace connection is initiated by this namespace.
 * @return A JSON object to return.
 */
UniValue getNamespaceInfo(const valtype& namespaceId, const valtype& name, const COutPoint& outp,
             int height, bool initiator)
{
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("namespaceId", EncodeBase58Check(namespaceId));
    obj.pushKV("display_name", ValtypeToString(name));
    obj.pushKV("txid", outp.hash.GetHex());
    obj.pushKV("height", height);
    obj.pushKV("initiator", initiator);

    return obj;
}

static RPCHelpMan keva_group_show()
{
    return RPCHelpMan{"keva_group_show",
        "\nList namespaces that are in the same group as the given namespace.\n",
        {
            {"namespace", RPCArg::Type::STR, RPCArg::Optional::NO, "The namespace Id"},
            {"maxage", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Only consider names updated in the last \"maxage\" blocks; 0 means all names, default=96000"},
            {"from", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Return from this position onward; index starts at 0, default=0"},
            {"nb", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Return only \"nb\" entries; 0 means all, default=0"},
            {"stat", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "If set to the string \"stat\", print statistics instead of returning the names"},
        },
        {
            RPCResult{RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "key", "The requested key."},
                {RPCResult::Type::STR, "value", "The key's current value."},
                {RPCResult::Type::STR, "txid", "The key's last update tx."},
                {RPCResult::Type::NUM, "height", "The key's last update height."},
            }},
        },
        RPCExamples{
                HelpExampleCli("keva_group_show", "NamespaceId")
            + HelpExampleCli("keva_group_show", "NamespaceId 96000 0 0 \"stat\"")
        + HelpExampleRpc("keva_group_show", "\"namespace_id\"")
            },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    NodeContext& node = EnsureAnyNodeContext(request.context);
    ChainstateManager& chainman = EnsureChainman(node);
    CCoinsViewCache& view = chainman.ActiveChainstate().CoinsTip();
    CTxMemPool& mempool = EnsureMemPool(node);

    if (chainman.IsInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                        "Kevacoin is downloading blocks...");
    }

    // ObserveSafeMode();

    // Interpret parameters.
    valtype nameSpace;
    int maxage(96000), from(0), nb(0);
    bool stats(false);

    if (request.params.size() >= 1) {
        if (!request.params[0].isStr()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid namespace id");
        }
        const std::string namespaceStr = request.params[0].get_str();
        if (!DecodeKevaNamespace(namespaceStr, Params(), nameSpace)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid namespace id");
        }
    }

    if (request.params.size() >= 2)
        if (!request.params[1].isNum()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid maxage");
        }
        maxage = request.params[1].getInt<int>();
    if (maxage < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                        "'maxage' should be non-negative");

    if (request.params.size() >= 3)
        if (!request.params[2].isNum()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid from");
        }
        from = request.params[2].getInt<int>();

    if (from < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "'from' should be non-negative");

    if (request.params.size() >= 4)
        if (!request.params[3].isNum()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid nb");
        }
        nb = request.params[3].getInt<int>();

    if (nb < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "'nb' should be non-negative");

    if (request.params.size() >= 5) {
        if (!request.params[4].isStr()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid stat");
        }
        if (request.params[4].get_str() != "stat")
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                            "fifth argument must be the literal string 'stat'");
        stats = true;
    }

    // Iterate over names to build up the result.
    UniValue namespaces(UniValue::VARR);
    unsigned count(0);

    LOCK (cs_main);

    valtype ns;
    CKevaData data;
    valtype nsDisplayKey = ValtypeFromString(CKevaScript::KEVA_DISPLAY_NAME_KEY);
    std::unique_ptr<CKevaIterator> iter(view.IterateAssociatedNamespaces(nameSpace));

    // Find the namespace connection initialized by others.
    while (iter->next(ns, data)) {
        const int age = chainman.ActiveHeight() - data.getHeight();
        assert(age >= 0);
        if (maxage != 0 && age >= maxage) {
            continue;
        }

        if (from > 0) {
            --from;
            continue;
        }
        assert(from == 0);

        if (stats) {
            ++count;
        } else {
            CKevaData nsData;
            valtype nsName;
            if (view.GetName(ns, nsDisplayKey, nsData)) {
                nsName = nsData.getValue();
                }
                namespaces.push_back(getNamespaceInfo(ns, nsName, data.getUpdateOutpoint(),
                                data.getHeight(), true));
        }

        if (nb > 0) {
            --nb;
            if (nb == 0)
                break;
        }
    }

    // Find the namespace connection initialized by us, and not confirmed yet.
    {
        // LOCK (mempool.cs);
        std::vector<std::tuple<valtype, valtype, valtype, uint256>> unconfirmedKeyValueList;
        mempool.getUnconfirmedKeyValueList(unconfirmedKeyValueList, nameSpace);
        std::set<valtype> nsList;
        valtype targetNS;
        for (auto entry: unconfirmedKeyValueList) {
            const valtype ns = std::get<0>(entry);
            if (ns != nameSpace) {
                continue;
            }
            const valtype key = std::get<1>(entry);
            // Find the value with the format _g:NamespaceId
            if (!isNamespaceGroup(key, targetNS)) {
                continue;
            }
            if (nsList.find(targetNS) != nsList.end()) {
                continue;
            }
            valtype val;
            if (mempool.getUnconfirmedKeyValue(nameSpace, key, val) && val.size() > 0) {
                CKevaData nsData;
                valtype nsName;
                if (view.GetName(targetNS, nsDisplayKey, nsData)) {
                    nsName = nsData.getValue();
                }
                UniValue obj(UniValue::VOBJ);
                obj.pushKV("namespaceId", EncodeBase58Check(targetNS));
                obj.pushKV("display_name", ValtypeToString(nsName));
                obj.pushKV("height", -1);
                obj.pushKV("initiator", false);
                namespaces.push_back(obj);
                nsList.insert(targetNS);
            }
        }
    }

    // Find the namespace connection initialized by us and confirmed.
    std::unique_ptr<CKevaIterator> iterKeys(view.IterateKeys(nameSpace));
    valtype targetNS;
    valtype key;
    while (iterKeys->next(key, data)) {

        // Find the value with the format _g:NamespaceId
        if (!isNamespaceGroup(key, targetNS)) {
            continue;
        }

        // If it has been removed but not yet confirmed, skip it anyway.
        {
            // LOCK (mempool.cs);
            valtype val;
            if (mempool.getUnconfirmedKeyValue(nameSpace, key, val) && val.size() == 0) {
                continue;
            }
        }

        const int age = chainman.ActiveHeight() - data.getHeight();
        assert(age >= 0);
        if (maxage != 0 && age >= maxage) {
            continue;
        }

        if (from > 0) {
            --from;
            continue;
        }
        assert(from == 0);

        if (stats) {
            ++count;
        }
        else {
            CKevaData nsData;
            valtype nsName;
            if (view.GetName(targetNS, nsDisplayKey, nsData)) {
                nsName = nsData.getValue();
            }
            namespaces.push_back(getNamespaceInfo(targetNS, nsName, data.getUpdateOutpoint(),
                            data.getHeight(), false));
        }

        if (nb > 0) {
            --nb;
            if (nb == 0)
            break;
        }
    }

    if (stats) {
        UniValue res(UniValue::VOBJ);
        res.pushKV("blocks", chainman.ActiveHeight());
        res.pushKV("count", static_cast<int>(count));
        return res;
    }

    return namespaces;
},
    };
}

void RegisterKevaRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[]{
        {"keva_get", &keva_get},
        {"keva_filter", &keva_filter},
        {"keva_group_show", &keva_group_show},
        {"keva_group_get", &keva_group_get},
        {"keva_group_filter", &keva_group_filter},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
