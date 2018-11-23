/*
bumo is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

bumo is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with bumo.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "proposer_manager.h"
#include<cross/cross_utils.h>
#include<ledger/ledger_manager.h>
#include <glue/glue_manager.h>
#include <overlay/peer_manager.h>
#include <algorithm>
namespace bumo {
	ProposerManager::ProposerManager() :
		enabled_(false),
		thread_ptr_(NULL){
		update_count_ = 0;
		last_update_time_ = utils::Timestamp::HighResolution();
		cur_nonce_ = 0;
		main_chain_ = General::GetSelfChainId() == General::MAIN_CHAIN_ID;
	}

	ProposerManager::~ProposerManager(){
		if (thread_ptr_){
			delete thread_ptr_;
			thread_ptr_ = NULL;
		}
	}

	bool ProposerManager::Initialize(){
		if (!main_chain_){
			return true;
		}

		enabled_ = true;
		thread_ptr_ = new utils::Thread(this);
		if (!thread_ptr_->Start("ProposerManager")) {
			return false;
		}
		bumo::MessageChannel::Instance().RegisterMessageChannelConsumer(this, protocol::MESSAGE_CHANNEL_SUBMIT_HEAD);
		return true;
	}

	bool ProposerManager::Exit(){
		if (!main_chain_){
			return true;
		}

		enabled_ = false;
		if (thread_ptr_) {
			thread_ptr_->JoinWithStop();
		}
		bumo::MessageChannel::Instance().UnregisterMessageChannelConsumer(this, protocol::MESSAGE_CHANNEL_SUBMIT_HEAD);
		return true;
	}

	void ProposerManager::Run(utils::Thread *thread) {
		while (enabled_){
			int64_t current_time = utils::Timestamp::HighResolution();

			if ((current_time - last_update_time_) > 5 * utils::MICRO_UNITS_PER_SEC){
				UpdateLatestStatus();
				last_update_time_ = current_time;
				update_count_++;
			}
			
			if (update_count_ % 3 == 0){
				ProposeBlocks();
			}
		}
	}

	void ProposerManager::UpdateLatestStatus(){
		utils::MutexGuard guard(child_chain_map_lock_);
		for (int i = 0; i <= MAX_CHAIN_ID; i++){
			ChildChain &child_chain = child_chain_maps_[i];
			if (child_chain.ledger_map.empty()){
				continue;
			}

			//update latest validates 
			UpdateLatestValidates(i, child_chain.cmc_latest_validates);

			//update latest child seq
			UpdateLatestSeq(i, child_chain.cmc_latest_seq);

			//sort seq
			SortChildSeq(child_chain);
		}
	}

	void ProposerManager::UpdateLatestValidates(const int64_t chain_id, utils::StringList &latest_validates){
		latest_validates.clear();

		Json::Value input_value;
		Json::Value params;
		params["chain_id"] = chain_id;
		input_value["method"] = "queryChildChainValidators";
		input_value["params"] = params;

		Json::Value result_list;
		int32_t error_code = bumo::CrossUtils::QueryContract(General::CONTRACT_CMC_ADDRESS, input_value.toFastString(), result_list);

		Json::Value object;
		std::string result = result_list[Json::UInt(0)]["result"]["value"].asString();
		object.fromString(result.c_str());

		if (error_code != protocol::ERRCODE_SUCCESS){
			LOG_ERROR("Failed to query validators .%d", error_code);
			return;
		}

		if (!object["validators"].isArray()){
			LOG_ERROR("Failed to validators list is not array");
			return;
		}

		int32_t size = object["validators"].size();

		PrivateKey private_key(Configure::Instance().ledger_configure_.validation_privatekey_);
		for (int32_t i = 0; i < size; i++){
			std::string address = object["validators"][i].asString().c_str();
			latest_validates.push_back(address);
		}
		return;
	}

	void ProposerManager::UpdateLatestSeq(const int64_t chain_id, int64_t &seq){
		Json::Value params;
		params["chain_id"] = chain_id;
		params["header_hash"] = "";

		Json::Value input_value;
		input_value["method"] = "queryChildBlockHeader";
		input_value["params"] = params;

		Json::Value result_list;
		int32_t error_code = bumo::CrossUtils::QueryContract(General::CONTRACT_CMC_ADDRESS, input_value.toFastString(), result_list);
		std::string result = result_list[Json::UInt(0)]["result"]["value"].asString();
		Json::Value object;
		object.fromString(result.c_str());
		if (error_code != protocol::ERRCODE_SUCCESS){
			seq = -1;
			LOG_ERROR("Failed to query child block .%d", error_code);
			return;
		}

		seq = object["seq"].asInt64();
	}

	void ProposerManager::SortChildSeq(ChildChain &child_chain){
		//Empty queues, ignore it
		if (child_chain.ledger_map.empty()){
			return;
		}

		//If cmc = chain max, ignore it
		if (child_chain.cmc_latest_seq == child_chain.recv_max_seq){
			return;
		}

		//Deletes invalid blocks
		LedgerMap &ledger_map = child_chain.ledger_map;
		for (auto itr = ledger_map.begin(); itr != ledger_map.end();){
			if (itr->second.chain_id() > child_chain.cmc_latest_seq){
				itr++; 
				continue;
			}

			ledger_map.erase(itr++);
		}

		//Request up to ten blocks
		int64_t max_nums = MIN(MAX_REQUEST_BLOCK_NUMS, (child_chain.recv_max_seq - child_chain.cmc_latest_seq));
		for (int64_t i = child_chain.cmc_latest_seq + 1; i < max_nums; i++){
			auto itr = ledger_map.find(i);
			if (itr != ledger_map.end()){
				continue;
			}
			RequestChainSeq(child_chain.chain_id, i);
		}
	}

	void ProposerManager::RequestChainSeq(int64_t chain_id, int64_t seq){
		protocol::MessageChannel message_channel;
		protocol::MessageChannelQueryHead query_head;
		query_head.set_ledger_seq(seq);
		message_channel.set_target_chain_id(chain_id);
		message_channel.set_msg_type(protocol::MESSAGE_CHANNEL_QUERY_HEAD);
		message_channel.set_msg_data(query_head.SerializeAsString());
		bumo::MessageChannel::GetInstance()->MessageChannelProducer(message_channel);
	}

	void ProposerManager::ProposeBlocks(){
		utils::MutexGuard guard(child_chain_map_lock_);
		for (int64_t i = 0; i <= MAX_CHAIN_ID; i++){
			ChildChain &child_chain = child_chain_maps_[i];
			//No data, ignore it
			if (child_chain.ledger_map.empty()){
				continue;
			}

			//Blocks are latest, ignore it
			if ((child_chain.recv_max_seq > 0) && child_chain.recv_max_seq <= child_chain.cmc_latest_seq){
				LOG_ERROR("recv_max_seq <= cmc_latest_seq, may be error");
				continue;
			}

			//Submit the latest five blocks
			std::vector<std::string> send_para_list;
			LedgerMap &ledger_map = child_chain.ledger_map;
			for (int i = 1; i <= 5; i++){
				LedgerMap::const_iterator itr = ledger_map.find(child_chain.cmc_latest_seq + i);
				if (itr == ledger_map.end()){
					break;
				}
				Json::Value block_header = bumo::Proto2Json(itr->second);
				Json::Value input_value;
				Json::Value params;

				params["chain_id"] = i;
				params["block_header"] = block_header;
				input_value["method"] = "submitChildBlockHeader";
				input_value["params"] = params;
				send_para_list.push_back(input_value.toFastString());
			}

			if (send_para_list.empty()){
				LOG_ERROR("send_para_list is empty");
				return;
			}

			SendTransaction(send_para_list);
		}
	}

	void ProposerManager::HandleMessageChannelConsumer(const protocol::MessageChannel &message_channel){
		if (message_channel.msg_type() != protocol::MESSAGE_CHANNEL_SUBMIT_HEAD){
			LOG_ERROR("Failed to message_channel type is not MESSAGE_CHANNEL_SUBMIT_HEAD, error msg type is %d", message_channel.msg_type());
			return;
		}

		protocol::LedgerHeader ledger_header;
		ledger_header.ParseFromString(message_channel.msg_data());

		if (ledger_header.chain_id() <= 0 || ledger_header.chain_id() >= MAX_CHAIN_ID){
			LOG_ERROR("chain id :(" FMT_I64 ")", ledger_header.chain_id());
			return;
		}

		utils::MutexGuard guard(child_chain_map_lock_);
		ChildChain &child_chain = child_chain_maps_[ledger_header.chain_id()];
		child_chain.ledger_map[ledger_header.seq()] = ledger_header;
		child_chain.recv_max_seq = MAX(child_chain.recv_max_seq, ledger_header.seq());
		child_chain.chain_id = ledger_header.chain_id();
	}

	void ProposerManager::SendTransaction(const std::vector<std::string> &paras){
		int32_t err_code = 0;
		int64_t fee_limit = -1;

		for (int i = 0; i <= MAX_SEND_TRANSACTION_TIMES; i++){
			std::string private_key = Configure::Instance().ledger_configure_.validation_privatekey_;
			TransactionFrm::pointer trans = CrossUtils::BuildTransaction(private_key, General::CONTRACT_CMC_ADDRESS, paras, cur_nonce_, fee_limit);
			err_code = CrossUtils::SendTransaction(trans);
			if (err_code == protocol::ERRCODE_ALREADY_EXIST){
				break;
			}
			if (err_code == protocol::ERRCODE_FEE_NOT_ENOUGH){
				fee_limit = int64_t(trans->GetFeeLimit() * 1.12);
			}

			if (err_code == protocol::ERRCODE_BAD_SEQUENCE){
				cur_nonce_++;
			}

			if (err_code != protocol::ERRCODE_SUCCESS){
				LOG_ERROR("Send transaction erro code:%d", err_code);
			}

			utils::Sleep(10);
		}

		BreakProposer("SendTransaction than 10 times...");
		return;
	}

	void ProposerManager::BreakProposer(const std::string &error_des){
		enabled_ = false;
		assert(false);
		LOG_ERROR("%s", error_des.c_str());
	}
}
