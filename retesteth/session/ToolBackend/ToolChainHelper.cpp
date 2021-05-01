#include "ToolChainHelper.h"
#include "ToolChainManager.h"
#include <retesteth/EthChecks.h>
#include <retesteth/Options.h>
#include <retesteth/testStructures/Common.h>
using namespace dev;
using namespace test;
using namespace teststruct;
using namespace dataobject;

namespace toolimpl
{
ToolParams::ToolParams(DataObject const& _data)
{
    const u256 unreachable = 10000000000;
    if (_data.count("homesteadForkBlock"))
        m_homesteadForkBlock = spVALUE(new VALUE(_data.atKey("homesteadForkBlock")));
    else
        m_homesteadForkBlock = spVALUE(new VALUE(unreachable));

    if (_data.count("byzantiumForkBlock"))
        m_byzantiumForkBlock = spVALUE(new VALUE(_data.atKey("byzantiumForkBlock")));
    else
        m_byzantiumForkBlock = spVALUE(new VALUE(unreachable));

    if (_data.count("constantinopleForkBlock"))
        m_constantinopleForkBlock = spVALUE(new VALUE(_data.atKey("constantinopleForkBlock")));
    else
        m_constantinopleForkBlock = spVALUE(new VALUE(unreachable));

    if (_data.count("muirGlacierForkBlock"))
        m_muirGlacierForkBlock = spVALUE(new VALUE(_data.atKey("muirGlacierForkBlock")));
    else
        m_muirGlacierForkBlock = spVALUE(new VALUE(unreachable));

    requireJsonFields(_data, "ToolParams " + _data.getKey(),
        {{"fork", {{DataType::String}, jsonField::Required}},
            {"muirGlacierForkBlock", {{DataType::String}, jsonField::Optional}},
            {"constantinopleForkBlock", {{DataType::String}, jsonField::Optional}},
            {"byzantiumForkBlock", {{DataType::String}, jsonField::Optional}},
            {"homesteadForkBlock", {{DataType::String}, jsonField::Optional}}});
}

// We simulate the client backend side here, so thats why number5 is hardcoded
// Map rewards from non standard forks into standard
static std::map<FORK, FORK> RewardMapForToolBefore5 = {{"FrontierToHomesteadAt5", "Frontier"},
    {"HomesteadToEIP150At5", "Homestead"}, {"EIP158ToByzantiumAt5", "EIP158"}, {"HomesteadToDaoAt5", "Homestead"},
    {"ByzantiumToConstantinopleFixAt5", "Byzantium"}};
static std::map<FORK, FORK> RewardMapForToolAfter5 = {{"FrontierToHomesteadAt5", "Homestead"},
    {"HomesteadToEIP150At5", "EIP150"}, {"EIP158ToByzantiumAt5", "Byzantium"}, {"HomesteadToDaoAt5", "Homestead"},
    {"ByzantiumToConstantinopleFixAt5", "ConstantinopleFix"}};

std::tuple<VALUE, FORK> prepareReward(SealEngine _engine, FORK const& _fork, VALUE const& _blockNumber)
{
    if (_engine == SealEngine::Ethash)
        ETH_WARNING_TEST("t8ntool backend treat Ethash as NoProof!", 6);

    // Setup mining rewards
    std::map<FORK, spVALUE> const& rewards = Options::get().getDynamicOptions().getCurrentConfig().getRewardMap();
    if (rewards.count(_fork))
        return {rewards.at(_fork).getCContent(), _fork};
    else
    {
        if (_blockNumber < 5)
        {
            if (!RewardMapForToolBefore5.count(_fork))
                ETH_ERROR_MESSAGE("ToolBackend error getting reward for fork: " + _fork.asString());
            auto const& trFork = RewardMapForToolBefore5.at(_fork);
            assert(rewards.count(trFork));
            return {rewards.at(trFork).getCContent(), trFork};
        }
        else
        {
            if (!RewardMapForToolAfter5.count(_fork))
                ETH_ERROR_MESSAGE("ToolBackend error getting reward for fork: " + _fork.asString());
            auto const& trFork = RewardMapForToolAfter5.at(_fork);
            assert(rewards.count(trFork));
            return {rewards.at(trFork).getCContent(), _fork == "HomesteadToDaoAt5" ? "HomesteadToDaoAt5" : trFork};
        }
    }
}

VALUE calculateGasLimit(VALUE const& _parentGasLimit, VALUE const& _parentGasUsed)
{
    static u256 gasFloorTarget = 3141562;  //_gasFloorTarget == Invalid256 ? 3141562 : _gasFloorTarget;
    u256 gasLimit = _parentGasLimit.asU256();
    static u256 boundDivisor = u256("0x0400");
    if (gasLimit < gasFloorTarget)
        return min<u256>(gasFloorTarget, gasLimit + gasLimit / boundDivisor - 1);
    else
        return max<u256>(
            gasFloorTarget, gasLimit - gasLimit / boundDivisor + 1 + (_parentGasUsed.asU256() * 6 / 5) / boundDivisor);
}

// Because tool report incomplete state. restore missing fields with zeros
// Also remove leading zeros in storage
State restoreFullState(DataObject const& _toolState)
{
    DataObject fullState;
    for (auto const& accTool : _toolState.getSubObjects())
    {
        DataObject acc;
        acc["balance"] = accTool.count("balance") ? accTool.atKey("balance").asString() : "0x00";
        acc["nonce"] = accTool.count("nonce") ? accTool.atKey("nonce").asString() : "0x00";
        acc["code"] = accTool.count("code") ? accTool.atKey("code").asString() : "0x";
        acc["storage"] = accTool.count("storage") ? accTool.atKey("storage") : DataObject(DataType::Object);
        for (auto& storageRecord : acc.atKeyUnsafe("storage").getSubObjectsUnsafe())
        {
            storageRecord.performModifier(mod_removeLeadingZerosFromHexValuesEVEN);
            storageRecord.performModifier(mod_removeLeadingZerosFromHexKeysEVEN);
        }
        fullState[accTool.getKey()] = acc;
    }
    return State(fullState);
}

ChainOperationParams ChainOperationParams::defaultParams(ToolParams const& _params)
{
    ChainOperationParams aleth;
    aleth.durationLimit = u256("0x0d");
    aleth.minimumDifficulty = u256("0x20000");
    aleth.difficultyBoundDivisor = u256("0x0800");
    aleth.homesteadForkBlock = _params.homesteadForkBlock().asU256();
    aleth.byzantiumForkBlock = _params.byzantiumForkBlock().asU256();
    aleth.constantinopleForkBlock = _params.constantinopleForkBlock().asU256();
    aleth.muirGlacierForkBlock = _params.muirGlacierForkBlock().asU256();
    return aleth;
}

// Aleth calculate difficulty formula
u256 calculateEthashDifficulty(ChainOperationParams const& _chainParams, spBlockHeader const& _bi, spBlockHeader const& _parent)
{
    const unsigned c_expDiffPeriod = 100000;

    if (_bi.getCContent().number() == 0)
        throw test::UpwardsException("calculateEthashDifficulty was called for block with number == 0");

    auto const& minimumDifficulty = _chainParams.minimumDifficulty;
    auto const& difficultyBoundDivisor = _chainParams.difficultyBoundDivisor;
    auto const& durationLimit = _chainParams.durationLimit;

    bigint target;  // stick to a bigint for the target. Don't want to risk going negative.
    auto const& bi = _bi.getCContent();
    auto const& parent = _parent.getCContent();
    if (bi.number() < _chainParams.homesteadForkBlock)
        // Frontier-era difficulty adjustment
        target = bi.timestamp().asU256() >= parent.timestamp().asU256() + durationLimit ?
                     parent.difficulty().asU256() - (parent.difficulty().asU256() / difficultyBoundDivisor) :
                     (parent.difficulty().asU256() + (parent.difficulty().asU256() / difficultyBoundDivisor));
    else
    {
        bigint const timestampDiff = bigint(bi.timestamp().asU256()) - parent.timestamp().asU256();
        bigint const adjFactor = bi.number() < _chainParams.byzantiumForkBlock ?
                                     max<bigint>(1 - timestampDiff / 10, -99) :  // Homestead-era difficulty adjustment
                                     max<bigint>((parent.hasUncles() ? 2 : 1) - timestampDiff / 9,
                                         -99);  // Byzantium-era difficulty adjustment

        target = parent.difficulty().asU256() + parent.difficulty().asU256() / 2048 * adjFactor;
    }

    bigint o = target;
    unsigned exponentialIceAgeBlockNumber = unsigned(parent.number().asU256() + 1);

    // EIP-2384 Istanbul/Berlin Difficulty Bomb Delay
    if (bi.number().asU256() >= _chainParams.muirGlacierForkBlock)
    {
        if (exponentialIceAgeBlockNumber >= 9000000)
            exponentialIceAgeBlockNumber -= 9000000;
        else
            exponentialIceAgeBlockNumber = 0;
    }
    // EIP-1234 Constantinople Ice Age delay
    else if (bi.number().asU256() >= _chainParams.constantinopleForkBlock)
    {
        if (exponentialIceAgeBlockNumber >= 5000000)
            exponentialIceAgeBlockNumber -= 5000000;
        else
            exponentialIceAgeBlockNumber = 0;
    }
    // EIP-649 Byzantium Ice Age delay
    else if (bi.number().asU256() >= _chainParams.byzantiumForkBlock)
    {
        if (exponentialIceAgeBlockNumber >= 3000000)
            exponentialIceAgeBlockNumber -= 3000000;
        else
            exponentialIceAgeBlockNumber = 0;
    }

    unsigned periodCount = exponentialIceAgeBlockNumber / c_expDiffPeriod;
    if (periodCount > 1)
        o += (bigint(1) << (periodCount - 2));  // latter will eventually become huge, so ensure it's a bigint.

    o = max<bigint>(minimumDifficulty, o);
    return u256(min<bigint>(o, std::numeric_limits<u256>::max()));
}


// Blockchain logic validator
void verifyEthereumBlockHeader(spBlockHeader const& _header, ToolChain const& _chain)
{
    // Check ethereum rules
    bool isLegacy = true;
    BlockHeader const& header = _header.getCContent();
    if (header.type() == BlockType::BlockHeader1559)
        isLegacy = false;

    if (isLegacy)
    {
        BlockHeaderLegacy const& legacyHeader = BlockHeaderLegacy::castFrom(_header);
        if (legacyHeader.gasLimit() > dev::u256("0x7fffffffffffffff"))
            throw test::UpwardsException("Header gasLimit > 0x7fffffffffffffff");
        if (legacyHeader.gasUsed() > legacyHeader.gasLimit())
            throw test::UpwardsException("Invalid gasUsed: header.gasUsed > header.gasLimit");
    }

    if (header.difficulty() < dev::u256("0x20000"))
        throw test::UpwardsException("Invalid difficulty: header.difficulty < 0x20000");
    if (header.extraData().asString().size() > 32 * 2 + 2)
        throw test::UpwardsException("Header extraData > 32 bytes");

    // Check DAO extraData
    if (_chain.fork().asString() == "HomesteadToDaoAt5" && header.number() > 4 && header.number() <= 5 + 9 &&
        header.extraData().asString() != "0x64616f2d686172642d666f726b")
        throw test::UpwardsException("BlockHeader require Dao ExtraData! (0x64616f2d686172642d666f726b)");

    bool found = false;
    for (auto const& block : _chain.blocks())
    {
        // See if uncles not already in chain
        if (block.header().getCContent().hash() == header.hash())
            throw test::UpwardsException("Block is already in chain!");
        for (auto const& un : block.uncles())
            if (un.getCContent().hash() == header.hash())
                throw test::UpwardsException("Block is already in chain!");

        if (block.header().getCContent().hash() == header.parentHash())
        {
            found = true;

            if (header.number() != block.header().getCContent().number() + 1)
                throw test::UpwardsException("BlockHeader number != parent.number + 1 (" + header.number().asDecString() +
                                             " != " + block.header().getCContent().number().asDecString() + ")");

            if (block.header().getCContent().timestamp() >= header.timestamp())
                throw test::UpwardsException("BlockHeader timestamp is less then it's parent block!");

            // Validate block gasLimit delta
            if (isLegacy && block.header().getCContent().type() == BlockType::BlockHeaderLegacy)
            {
                BlockHeaderLegacy const& legacyHeader = BlockHeaderLegacy::castFrom(_header);
                BlockHeaderLegacy const& legacyPrevBlock = BlockHeaderLegacy::castFrom(block.header());
                VALUE deltaGas = legacyPrevBlock.gasLimit().asU256() / 1024;
                if (legacyHeader.gasLimit().asU256() >= legacyPrevBlock.gasLimit().asU256() + deltaGas.asU256() ||
                    legacyHeader.gasLimit().asU256() <= legacyPrevBlock.gasLimit().asU256() - deltaGas.asU256())
                    throw test::UpwardsException("Invalid gaslimit: " + legacyHeader.gasLimit().asDecString() + ", want " +
                                                 legacyPrevBlock.gasLimit().asDecString() + " +/- " + deltaGas.asDecString());
            }

            // Validate block difficulty delta
            ChainOperationParams params = ChainOperationParams::defaultParams(_chain.toolParams());
            u256 newDiff = calculateEthashDifficulty(params, _header, block.header());
            if (header.difficulty().asU256() != newDiff)
                throw test::UpwardsException(
                    "Invalid difficulty: " + header.difficulty().asDecString() + ", want: " + VALUE(newDiff).asDecString());
        }
    }
    if (!found)
        throw test::UpwardsException(
            "verifyEthereumBlockHeader:: Parent block hash not found: " + _header.getCContent().parentHash().asString());
}


}  // namespace toolimpl
