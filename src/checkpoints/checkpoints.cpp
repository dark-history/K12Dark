// Copyright (c) 2014-2019, AEON, The Monero Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers

#include "include_base_utils.h"

using namespace epee;

#include "checkpoints.h"

#include "common/dns_utils.h"
#include "include_base_utils.h"
#include "string_tools.h"
#include <functional>
#include "storages/portable_storage_template_helper.h" // epee json include
#include "serialization/keyvalue_serialization.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "checkpoints"

namespace cryptonote
{
  /**
   * @brief struct for loading a checkpoint from json
   */
  struct t_hashline
  {
    uint64_t height; //!< the height of the checkpoint
    std::string hash; //!< the hash for the checkpoint
        BEGIN_KV_SERIALIZE_MAP()
          KV_SERIALIZE(height)
          KV_SERIALIZE(hash)
        END_KV_SERIALIZE_MAP()
  };

  /**
   * @brief struct for loading many checkpoints from json
   */
  struct t_hash_json {
    std::vector<t_hashline> hashlines; //!< the checkpoint lines from the file
        BEGIN_KV_SERIALIZE_MAP()
          KV_SERIALIZE(hashlines)
        END_KV_SERIALIZE_MAP()
  };

  //---------------------------------------------------------------------------
  checkpoints::checkpoints()
  {
  }
  //---------------------------------------------------------------------------
  bool checkpoints::add_checkpoint(uint64_t height, const std::string& hash_str, const std::string& difficulty_str)
  {
    crypto::hash h = crypto::null_hash;
    bool r = epee::string_tools::parse_tpod_from_hex_string(hash_str, h);
    CHECK_AND_ASSERT_MES(r, false, "Failed to parse checkpoint hash string into binary representation!");

    // return false if adding at a height we already have AND the hash is different
    if (m_points.count(height))
    {
      CHECK_AND_ASSERT_MES(h == m_points[height], false, "Checkpoint at given height already exists, and hash for new checkpoint was different!");
    }
    m_points[height] = h;
    if (!difficulty_str.empty())
    {
      try
      {
        difficulty_type difficulty(difficulty_str);
        if (m_difficulty_points.count(height))
        {
          CHECK_AND_ASSERT_MES(difficulty == m_difficulty_points[height], false, "Difficulty checkpoint at given height already exists, and difficulty for new checkpoint was different!");
        }
        m_difficulty_points[height] = difficulty;
      }
      catch (...)
      {
        LOG_ERROR("Failed to parse difficulty checkpoint: " << difficulty_str);
        return false;
      }
    }
    return true;
  }
  //---------------------------------------------------------------------------
  bool checkpoints::is_in_checkpoint_zone(uint64_t height) const
  {
    return !m_points.empty() && (height <= (--m_points.end())->first);
  }
  //---------------------------------------------------------------------------
  bool checkpoints::check_block(uint64_t height, const crypto::hash& h, bool& is_a_checkpoint) const
  {
    auto it = m_points.find(height);
    is_a_checkpoint = it != m_points.end();
    if(!is_a_checkpoint)
      return true;

    if(it->second == h)
    {
      MINFO("CHECKPOINT PASSED FOR HEIGHT " << height << " " << h);
      return true;
    }else
    {
      MWARNING("CHECKPOINT FAILED FOR HEIGHT " << height << ". EXPECTED HASH: " << it->second << ", FETCHED HASH: " << h);
      return false;
    }
  }
  //---------------------------------------------------------------------------
  bool checkpoints::check_block(uint64_t height, const crypto::hash& h) const
  {
    bool ignored;
    return check_block(height, h, ignored);
  }
  //---------------------------------------------------------------------------
  //FIXME: is this the desired behavior?
  bool checkpoints::is_alternative_block_allowed(uint64_t blockchain_height, uint64_t block_height) const
  {
    if (0 == block_height)
      return false;

    auto it = m_points.upper_bound(blockchain_height);
    // Is blockchain_height before the first checkpoint?
    if (it == m_points.begin())
      return true;

    --it;
    uint64_t checkpoint_height = it->first;
    return checkpoint_height < block_height;
  }
  //---------------------------------------------------------------------------
  uint64_t checkpoints::get_max_height() const
  {
    if (m_points.empty())
      return 0;
    return m_points.rbegin()->first;
  }
  //---------------------------------------------------------------------------
  const std::map<uint64_t, crypto::hash>& checkpoints::get_points() const
  {
    return m_points;
  }
  //---------------------------------------------------------------------------
  const std::map<uint64_t, difficulty_type>& checkpoints::get_difficulty_points() const
  {
    return m_difficulty_points;
  }

