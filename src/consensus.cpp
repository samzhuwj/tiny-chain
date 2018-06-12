#include <algorithm>
#include <tinychain/tinychain.hpp>
#include <tinychain/consensus.hpp>
#include <tinychain/blockchain.hpp>

namespace tinychain
{

void miner::start(address_t& addr){
    for(;;) {
        block new_block;


        // 未找到，继续找
        if (!pow_once(new_block, addr)) {
            continue;
        }

        // pool已经被清空
        chain_.pool_reset();

        // 调用网络广播

        // 本地存储
        chain_.push_block(new_block);
    }
}

tx miner::create_coinbase_tx(address_t& addr) {
    return tx{addr};
}

bool miner::pow_once(block& new_block, address_t& addr) {

    auto&& pool = chain_.pool();

    auto&& prev_block = chain_.get_last_block();

    // 填充新块
    new_block.header_.height = prev_block.header_.height + 1;
    new_block.header_.prev_hash = prev_block.header_.hash;

    new_block.header_.timestamp = get_now_timestamp();

    new_block.header_.tx_count = pool.size();

    // 难度调整: 
    // 控制每块速度，控制最快速度，大约10秒
    uint64_t time_peroid = new_block.header_.timestamp - prev_block.header_.timestamp;
    //log::info("consensus") << "target:" << ncan;

    if (time_peroid <= 10u) {
        new_block.header_.difficulty = prev_block.header_.difficulty + 9000;
    } else {
        new_block.header_.difficulty = prev_block.header_.difficulty - 3000;
    }
    // 计算挖矿目标值,最大值除以难度就目标值
    uint64_t target = 0xffffffffffffffff / prev_block.header_.difficulty;

    // 设置coinbase交易
    auto&& tx = create_coinbase_tx(addr);
    pool.push_back(tx);

    // 装载交易
    new_block.setup(pool);

    // 计算目标值
    for ( uint64_t n = 0; ; ++n) {
        //尝试候选目标值
        new_block.header_.nonce = n;
        auto&& jv_block = new_block.to_json();
        auto&& can = to_sha256(jv_block);
        uint64_t ncan = std::stoull(can.substr(0, 16), 0, 16); //截断前16位，转换uint64 后进行比较

        // 找到了
        if (ncan < target) {
            //log::info("consensus") << "target:" << ncan;
            //log::info("consensus") << "hash  :" << to_sha256(jv_block);
            new_block.header_.hash = can;
            log::info("consensus") << "new block :" << jv_block.toStyledString();
            log::info("consensus") << "new block :" << can;
            return true;
        }
    }

    return false;
}

bool validate_tx(blockchain& chain, const tx& new_tx) {
    // input exsited?
    auto&& inputs = new_tx.inputs();
    for (auto& each : inputs) {

        tx pt;
        if (!chain.get_tx(each.first, pt)) {
            return false;
        }

        log::info("consensus")<<pt.to_json().toStyledString();
        //auto&& tl = pt.outputs();
    }

    return true;
}

bool validate_block(const tx& new_block) {

    return true;
}


} //tinychain

