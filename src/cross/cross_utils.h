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

#ifndef CROSS_UTILS_H_
#define CROSS_UTILS_H_
#include<ledger/ledger_manager.h>

namespace bumo {
	class CrossUtils : public utils::NonCopyable{
	public:
		CrossUtils() {}
		~CrossUtils() {}
		static int32_t QueryContract(const std::string &address, const std::string &input, Json::Value &query_rets);
		static int32_t SendTransaction(TransactionFrm::pointer tran_ptr);

		static TransactionFrm::pointer BuildTransaction(const std::string &private_key, const std::string &dest, const std::vector<std::string> &paras, int64_t nonce, int64_t fee_limit);
	private:
	};
}

#endif