  bool checkpoints::check_for_conflicts(const checkpoints& other) const
  {
    for (auto& pt : other.get_points())
    {
      if (m_points.count(pt.first))
      {
        CHECK_AND_ASSERT_MES(pt.second == m_points.at(pt.first), false, "Checkpoint at given height already exists, and hash for new checkpoint was different!");
      }
    }
    return true;
  }

  bool checkpoints::init_default_checkpoints(network_type nettype)
  {
    if (nettype == TESTNET)
    {
      ADD_CHECKPOINT2(0,       "d0ae70ae5f35b27b3a4b68ae4f78eeca9989174eebe8a1e55cd139fe9b797223", "1");
      ADD_CHECKPOINT2(50000,   "71f59410da0bf670c6f24c91f0f940c5c3541a150a38f5fa7f9c520a230158d9", "947184082");
      ADD_CHECKPOINT2(100000,  "5e321820d2c5c10640b9a933a660d610bfa7db7d8ab3a8531d642a914cfb9747", "1436381728");
      return true;
    }
    if (nettype == STAGENET)
    {
      ADD_CHECKPOINT2(0,       "d833094ccf64a3f05292a6ce6b61cb42c3490115ac1a80390b6bced0df8f1416", "1");
      ADD_CHECKPOINT2(80000,   "a6b4d686c5b9fcf7a73e8e3e63bda1989d608c55bff391235500b2a1720d2ad4", "8824240");
      ADD_CHECKPOINT2(200000,  "b51c712e2afcf575cd79df1c473e83c4fdcfbce00934cde03a480b8ac81771e1", "2186992746");
      return true;
    }
    ADD_CHECKPOINT2(1,      "1440a20f078bf3264822234b347f8382606577d73d4e9d3cb7296d73889bc421", "2");
    ADD_CHECKPOINT2(100,    "6dd13aaab16679f49ee6b2b75c7dc99b1fd09ab2282b18cb4b55b73110655742", "120823772");
    ADD_CHECKPOINT2(1000,   "bc6458452fd0575a314089bf302f6fd68ebaa2d689c42f3365293b96bbdf1f25", "9161286978");
    ADD_CHECKPOINT2(10000,  "1ac1ebd25baf0d6ec593daa3389f1aa7e860ff2cc29f3cf1be586d245b379da4", "71151659200");
    ADD_CHECKPOINT2(15000,  "15567af42afc1ed00538f53b5e3822d421e3ed6372ca79f4ea4e3e3bab709a87", "102902810479");
    ADD_CHECKPOINT2(175500, "3f7dd748b3b863b04654d87a387f2b65a365f467188971f3192eab2368e64a35", "1382142910023");
    ADD_CHECKPOINT2(450000, "f69a6e57c4dd5df2f492c9d31c50f11aad2c25a64d540ce5f5d11b572aec8ab7", "1604326150186");
    ADD_CHECKPOINT2(540000, "94e19cf9d5a16ae90f67c321f8376b87da21d6d6c2cb0957b9ab558dca66c1dc", "1691603760435");
    ADD_CHECKPOINT2(592001, "e8bc936b287a9c426a15cf127624b064c88e6d37655cc87f9a62cf1623c62385", "1817694366711");
    ADD_CHECKPOINT2(798358, "804c7fe07511d9387e7cda534c9e8b644d406d8d0ff299799a8177850d4e75a0", "26676268161857");
    ADD_CHECKPOINT2(871000, "99f7e5460da3fb4e2b15214017b0a17ff0294823ad852259ff837f0ffeeb90f0", "74226941049292");
    ADD_CHECKPOINT2(959800, "8a89ede82ae1e3408703feae87c99bccca8455744743eede02bd76b43d202dc6", "716458758970498");
    ADD_CHECKPOINT2(1026000, "ea5ace68a81b3b50ec27a457799fec63a9d45acd35a38e31e2b7b5be2315a13e", "997668660280133");
    ADD_CHECKPOINT2(1133000, "4efa7a1aa943b3d1a9cd8807cdb34ba10767e6437876e28964dcdf1bb4da62e2", "1627621413427613");
    ADD_CHECKPOINT2(1157000, "320304a96228979a9565c550c666a5ceaf2f2dbd99aa5ff8354385fb515be7ea", "11699971284450871988");
    ADD_CHECKPOINT2(1260000, "6afc9c592c80638da2a11cedd716926f5c63b877945855808c25553000902d3f", "158147492702791503418");
    ADD_CHECKPOINT2(1470000, "b419aa44016b6b1f93acecd963a27ead03f56fba8bed038e8f9704ae8fc4731b", "559264147197470555383");
    

    return true;
  }

