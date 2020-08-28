/**
 * \file msg.hpp
 * \author kadds (itmyxyf@gmail.com)
 * \brief all p2p message types
 * \version 0.1
 * \date 2020-03-21
 *
 * @copyright Copyright (c) 2020.
 * This file is part of P2P-Live.
 *
 * P2P-Live is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * P2P-Live is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with P2P-Live. If not, see <http: //www.gnu.org/licenses/>.
 *
 */

#pragma once
#include "../endian.hpp"
#include "../net.hpp"
namespace net::p2p
{
/// frame id
using fragment_id_t = u64;
/// session id: room id
using session_id_t = u32;
/// channel: 0: init, 1: video, 2: audio
using channel_t = u8;

} // namespace net::p2p