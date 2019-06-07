#include "scheme_state.h"
#include "scheme_expectState.h"
#include "scheme_postState.h"
#include <mutex>
#include <retesteth/TestOutputHelper.h>
using namespace  std;

namespace test {
CompareResult compareStates(scheme_state const& _stateExpect, scheme_state const& _statePost)
{
    return compareStates(scheme_expectState(_stateExpect.getData()), _statePost);
}

CompareResult compareStates(scheme_expectState const& _stateExpect, scheme_state const& _statePost)
{
    CompareResult result = CompareResult::Success;
    auto checkMessage = [&result](bool _flag, CompareResult _type, string const& _error) -> void {
        ETH_MARK_ERROR(_flag, _error);
        if (!_flag)
			result = _type;
    };

    for (auto const& a: _stateExpect.getAccounts())
    {
        if (a.shouldNotExist())
		{
            checkMessage(!_statePost.hasAccount(a.address()),
                CompareResult::AccountShouldNotExist,
                TestOutputHelper::get().testName() +  "' Compare States: " + a.address()
                + "' address not expected to exist!");
			if (result != CompareResult::Success)
				break;
			continue;
		}
        else
            checkMessage(_statePost.hasAccount(a.address()),
                CompareResult::MissingExpectedAccount,
                TestOutputHelper::get().testName() +  " Compare States: Missing expected address: '" + a.address() + "'");

		if (result != CompareResult::Success)
            break;

		// Check account in state post with expect section account
		scheme_account const& inState = _statePost.getAccount(a.address());

        if (a.hasBalance())
		{
            u256 inStateB = u256(inState.getData().atKey("balance").asString());
            checkMessage(a.getData().atKey("balance").asString() ==
                             inState.getData().atKey("balance").asString(),
                CompareResult::IncorrectBalance,
                TestOutputHelper::get().testName() + " Check State: '" + a.address() +
                    "': incorrect balance " + toString(inStateB) + ", expected " +
                    toString(u256(a.getData().atKey("balance").asString())) + " (" +
                    a.getData().atKey("balance").asString() +
                    " != " + inState.getData().atKey("balance").asString() + ")");
        }

        if (a.hasNonce())
            checkMessage(a.getData().atKey("nonce").asString() ==
                             inState.getData().atKey("nonce").asString(),
                CompareResult::IncorrectNonce,
                TestOutputHelper::get().testName() + " Check State: '" + a.address() +
                    "': incorrect nonce " + inState.getData().atKey("nonce").asString() +
                    ", expected " + a.getData().atKey("nonce").asString());

        // Check that state post has values from expected storage
        if (a.hasStorage()) {
            CompareResult res = a.compareStorage(inState.getData().atKey("storage"));
            if (result == CompareResult::Success)
                result = res;  // Only override success result with potential error
        }

        if (a.hasCode())
            checkMessage(
                a.getData().atKey("code").asString() == inState.getData().atKey("code").asString(),
                CompareResult::IncorrectCode,
                TestOutputHelper::get().testName() + " Check State: '" + a.address() +
                    "': incorrect code '" + inState.getData().atKey("code").asString() +
                    "', expected '" + a.getData().atKey("code").asString() + "'");
    }
    return result;
}

mutex g_staticDeclaration;
DataObject scheme_state::getDataForRPC(std::string const& _network) const
{
    // Cpp specific precompiled declaraion for genesis
    static DataObject ecrecoverObj;
    static DataObject sha256Obj;
    static DataObject ripemd160Obj;
    static DataObject identityObj;
    static DataObject alt_bn128_G1_addObj;
    static DataObject alt_bn128_G1_mulObj;

    {
        std::lock_guard<std::mutex> lock(g_staticDeclaration);
        if (!ecrecoverObj.count("name"))
        {
            ecrecoverObj["name"] = "ecrecover";
            ecrecoverObj["linear"]["base"] = 3000;
            ecrecoverObj["linear"]["word"] = 0;

            sha256Obj["name"] = "sha256";
            sha256Obj["linear"]["base"] = 60;
            sha256Obj["linear"]["word"] = 12;

            ripemd160Obj["name"] = "sha256";
            ripemd160Obj["linear"]["base"] = 600;
            ripemd160Obj["linear"]["word"] = 120;

            identityObj["name"] = "identity";
            identityObj["linear"]["base"] = 15;
            identityObj["linear"]["word"] = 3;

            alt_bn128_G1_addObj["name"] = "alt_bn128_G1_add";
            alt_bn128_G1_addObj["linear"]["base"] = 500;
            alt_bn128_G1_addObj["linear"]["word"] = 0;

            alt_bn128_G1_mulObj["name"] = "alt_bn128_G1_mul";
            alt_bn128_G1_mulObj["linear"]["base"] = 40000;
            alt_bn128_G1_mulObj["linear"]["word"] = 0;
        }
    }

    bool networkChecked = false;
    if (_network == "Frontier" || _network == "Homestead" || _network == "EIP150" || _network == "EIP158")
        networkChecked = true;

    DataObject ret = m_data;
    ret["0x0000000000000000000000000000000000000001"]["precompiled"] = ecrecoverObj;
    ret["0x0000000000000000000000000000000000000002"]["precompiled"] = sha256Obj;
    ret["0x0000000000000000000000000000000000000003"]["precompiled"] = ripemd160Obj;
    ret["0x0000000000000000000000000000000000000004"]["precompiled"] = identityObj;

    if (_network == "Byzantium" || _network == "Constantinople" || _network == "ConstantinopleFix")
    {
        networkChecked = true;
        ret["0x0000000000000000000000000000000000000005"]["precompiled"]["name"] = "modexp";
        ret["0x0000000000000000000000000000000000000006"]["precompiled"] = alt_bn128_G1_addObj;
        ret["0x0000000000000000000000000000000000000007"]["precompiled"] = alt_bn128_G1_mulObj;
        ret["0x0000000000000000000000000000000000000008"]["precompiled"]["name"] =
            "alt_bn128_pairing_product";
    }

    if (!networkChecked)
        ETH_FAIL_MESSAGE("Unhandled network: " + _network + " (scheme_state::getDataForRPC)");
    return ret;
}
} // namespace test