  bool checkpoints::load_checkpoints_from_json(const std::string &json_hashfile_fullpath)
  {
    boost::system::error_code errcode;
    if (! (boost::filesystem::exists(json_hashfile_fullpath, errcode)))
    {
      LOG_PRINT_L1("Blockchain checkpoints file not found");
      return true;
    }

    LOG_PRINT_L1("Adding checkpoints from blockchain hashfile");

    uint64_t prev_max_height = get_max_height();
    LOG_PRINT_L1("Hard-coded max checkpoint height is " << prev_max_height);
    t_hash_json hashes;
    if (!epee::serialization::load_t_from_json_file(hashes, json_hashfile_fullpath))
    {
      MERROR("Error loading checkpoints from " << json_hashfile_fullpath);
      return false;
    }
    for (std::vector<t_hashline>::const_iterator it = hashes.hashlines.begin(); it != hashes.hashlines.end(); )
    {
      uint64_t height;
      height = it->height;
      if (height <= prev_max_height) {
	LOG_PRINT_L1("ignoring checkpoint height " << height);
      } else {
	std::string blockhash = it->hash;
	LOG_PRINT_L1("Adding checkpoint height " << height << ", hash=" << blockhash);
	ADD_CHECKPOINT(height, blockhash);
      }
      ++it;
    }

    return true;
  }

  bool checkpoints::load_checkpoints_from_dns(network_type nettype)
  {
    return true; // TODO: setup DNS checkpoints for Aeon
    std::vector<std::string> records;

    // All four MoneroPulse domains have DNSSEC on and valid
    static const std::vector<std::string> dns_urls = { "checkpoints.moneropulse.se"
						     , "checkpoints.moneropulse.org"
						     , "checkpoints.moneropulse.net"
						     , "checkpoints.moneropulse.co"
    };

    static const std::vector<std::string> testnet_dns_urls = { "testpoints.moneropulse.se"
							     , "testpoints.moneropulse.org"
							     , "testpoints.moneropulse.net"
							     , "testpoints.moneropulse.co"
    };

    static const std::vector<std::string> stagenet_dns_urls = { "stagenetpoints.moneropulse.se"
                   , "stagenetpoints.moneropulse.org"
                   , "stagenetpoints.moneropulse.net"
                   , "stagenetpoints.moneropulse.co"
    };

    if (!tools::dns_utils::load_txt_records_from_dns(records, nettype == TESTNET ? testnet_dns_urls : nettype == STAGENET ? stagenet_dns_urls : dns_urls))
      return true; // why true ?

    for (const auto& record : records)
    {
      auto pos = record.find(":");
      if (pos != std::string::npos)
      {
        uint64_t height;
        crypto::hash hash;

        // parse the first part as uint64_t,
        // if this fails move on to the next record
        std::stringstream ss(record.substr(0, pos));
        if (!(ss >> height))
        {
    continue;
        }

        // parse the second part as crypto::hash,
        // if this fails move on to the next record
        std::string hashStr = record.substr(pos + 1);
        if (!epee::string_tools::parse_tpod_from_hex_string(hashStr, hash))
        {
    continue;
        }

        ADD_CHECKPOINT(height, hashStr);
      }
    }
    return true;
  }

  bool checkpoints::load_new_checkpoints(const std::string &json_hashfile_fullpath, network_type nettype, bool dns)
  {
    bool result;

    result = load_checkpoints_from_json(json_hashfile_fullpath);
    if (dns)
    {
      result &= load_checkpoints_from_dns(nettype);
    }

    return result;
  }
}
