// Copyright (c) 2022-present Telos Foundation & contributors
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "xevm_contract_runtime/sys_contract/xdelegate_usdt_contract.h"

#include "xcommon/xaccount_address.h"
#include "xcommon/xchain_uuid.h"
#include "xcommon/xeth_address.h"
#include "xdata/xnative_contract_address.h"
#include "xevm_common/common_data.h"
#include "xevm_common/xabi_decoder.h"
#include "xevm_common/xfixed_hash.h"
#include "xevm_contract_runtime/sys_contract/xdelegate_erc20_contract.h"
#include "xevm_contract_runtime/xerror/xerror.h"

#include <cinttypes>

NS_BEG4(top, contract_runtime, evm, sys_contract)

bool xtop_delegate_usdt_contract::execute(xbytes_t input,
                                          uint64_t target_gas,
                                          sys_contract_context const & context,
                                          bool is_static,
                                          observer_ptr<statectx::xstatectx_face_t> state_ctx,
                                          sys_contract_precompile_output & output,
                                          sys_contract_precompile_error & err) {
    assert(state_ctx);

    // chain_uuid (1 byte) | erc20_method_id (4 bytes) | parameters (depends)
    if (input.empty()) {
        err.fail_status = precompile_error::Fatal;
        err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

        xwarn("precompiled usdt contract: invalid input");

        return false;
    }

    common::xchain_uuid_t const chain_uuid{top::from_byte<common::xchain_uuid_t>(input.front())};
    if (chain_uuid != common::xchain_uuid_t::eth) {
        err.fail_status = precompile_error::Fatal;
        err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::NotSupported);

        xwarn("precompiled usdt contract: not supported token: %d", static_cast<int>(chain_uuid));

        return false;
    }

    std::error_code ec;
    evm_common::xabi_decoder_t abi_decoder = evm_common::xabi_decoder_t::build_from(xbytes_t{std::next(std::begin(input), 1), std::end(input)}, ec);
    if (ec) {
        err.fail_status = precompile_error::Fatal;
        err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

        xwarn("precompiled usdt contract: illegal input data");

        return false;
    }

    auto const function_selector = abi_decoder.extract<evm_common::xfunction_selector_t>(ec);
    if (ec) {
        err.fail_status = precompile_error::Fatal;
        err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

        xwarn("precompiled usdt contract: illegal input function selector");

        return false;
    }

    switch (function_selector.method_id) {
    case method_id_decimals: {
        xdbg("precompiled usdt contract: decimals");

        output.exit_status = Returned;
        output.cost = 0;
        output.output = top::to_bytes(evm_common::u256{18});

        return true;
    }

    case method_id_total_supply: {
        xdbg("precompiled usdt contract: totalSupply");

        uint64_t constexpr total_supply_gas_cost = 2538;
        if (target_gas < total_supply_gas_cost) {
            err.fail_status = Error;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitError::OutOfGas);

            xwarn("precompiled usdt contract: totalSupply out of gas, gas remained %" PRIu64 " gas required %" PRIu64, target_gas, total_supply_gas_cost);

            return false;
        }

        if (!abi_decoder.empty()) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: totalSupply with non-empty parameter");

            return false;
        }

        evm_common::u256 supply{"45257057549529550000000000000"};
        output.exit_status = Returned;
        output.cost = 0;
        output.output = top::to_bytes(supply);

        return true;
    }

    case method_id_balance_of: {
        xdbg("precompiled usdt contract: balanceOf");

        uint64_t constexpr balance_of_gas_cost = 3268;
        if (target_gas < balance_of_gas_cost) {
            err.fail_status = precompile_error::Error;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitError::OutOfGas);

            xwarn("precompiled usdt contract: balanceOf out of gas, gas remained %" PRIu64 " gas required %" PRIu64, target_gas, balance_of_gas_cost);

            return false;
        }

        if (abi_decoder.size() != 1) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: balance_of with invalid parameter (parameter count not one)");

            return false;
        }

        auto const & eth_address = abi_decoder.extract<common::xeth_address_t>(ec);
        if (ec) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: balance_of invalid account");

            return false;
        }

        auto state = state_ctx->load_unit_state(common::xaccount_address_t::build_from(eth_address, base::enum_vaccount_addr_type_secp256k1_evm_user_account).vaccount());

        evm_common::u256 value = state->tep_token_balance(common::xtoken_id_t::usdt);
        output.cost = 0;
        output.exit_status = Returned;
        output.output = top::to_bytes(value);
        assert(output.output.size() == 32);

        return true;
    }

    case method_id_transfer: {
        xdbg("precompiled usdt contract: transfer");

        uint64_t constexpr transfer_gas_cost = 18446;
        xbytes_t result(32, 0);

        if (is_static) {
            err.fail_status = precompile_error::Revert;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitRevert::Reverted);
            err.cost = transfer_gas_cost;
            err.output = result;

            xwarn("precompiled usdt contract: transfer is not allowed in static context");

            return false;
        }

        if (target_gas < transfer_gas_cost) {
            err.fail_status = precompile_error::Error;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitError::OutOfGas);

            xwarn("precompiled usdt contract: transfer out of gas, gas remained %" PRIu64 " gas required %" PRIu64, target_gas, transfer_gas_cost);

            return false;
        }

        if (abi_decoder.size() != 2) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: transfer with invalid parameter");

            return false;
        }

        auto const & recipient_address = abi_decoder.extract<common::xeth_address_t>(ec);
        if (ec) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: transfer with invalid account");

            return false;
        }
        auto const & recipient_account_address = common::xaccount_address_t::build_from(recipient_address, base::enum_vaccount_addr_type_secp256k1_evm_user_account);

        evm_common::u256 const value = abi_decoder.extract<evm_common::u256>(ec);
        if (ec) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: transfer with invalid value");

            return false;
        }

        auto sender_state = state_ctx->load_unit_state(common::xaccount_address_t::build_from(context.caller, base::enum_vaccount_addr_type_secp256k1_evm_user_account).vaccount());
        auto recver_state = state_ctx->load_unit_state(recipient_account_address.vaccount());

        sender_state->transfer(common::xtoken_id_t::usdt, top::make_observer(recver_state.get()), value, ec);

        if (!ec) {
            auto const & contract_address = context.address;
            auto const & caller_address = context.caller;

            evm_common::xh256s_t topics;
            topics.push_back(evm_common::xh256_t(event_hex_string_transfer));
            topics.push_back(evm_common::xh256_t(caller_address.to_h256()));
            topics.push_back(evm_common::xh256_t(recipient_address.to_h256()));
            evm_common::xevm_log_t log(contract_address, topics, top::to_bytes(value));

            result[31] = 1;

            output.cost = 0;
            output.exit_status = Returned;
            output.output = result;
            output.logs.push_back(log);
        } else {
            uint64_t constexpr transfer_reverted_gas_cost = 3662;

            err.fail_status = precompile_error::Revert;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitRevert::Reverted);
            err.cost = transfer_reverted_gas_cost;
            err.output = result;

            xwarn("precompiled usdt contract: transfer reverted. ec %" PRIi32 " category %s msg %s", ec.value(), ec.category().name(), ec.message().c_str());
        }

        return !ec;
    }

    case method_id_transfer_from: {
        xdbg("precompiled usdt contract: transferFrom");

        uint64_t constexpr transfer_from_gas_cost = 18190;
        xbytes_t result(32, 0);

        if (is_static) {
            err.fail_status = precompile_error::Revert;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitRevert::Reverted);
            err.cost = transfer_from_gas_cost;
            err.output = result;

            xwarn("precompiled usdt contract: transferFrom is not allowed in static context");

            return false;
        }

        if (target_gas < transfer_from_gas_cost) {
            err.fail_status = precompile_error::Error;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitError::OutOfGas);

            xwarn("precompiled usdt contract: transferFrom out of gas, gas remained %" PRIu64 " gas required %" PRIu64, target_gas, transfer_from_gas_cost);

            return false;
        }

        if (abi_decoder.size() != 3) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: transferFrom with invalid parameters");

            return false;
        }

        auto const & owner_address = abi_decoder.extract<common::xeth_address_t>(ec);
        if (ec) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: transferFrom invalid owner account");

            return false;
        }

        auto const & recipient_address = abi_decoder.extract<common::xeth_address_t>(ec);
        if (ec) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: transferFrom invalid recipient account");

            return false;
        }

        auto const & owner_account_address = common::xaccount_address_t::build_from(owner_address, base::enum_vaccount_addr_type_secp256k1_evm_user_account);
        auto const & recipient_account_address = common::xaccount_address_t::build_from(recipient_address, base::enum_vaccount_addr_type_secp256k1_evm_user_account);

        auto const value = abi_decoder.extract<evm_common::u256>(ec);
        if (ec) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: transferFrom invalid value");

            return false;
        }

        auto owner_state = state_ctx->load_unit_state(owner_account_address.vaccount());
        owner_state->update_allowance(common::xtoken_id_t::usdt,
                                      common::xaccount_address_t::build_from(context.caller, base::enum_vaccount_addr_type_secp256k1_evm_user_account),
                                      value,
                                      data::xallowance_update_op_t::decrease,
                                      ec);
        if (!ec) {
            auto recver_state = state_ctx->load_unit_state(recipient_account_address.vaccount());
            owner_state->transfer(common::xtoken_id_t::usdt, top::make_observer(recver_state.get()), value, ec);
            if (!ec) {
                result[31] = 1;
            }
        }

        if (!ec) {
            auto const & contract_address = context.address;

            evm_common::xh256s_t topics;
            topics.push_back(evm_common::xh256_t(event_hex_string_transfer));
            topics.push_back(evm_common::xh256_t(owner_address.to_h256()));
            topics.push_back(evm_common::xh256_t(recipient_address.to_h256()));
            evm_common::xevm_log_t log(contract_address, topics, top::to_bytes(value));

            result[31] = 1;

            output.cost = 0;
            output.exit_status = Returned;
            output.output = result;
            output.logs.push_back(log);
        } else {
            uint64_t constexpr transfer_from_reverted_gas_cost = 4326;
            err.fail_status = precompile_error::Revert;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitRevert::Reverted);
            err.cost = transfer_from_reverted_gas_cost;
            err.output = result;

            xwarn("precompiled usdt contract: transferFrom reverted. ec %" PRIi32 " category %s msg %s", ec.value(), ec.category().name(), ec.message().c_str());
        }

        return !ec;
    }

    case method_id_approve: {
        xdbg("precompiled usdt contract: approve");

        uint64_t constexpr approve_gas_cost = 18599;
        xbytes_t result(32, 0);
        if (is_static) {
            err.fail_status = precompile_error::Revert;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitRevert::Reverted);
            err.cost = approve_gas_cost;
            err.output = result;

            xwarn("precompiled usdt contract: approve is not allowed in static context");

            return false;
        }

        if (target_gas < approve_gas_cost) {
            err.fail_status = precompile_error::Error;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitError::OutOfGas);

            xwarn("precompiled usdt contract: approve out of gas, gas remained %" PRIu64 " gas required %" PRIu64, target_gas, approve_gas_cost);

            return false;
        }

        if (abi_decoder.size() != 2) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: approve with invalid parameter");

            return false;
        }

        auto const & spender_address = abi_decoder.extract<common::xeth_address_t>(ec);
        if (ec) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: approve invalid spender account");

            return false;
        }
        auto const & spender_account_address = common::xaccount_address_t::build_from(spender_address, base::enum_vaccount_addr_type_secp256k1_evm_user_account);

        auto const amount = abi_decoder.extract<evm_common::u256>(ec);
        if (ec) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: approve invalid value");

            return false;
        }

        auto sender_state = state_ctx->load_unit_state(common::xaccount_address_t::build_from(context.caller, base::enum_vaccount_addr_type_secp256k1_evm_user_account).vaccount());
        sender_state->approve(common::xtoken_id_t::usdt, spender_account_address, amount, ec);

        auto const & contract_address = context.address;
        auto const & caller_address = context.caller;

        if (!ec) {
            evm_common::xh256s_t topics;
            topics.push_back(evm_common::xh256_t(event_hex_string_approve));
            topics.push_back(evm_common::xh256_t(caller_address.to_h256()));
            topics.push_back(evm_common::xh256_t(spender_address.to_h256()));
            evm_common::xevm_log_t log(contract_address, topics, top::to_bytes(amount));
            result[31] = 1;

            output.cost = 0;
            output.exit_status = Returned;
            output.output = result;
            output.logs.push_back(log);
        } else {
            err.fail_status = precompile_error::Revert;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitRevert::Reverted);
            err.cost = approve_gas_cost / 2;
            err.output = result;

            xerror("precompiled usdt contract: approve reverted. ec %" PRIi32 " category %s msg %s", ec.value(), ec.category().name(), ec.message().c_str());
        }

        return true;
    }

    case method_id_allowance: {
        xdbg("precompiled usdt contract: allowance");

        uint64_t constexpr allowance_gas_cost = 3987;
        if (target_gas < allowance_gas_cost) {
            err.fail_status = precompile_error::Error;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitError::OutOfGas);

            xwarn("precompiled usdt contract: allowance out of gas. gas remained %" PRIu64 " gas required %" PRIu64, target_gas, allowance_gas_cost);

            return false;
        }

        xbytes_t result(32, 0);
        if (abi_decoder.size() != 2) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: allowance with invalid parameter");

            return false;
        }

        auto const & owner_address = abi_decoder.extract<common::xeth_address_t>(ec);
        if (ec) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: allowance invalid owner account");

            return false;
        }

        auto const & spender_address = abi_decoder.extract<common::xeth_address_t>(ec);
        if (ec) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: allowance invalid spender account");

            return false;
        }

        common::xaccount_address_t owner_account_address = common::xaccount_address_t::build_from(owner_address, base::enum_vaccount_addr_type_secp256k1_evm_user_account);
        common::xaccount_address_t spender_account_address = common::xaccount_address_t::build_from(spender_address, base::enum_vaccount_addr_type_secp256k1_evm_user_account);

        auto owner_state = state_ctx->load_unit_state(owner_account_address.vaccount());
        result = top::to_bytes(owner_state->allowance(common::xtoken_id_t::usdt, spender_account_address, ec));
        assert(result.size() == 32);

        output.cost = 0;
        output.output = result;
        output.exit_status = Returned;

        return true;
    }

    case method_id_mint: {
        xdbg("precompiled usdt contract: mint");

        uint64_t constexpr mint_gas_cost = 3155;
        xbytes_t result(32, 0);

        if (is_static) {
            err.fail_status = precompile_error::Revert;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitRevert::Reverted);
            err.cost = mint_gas_cost;
            err.output = result;

            xwarn("precompiled usdt contract: mint is not allowed in static context");

            return false;
        }

        // Only controller can mint tokens.
        auto const & contract_state = state_ctx->load_unit_state(evm_usdt_contract_address.vaccount());
        auto const & msg_sender = common::xaccount_address_t::build_from(context.caller, base::enum_vaccount_addr_type_secp256k1_evm_user_account);
        auto const & token_controller = contract_state->tep_token_controller(chain_uuid);
        if (msg_sender != token_controller) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: mint called by non-admin account %s", context.caller.c_str());

            return false;
        }

        if (target_gas < mint_gas_cost) {
            err.fail_status = precompile_error::Error;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitError::OutOfGas);

            xwarn("precompiled usdt contract: mint out of gas, gas remained %" PRIu64 " gas required %" PRIu64, target_gas, mint_gas_cost);

            return false;
        }

        if (abi_decoder.size() != 2) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: mint with invalid parameter");

            return false;
        }

        auto const & recver = abi_decoder.extract<common::xeth_address_t>(ec);
        if (ec) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: mint with invalid receiver address");

            return false;
        }
        auto const recver_address = common::xaccount_address_t::build_from(recver, base::enum_vaccount_addr_type_secp256k1_evm_user_account);

        evm_common::u256 const value = abi_decoder.extract<evm_common::u256>(ec);
        if (ec) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: mint with invalid value");

            return false;
        }

        auto recver_state = state_ctx->load_unit_state(recver_address.vaccount());
        recver_state->tep_token_deposit(common::xtoken_id_t::usdt, value);

        if (!ec) {
            auto const & contract_address = context.address;
            auto const & recipient_address = recver;

            evm_common::xh256s_t topics;
            topics.push_back(evm_common::xh256_t(event_hex_string_transfer));
            topics.push_back(evm_common::xh256_t(common::xeth_address_t::zero().to_h256()));
            topics.push_back(evm_common::xh256_t(recipient_address.to_h256()));
            evm_common::xevm_log_t log(contract_address, topics, top::to_bytes(value));
            result[31] = 1;

            output.cost = 0;
            output.exit_status = Returned;
            output.output = result;
            output.logs.push_back(log);
        } else {
            err.fail_status = precompile_error::Revert;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitRevert::Reverted);
            err.cost = mint_gas_cost;
            err.output = result;

            xwarn("precompiled usdt contract: mint reverted. ec %" PRIi32 " category %s msg %s", ec.value(), ec.category().name(), ec.message().c_str());
        }

        return !ec;
    }

    case method_id_burn_from: {
        xdbg("precompiled usdt contract: burnFrom");

        uint64_t constexpr burn_gas_cost = 3155;
        xbytes_t result(32, 0);

        if (is_static) {
            err.fail_status = precompile_error::Revert;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitRevert::Reverted);
            err.cost = burn_gas_cost;
            err.output = result;

            xwarn("precompiled usdt contract: burnFrom is not allowed in static context");

            return false;
        }

        auto const & contract_state = state_ctx->load_unit_state(evm_usdt_contract_address.vaccount());
        auto const & msg_sender = common::xaccount_address_t::build_from(context.caller, base::enum_vaccount_addr_type_secp256k1_evm_user_account);
        auto const & token_controller = contract_state->tep_token_controller(chain_uuid);
        if (msg_sender != token_controller) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: burnFrom called by non-admin account %s", context.caller.c_str());

            return false;
        }

        if (target_gas < burn_gas_cost) {
            err.fail_status = precompile_error::Error;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitError::OutOfGas);

            xwarn("precompiled usdt contract: burnFrom out of gas, gas remained %" PRIu64 " gas required %" PRIu64, target_gas, burn_gas_cost);

            return false;
        }

        if (abi_decoder.size() != 2) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: burnFrom with invalid parameter");

            return false;
        }

        auto const & burn_from = abi_decoder.extract<common::xeth_address_t>(ec);
        if (ec) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: burnFrom with invalid burn from address");

            return false;
        }
        auto const & burn_from_address = common::xaccount_address_t::build_from(burn_from, base::enum_vaccount_addr_type_secp256k1_evm_user_account);

        evm_common::u256 const value = abi_decoder.extract<evm_common::u256>(ec);
        if (ec) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: burnFrom with invalid value");

            return false;
        }

        auto recver_state = state_ctx->load_unit_state(burn_from_address.vaccount());
        recver_state->tep_token_withdraw(common::xtoken_id_t::usdt, value);

        if (!ec) {
            auto const & contract_address = context.address;

            evm_common::xh256s_t topics;
            topics.push_back(evm_common::xh256_t(event_hex_string_transfer));
            topics.push_back(evm_common::xh256_t(burn_from.to_h256()));
            topics.push_back(evm_common::xh256_t(common::xeth_address_t::zero().to_h256()));
            evm_common::xevm_log_t log(contract_address, topics, top::to_bytes(value));
            result[31] = 1;

            output.cost = 0;
            output.exit_status = Returned;
            output.output = result;
            output.logs.push_back(log);
        } else {
            err.fail_status = precompile_error::Revert;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitRevert::Reverted);
            err.cost = burn_gas_cost;
            err.output = result;

            xwarn("precompiled usdt contract: burnFrom reverted. ec %" PRIi32 " category %s msg %s", ec.value(), ec.category().name(), ec.message().c_str());
        }

        return !ec;
    }

    case method_id_transfer_ownership: {
        xdbg("precompiled usdt contract: transferOwnership");

        uint64_t constexpr transfer_ownership_gas_cost = 3155;
        xbytes_t result(32, 0);

        if (is_static) {
            err.fail_status = precompile_error::Revert;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitRevert::Reverted);
            err.cost = transfer_ownership_gas_cost;
            err.output = result;

            xwarn("precompiled usdt contract: transferOwnership is not allowed in static context");

            return false;
        }

        assert(chain_uuid == common::xchain_uuid_t::eth);

        auto contract_state = state_ctx->load_unit_state(evm_usdt_contract_address.vaccount());
        auto const & msg_sender = common::xaccount_address_t::build_from(context.caller, base::enum_vaccount_addr_type_secp256k1_evm_user_account);
        auto const & token_owner = contract_state->tep_token_owner(chain_uuid);
        if (msg_sender != token_owner) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: transferOwnership called by non-admin account %s", context.caller.c_str());

            return false;
        }
        ec.clear();

        if (target_gas < transfer_ownership_gas_cost) {
            err.fail_status = precompile_error::Error;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitError::OutOfGas);

            xwarn("precompiled usdt contract: transferOwnership out of gas, gas remained %" PRIu64 " gas required %" PRIu64, target_gas, transfer_ownership_gas_cost);

            return false;
        }

        if (abi_decoder.size() != 1) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: transferOwnership with invalid parameter");

            return false;
        }

        auto const new_owner = abi_decoder.extract<common::xeth_address_t>(ec);
        if (ec) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: transferOwnership with invalid burn from address");

            return false;
        }

        contract_state->tep_token_owner(chain_uuid, common::xaccount_address_t::build_from(new_owner, base::enum_vaccount_addr_type_secp256k1_evm_user_account), ec);
        if (!ec) {
            auto const & contract_address = context.address;

            evm_common::xh256s_t topics;
            topics.push_back(evm_common::xh256_t(event_hex_string_ownership_transferred));
            topics.push_back(evm_common::xh256_t(context.caller.to_h256()));
            topics.push_back(evm_common::xh256_t(new_owner.to_h256()));
            evm_common::xevm_log_t log{contract_address, topics};
            result[31] = 1;

            output.cost = 0;
            output.exit_status = Returned;
            output.output = result;
            output.logs.push_back(std::move(log));
        } else {
            err.fail_status = precompile_error::Revert;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitRevert::Reverted);
            err.cost = transfer_ownership_gas_cost;
            err.output = result;

            xwarn("precompiled usdt contract: transferOwnership reverted. ec %" PRIi32 " category %s msg %s", ec.value(), ec.category().name(), ec.message().c_str());
        }

        return !ec;
    }

    case method_id_set_controller: {
        xdbg("precompiled usdt contract: setController");

        uint64_t constexpr set_controller_gas_cost = 3155;
        xbytes_t result(32, 0);

        if (is_static) {
            err.fail_status = precompile_error::Revert;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitRevert::Reverted);
            err.cost = set_controller_gas_cost;
            err.output = result;

            xwarn("precompiled usdt contract: setController is not allowed in static context");

            return false;
        }

        assert(chain_uuid == common::xchain_uuid_t::eth);

        // only contract owner can set controller.
        auto contract_state = state_ctx->load_unit_state(evm_usdt_contract_address.vaccount());
        auto const & msg_sender = common::xaccount_address_t::build_from(context.caller, base::enum_vaccount_addr_type_secp256k1_evm_user_account);
        auto const & token_owner = contract_state->tep_token_owner(chain_uuid);
        if (msg_sender != token_owner) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: setController called by non-admin account %s", context.caller.c_str());

            return false;
        }
        ec.clear();

        if (target_gas < set_controller_gas_cost) {
            err.fail_status = precompile_error::Error;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitError::OutOfGas);

            xwarn("precompiled usdt contract: setController out of gas, gas remained %" PRIu64 " gas required %" PRIu64, target_gas, set_controller_gas_cost);

            return false;
        }

        if (abi_decoder.size() != 1) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: setController with invalid parameter");

            return false;
        }

        auto const new_controller = abi_decoder.extract<common::xeth_address_t>(ec);
        if (ec) {
            err.fail_status = precompile_error::Fatal;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::Other);

            xwarn("precompiled usdt contract: setController with invalid burn from address");

            return false;
        }

        auto const & old_controller = common::xeth_address_t::build_from(contract_state->tep_token_controller(chain_uuid));
        contract_state->tep_token_controller(chain_uuid, common::xaccount_address_t::build_from(new_controller, base::enum_vaccount_addr_type_secp256k1_evm_user_account), ec);

        if (!ec) {
            auto const & contract_address = context.address;

            evm_common::xh256s_t topics;
            topics.push_back(evm_common::xh256_t(event_hex_string_controller_set));
            topics.push_back(evm_common::xh256_t(old_controller.to_h256()));
            topics.push_back(evm_common::xh256_t(new_controller.to_h256()));
            evm_common::xevm_log_t log{contract_address, topics};
            result[31] = 1;

            output.cost = 0;
            output.exit_status = Returned;
            output.output = result;
            output.logs.push_back(std::move(log));
        } else {
            err.fail_status = precompile_error::Revert;
            err.minor_status = static_cast<uint32_t>(precompile_error_ExitRevert::Reverted);
            err.cost = set_controller_gas_cost;
            err.output = result;

            xwarn("precompiled usdt contract: setController reverted. ec %" PRIi32 " category %s msg %s", ec.value(), ec.category().name(), ec.message().c_str());
        }

        return !ec;
    }

    case method_id_owner: {
        xdbg("precompiled usdt contract: owner");

        xbytes_t result(32, 0);

        auto contract_state = state_ctx->load_unit_state(evm_usdt_contract_address.vaccount());
        auto const & token_owner = contract_state->tep_token_owner(chain_uuid);
        auto const owner = common::xeth_address_t::build_from(token_owner);

        output.cost = 0;
        output.exit_status = Returned;
        output.output = owner.to_h256();

        return true;
    }

    case method_id_controller: {
        xdbg("precompiled usdt contract: controller");
        
        xbytes_t result(32, 0);

        auto contract_state = state_ctx->load_unit_state(evm_usdt_contract_address.vaccount());
        auto const & controller = common::xeth_address_t::build_from(contract_state->tep_token_controller(chain_uuid));

        output.cost = 0;
        output.exit_status = Returned;
        output.output = controller.to_h256();

        return true;
    }

    default: {
        err.fail_status = precompile_error::Fatal;
        err.minor_status = static_cast<uint32_t>(precompile_error_ExitFatal::NotSupported);

        xwarn("precompiled usdt contract: not supported method_id: %" PRIx32, function_selector.method_id);

        return false;
    }
    }
}

NS_END4
