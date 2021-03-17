/**
 * Copyright (C) 2020 Xilinx, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */


#include "device_linux.h"
#include "core/common/query_requests.h"

#include "xrt.h"
#include "zynq_dev.h"

#include <string>
#include <memory>
#include <iostream>
#include <map>
#include <boost/format.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#ifdef XRT_ENABLE_AIE
#include "core/edge/include/zynq_ioctl.h"
#include <fcntl.h>
extern "C" {
#include <xaiengine.h>
}
#ifndef __AIESIM__
#include "xaiengine/xlnx-ai-engine.h"
#include <sys/ioctl.h>
#endif
#endif

namespace {

namespace query = xrt_core::query;
using key_type = query::key_type;
xclDeviceHandle handle;
xclDeviceInfo2 deviceInfo;

static std::map<query::key_type, std::unique_ptr<query::request>> query_tbl;

static zynq_device*
get_edgedev(const xrt_core::device* device)
{
  return zynq_device::get_dev();
}

struct bdf 
{
  using result_type = query::pcie_bdf::result_type;

  static result_type
  get(const xrt_core::device* device, key_type)
  {
    return std::make_tuple(0,0,0);
  }

};

struct board_name 
{
  using result_type = query::board_name::result_type;

  static result_type
  get(const xrt_core::device* device, key_type)
  {
    result_type deviceName("edge");
    std::ifstream VBNV("/etc/xocl.txt");
    if (VBNV.is_open()) {
      VBNV >> deviceName;
    }
    VBNV.close();
    return deviceName;
  }
};

struct is_ready 
{
  using result_type = query::is_ready::result_type;

  static result_type
  get(const xrt_core::device* device, key_type)
  {
    return true;
  }
};

static xclDeviceInfo2
init_device_info(const xrt_core::device* device)
{
  xclDeviceInfo2 dinfo;
  xclGetDeviceInfo2(device->get_user_handle(), &dinfo);
  return dinfo;
}

struct devInfo
{
  static boost::any
  get(const xrt_core::device* device,key_type key)
  {
    auto edev = get_edgedev(device);
    static std::map<const xrt_core::device*, xclDeviceInfo2> infomap;
    auto it = infomap.find(device);
    if (it == infomap.end()) {
      auto ret = infomap.emplace(device,init_device_info(device));
      it = ret.first;
    }

    auto& deviceInfo = (*it).second;
    switch (key) {
    case key_type::edge_vendor:
      return deviceInfo.mVendorId;
    case key_type::rom_vbnv:
      return std::string(deviceInfo.mName);
    case key_type::rom_ddr_bank_size_gb:
    {
      static const uint32_t BYTES_TO_GBYTES = 30;
      return (deviceInfo.mDDRSize >> BYTES_TO_GBYTES);
    }
    case key_type::rom_ddr_bank_count_max:
      return static_cast<uint64_t>(deviceInfo.mDDRBankCount);
    case key_type::clock_freqs_mhz:
    {
      std::vector<std::string> clk_freqs;
      for(int i = 0; i < sizeof(deviceInfo.mOCLFrequency)/sizeof(deviceInfo.mOCLFrequency[0]); i++)
        clk_freqs.push_back(std::to_string(deviceInfo.mOCLFrequency[i]));
      return clk_freqs;
    }
    case key_type::rom_time_since_epoch:
      return static_cast<uint64_t>(deviceInfo.mTimeStamp);
    default:
      throw query::no_such_key(key);
    }
  }
};

struct kds_cu_info
{
  using result_type = query::kds_cu_info::result_type;

  static result_type
  get(const xrt_core::device* device, key_type key)
  {
    auto edev = get_edgedev(device);

    std::vector<std::string> stats;
    std::string errmsg;
    edev->sysfs_get("kds_custat", errmsg, stats);
    if (!errmsg.empty())
      throw std::runtime_error(errmsg);

    result_type cuStats;
    for (auto& line : stats) {
        uint32_t base_address = 0;
        uint32_t usages = 0;
        uint32_t status = 0;
        sscanf(line.c_str(), "CU[@0x%x] : %d status : %d", &base_address, &usages, &status);
        cuStats.push_back(std::make_tuple(base_address, usages, status));
    }

    return cuStats;
  }
};

struct aie_reg_read
{
  using result_type = query::aie_reg_read::result_type;

  static result_type
  get(const xrt_core::device* device, key_type key, const boost::any& r, const boost::any& c, const boost::any& reg)
  {
    auto dev = get_edgedev(device);
    uint32_t val;
    auto row = boost::any_cast<int>(r);
    auto col = boost::any_cast<int>(c);
    auto v = boost::any_cast<std::string>(reg);
    
#ifdef XRT_ENABLE_AIE
#ifndef __AIESIM__
    std::map<std::string , uint32_t> regmap = {
        {"Core_R0", 0x00030000},
        {"Core_R1", 0x00030010},
        {"Core_R2", 0x00030020},
        {"Core_R3", 0x00030030},
        {"Core_R4", 0x00030040},
        {"Core_R5", 0x00030050},
        {"Core_R6", 0x00030060},
        {"Core_R7", 0x00030070},
        {"Core_R8", 0x00030080},
        {"Core_R9", 0x00030090},
        {"Core_R10", 0x000300A0},
        {"Core_R11", 0x000300B0},
        {"Core_R12", 0x000300C0},
        {"Core_R13", 0x000300D0},
        {"Core_R14", 0x000300E0},
        {"Core_R15", 0x000300F0},
        {"Core_P0", 0x00030100},
        {"Core_P1", 0x00030110},
        {"Core_P2", 0x00030120},
        {"Core_P3", 0x00030130},
        {"Core_P4", 0x00030140},
        {"Core_P5", 0x00030150},
        {"Core_P6", 0x00030160},
        {"Core_P7", 0x00030170},
        {"Core_CL0", 0x00030180},
        {"Core_CH0", 0x00030190},
        {"Core_CL1", 0x000301A0},
        {"Core_CH1", 0x000301B0},
        {"Core_CL2", 0x000301C0},
        {"Core_CH2", 0x000301D0},
        {"Core_CL3", 0x000301E0},
        {"Core_CH3", 0x000301F0},
        {"Core_CL4", 0x00030200},
        {"Core_CH4", 0x00030210},
        {"Core_CL5", 0x00030220},
        {"Core_CH5", 0x00030230},
        {"Core_CL6", 0x00030240},
        {"Core_CH6", 0x00030250},
        {"Core_CL7", 0x00030260},
        {"Core_CH7", 0x00030270},
        {"Core_PC", 0x00030280},
        {"Core_FC", 0x00030290},
        {"Core_SP", 0x000302A0},
        {"Core_LR", 0x000302B0},
        {"Core_M0", 0x000302C0},
{"Core_M1", 0x000302D0},
{"Core_M2", 0x000302E0},
{"Core_M3", 0x000302F0},
{"Core_M4", 0x00030300},
{"Core_M5", 0x00030310},
{"Core_M6", 0x00030320},
{"Core_M7", 0x00030330},
{"Core_CB0", 0x00030340},
{"Core_CB1", 0x00030350},
{"Core_CB2", 0x00030360},
{"Core_CB3", 0x00030370},
{"Core_CB4", 0x00030380},
{"Core_CB5", 0x00030390},
{"Core_CB6", 0x000303A0},
{"Core_CB7", 0x000303B0},
{"Core_CS0", 0x000303C0},
{"Core_CS1", 0x000303D0},
{"Core_CS2", 0x000303E0},
{"Core_CS3", 0x000303F0},
{"Core_CS4", 0x00030400},
{"Core_CS5", 0x00030410},
{"Core_CS6", 0x00030420},
{"Core_CS7", 0x00030430},
{"Core_MD0", 0x00030440},
{"Core_MD1", 0x00030450},
{"Core_MC0", 0x00030460},
{"Core_MC1", 0x00030470},
{"Core_S0", 0x00030480},
{"Core_S1", 0x00030490},
{"Core_S2", 0x000304A0},
{"Core_S3", 0x000304B0},
{"Core_S4", 0x000304C0},
{"Core_S5", 0x000304D0},
{"Core_S6", 0x000304E0},
{"Core_S7", 0x000304F0},
{"Core_LS", 0x00030500},
{"Core_LE", 0x00030510},
{"Core_LC", 0x00030520},
{"Performance_Ctrl0", 0x00031000},
{"Performance_Ctrl1", 0x00031004},
{"Performance_Ctrl2", 0x00031008},
{"Performance_Counter0", 0x00031020},
{"Performance_Counter1", 0x00031024},
{"Performance_Counter2", 0x00031028},
{"Performance_Counter3", 0x0003102C},
{"Performance_Counter0_Event_Value", 0x00031080},
{"Performance_Counter1_Event_Value", 0x00031084},
{"Performance_Counter2_Event_Value", 0x00031088},
{"Performance_Counter3_Event_Value", 0x0003108C},
{"Core_Control", 0x00032000},
{"Core_Status", 0x00032004},
{"Enable_Events", 0x00032008},
{"Reset_Event", 0x0003200C},
{"Debug_Control0", 0x00032010},
{"Debug_Control1", 0x00032014},
{"Debug_Control2", 0x00032018},
{"Debug_Status", 0x0003201C},
{"PC_Event0", 0x00032020},
{"PC_Event1", 0x00032024},
{"PC_Event2", 0x00032028},
{"PC_Event3", 0x0003202C},
{"Error_Halt_Control", 0x00032030},
{"Error_Halt_Event", 0x00032034},
{"ECC_Control", 0x00032100},
{"ECC_Scrubbing_Event", 0x00032110 },
{"ECC_Failing_Address", 0x00032120},
{"ECC_Instruction_Word_0", 0x00032130},
{"ECC_Instruction_Word_1", 0x00032134 },
{"ECC_Instruction_Word_2", 0x00032138 },
{"ECC_Instruction_Word_3", 0x0003213C },
{"Timer_Control", 0x00034000},
{"Event_Generate", 0x00034008 },
{"Event_Broadcast0", 0x00034010 },
{"Event_Broadcast1", 0x00034014 },
{"Event_Broadcast2", 0x00034018 },
{"Event_Broadcast3", 0x0003401C },
{"Event_Broadcast4", 0x00034020 },
{"Event_Broadcast5", 0x00034024 },
{"Event_Broadcast6", 0x00034028 },
{"Event_Broadcast7", 0x0003402C },
{"Event_Broadcast8", 0x00034030 },
{"Event_Broadcast9", 0x00034034 },
{"Event_Broadcast10", 0x00034038 },
{"Event_Broadcast11", 0x0003403C },
{"Event_Broadcast12", 0x00034040 },
{"Event_Broadcast13", 0x00034044 },
{"Event_Broadcast14", 0x00034048 },
{"Event_Broadcast15", 0x0003404C},
{"Event_Broadcast_Block_South_Set", 0x00034050},
{"Event_Broadcast_Block_South_Clr", 0x00034054},
{"Event_Broadcast_Block_South_Value", 0x00034058},
{"Event_Broadcast_Block_West_Set", 0x00034060},
{"Event_Broadcast_Block_West_Clr", 0x00034064 },
{"Event_Broadcast_Block_West_Value", 0x00034068},
{"Event_Broadcast_Block_North_Set", 0x00034070 },
{"Event_Broadcast_Block_North_Clr", 0x00034074 },
{"Event_Broadcast_Block_North_Value", 0x00034078},
{"Event_Broadcast_Block_East_Set", 0x00034080 },
{"Event_Broadcast_Block_East_Clr", 0x00034084 },
{"Event_Broadcast_Block_East_Value", 0x00034088},
{"Trace_Control0", 0x000340D0},
{"Trace_Control1", 0x000340D4},
{"Trace_Status", 0x000340D8},
{"Trace_Event0", 0x000340E0},
{"Trace_Event1", 0x000340E4},
{"Timer_Trig_Event_Low_Value", 0x000340F0},
{"Timer_Trig_Event_High_Value", 0x000340F4},
{"Timer_Low", 0x000340F8},
{"Timer_High", 0x000340FC },
{"Event_Status0", 0x00034200 },
{"Event_Status1", 0x00034204 },
{"Event_Status2", 0x00034208 },
{"Event_Status3", 0x0003420C },
{"Combo_event_inputs", 0x00034400 },
{"Combo_event_control", 0x00034404 },
{"Event_Group_0_Enable", 0x00034500 },
{"Event_Group_PC_Enable", 0x00034504 },
{"Event_Group_Core_Stall_Enable", 0x00034508},
{"Event_Group_Core_Program_Flow_Enable", 0x0003450C},
{"Event_Group_Errors0_Enable", 0x00034510},
{"Event_Group_Errors1_Enable", 0x00034514},
{"Event_Group_Stream_Switch_Enable", 0x00034518 },
{"Event_Group_Broadcast_Enable", 0x0003451C },
{"Event_Group_User_Event_Enable", 0x00034520},
{"Tile_Control", 0x00036030},
{"Tile_Control_Packet_Handler_Status", 0x00036034},
{"Tile_Clock_Control", 0x00036040},
{"CSSD_Trigger", 0x00036044},
{"Spare_Reg", 0x00036050},
{"Stream_Switch_Master_Config_ME_Core0", 0x0003F000},
{"Stream_Switch_Master_Config_ME_Core1", 0x0003F004 },
{"Stream_Switch_Master_Config_DMA0", 0x0003F008},
{"Stream_Switch_Master_Config_DMA1", 0x0003F00C },
{"Stream_Switch_Master_Config_Tile_Ctrl", 0x0003F010},
{"Stream_Switch_Master_Config_FIFO0", 0x0003F014},
{"Stream_Switch_Master_Config_FIFO1", 0x0003F018},
{"Stream_Switch_Master_Config_South0", 0x0003F01C},
{"Stream_Switch_Master_Config_South1", 0x0003F020},
{"Stream_Switch_Master_Config_South2", 0x0003F024},
{"Stream_Switch_Master_Config_South3", 0x0003F028},
{"Stream_Switch_Master_Config_West0", 0x0003F02C},
{"Stream_Switch_Master_Config_West1", 0x0003F030},
{"Stream_Switch_Master_Config_West2", 0x0003F034},
{"Stream_Switch_Master_Config_West3", 0x0003F038},
{"Stream_Switch_Master_Config_North0", 0x0003F03C},
{"Stream_Switch_Master_Config_North1", 0x0003F040},
{"Stream_Switch_Master_Config_North2", 0x0003F044},
{"Stream_Switch_Master_Config_North3", 0x0003F048},
{"Stream_Switch_Master_Config_North4", 0x0003F04C},
{"Stream_Switch_Master_Config_North5", 0x0003F050},
{"Stream_Switch_Master_Config_East0", 0x0003F054},
{"Stream_Switch_Master_Config_East1", 0x0003F058},
{"Stream_Switch_Master_Config_East2", 0x0003F05C},
{"Stream_Switch_Master_Config_East3", 0x0003F060},
{"Stream_Switch_Slave_ME_Core0_Config", 0x0003F100},
{"Stream_Switch_Slave_ME_Core1_Config", 0x0003F104},
{"Stream_Switch_Slave_DMA_0_Config", 0x0003F108},
{"Stream_Switch_Slave_DMA_1_Config", 0x0003F10C},
{"Stream_Switch_Slave_Tile_Ctrl_Config", 0x0003F110},
{"Stream_Switch_Slave_FIFO_0_Config", 0x0003F114},
{"Stream_Switch_Slave_FIFO_1_Config", 0x0003F118},
{"Stream_Switch_Slave_South_0_Config", 0x0003F11C},
{"Stream_Switch_Slave_South_1_Config", 0x0003F120},
{"Stream_Switch_Slave_South_2_Config", 0x0003F124},
{"Stream_Switch_Slave_South_3_Config", 0x0003F128},
{"Stream_Switch_Slave_South_4_Config", 0x0003F12C},
{"Stream_Switch_Slave_South_5_Config", 0x0003F130},
{"Stream_Switch_Slave_West_0_Config", 0x0003F134},
{"Stream_Switch_Slave_West_1_Config", 0x0003F138},
{"Stream_Switch_Slave_West_2_Config", 0x0003F13C},
{"Stream_Switch_Slave_West_3_Config", 0x0003F140},
{"Stream_Switch_Slave_North_0_Config", 0x0003F144},
{"Stream_Switch_Slave_North_1_Config", 0x0003F148},
{"Stream_Switch_Slave_North_2_Config", 0x0003F14C},
{"Stream_Switch_Slave_North_3_Config", 0x0003F150},
{"Stream_Switch_Slave_East_0_Config", 0x0003F154},
{"Stream_Switch_Slave_East_1_Config", 0x0003F158},
{"Stream_Switch_Slave_East_2_Config", 0x0003F15C},
{"Stream_Switch_Slave_East_3_Config", 0x0003F160},
{"Stream_Switch_Slave_ME_Trace_Config", 0x0003F164},
{"Stream_Switch_Slave_Mem_Trace_Config", 0x0003F168},
{"Stream_Switch_Slave_ME_Core0_Slot0", 0x0003F200},
{"Stream_Switch_Slave_ME_Core0_Slot1", 0x0003F204},
{"Stream_Switch_Slave_ME_Core0_Slot2", 0x0003F208},
{"Stream_Switch_Slave_ME_Core0_Slot3", 0x0003F20C},
{"Stream_Switch_Slave_ME_Core1_Slot0", 0x0003F210},
{"Stream_Switch_Slave_ME_Core1_Slot1", 0x0003F214},
{"Stream_Switch_Slave_ME_Core1_Slot2", 0x0003F218},
{"Stream_Switch_Slave_ME_Core1_Slot3", 0x0003F21C},
{"Stream_Switch_Slave_DMA_0_Slot0", 0x0003F220},
{"Stream_Switch_Slave_DMA_0_Slot1", 0x0003F224},
{"Stream_Switch_Slave_DMA_0_Slot2", 0x0003F228},
{"Stream_Switch_Slave_DMA_0_Slot3", 0x0003F22C},
{"Stream_Switch_Slave_DMA_1_Slot0", 0x0003F230},
{"Stream_Switch_Slave_DMA_1_Slot1", 0x0003F234},
{"Stream_Switch_Slave_DMA_1_Slot2", 0x0003F238},
{"Stream_Switch_Slave_DMA_1_Slot3", 0x0003F23C},
{"Stream_Switch_Slave_Tile_Ctrl_Slot0", 0x0003F240},
{"Stream_Switch_Slave_Tile_Ctrl_Slot1", 0x0003F244},
{"Stream_Switch_Slave_Tile_Ctrl_Slot2", 0x0003F248},
{"Stream_Switch_Slave_Tile_Ctrl_Slot3", 0x0003F24C},
{"Stream_Switch_Slave_FIFO_0_Slot0", 0x0003F250},
{"Stream_Switch_Slave_FIFO_0_Slot1", 0x0003F254},
{"Stream_Switch_Slave_FIFO_0_Slot2", 0x0003F258},
{"Stream_Switch_Slave_FIFO_0_Slot3", 0x0003F25C},
{"Stream_Switch_Slave_FIFO_1_Slot0", 0x0003F260},
{"Stream_Switch_Slave_FIFO_1_Slot1", 0x0003F264},
{"Stream_Switch_Slave_FIFO_1_Slot2", 0x0003F268},
{"Stream_Switch_Slave_FIFO_1_Slot3", 0x0003F26C},
{"Stream_Switch_Slave_South_0_Slot0", 0x0003F270},
{"Stream_Switch_Slave_South_0_Slot1", 0x0003F274},
{"Stream_Switch_Slave_South_0_Slot2", 0x0003F278},
{"Stream_Switch_Slave_South_0_Slot3", 0x0003F27C},
{"Stream_Switch_Slave_South_1_Slot0", 0x0003F280},
{"Stream_Switch_Slave_South_1_Slot1", 0x0003F284},
{"Stream_Switch_Slave_South_1_Slot2", 0x0003F288},
{"Stream_Switch_Slave_South_1_Slot3", 0x0003F28C},
{"Stream_Switch_Slave_South_2_Slot0", 0x0003F290},
{"Stream_Switch_Slave_South_2_Slot1", 0x0003F294},
{"Stream_Switch_Slave_South_2_Slot2", 0x0003F298},
{"Stream_Switch_Slave_South_2_Slot3", 0x0003F29C},
{"Stream_Switch_Slave_South_3_Slot0", 0x0003F2A0},
{"Stream_Switch_Slave_South_3_Slot1", 0x0003F2A4},
{"Stream_Switch_Slave_South_3_Slot2", 0x0003F2A8},
{"Stream_Switch_Slave_South_3_Slot3", 0x0003F2AC},
{"Stream_Switch_Slave_South_4_Slot0", 0x0003F2B0},
{"Stream_Switch_Slave_South_4_Slot1", 0x0003F2B4},
{"Stream_Switch_Slave_South_4_Slot2", 0x0003F2B8},
{"Stream_Switch_Slave_South_4_Slot3", 0x0003F2BC},
{"Stream_Switch_Slave_South_5_Slot0", 0x0003F2C0},
{"Stream_Switch_Slave_South_5_Slot1", 0x0003F2C4},
{"Stream_Switch_Slave_South_5_Slot2", 0x0003F2C8},
{"Stream_Switch_Slave_South_5_Slot3", 0x0003F2CC},
{"Stream_Switch_Slave_West_0_Slot0", 0x0003F2D0},
{"Stream_Switch_Slave_West_0_Slot1", 0x0003F2D4},
{"Stream_Switch_Slave_West_0_Slot2", 0x0003F2D8},
{"Stream_Switch_Slave_West_0_Slot3", 0x0003F2DC},
{"Stream_Switch_Slave_West_1_Slot0", 0x0003F2E0},
{"Stream_Switch_Slave_West_1_Slot1", 0x0003F2E4},
{"Stream_Switch_Slave_West_1_Slot2", 0x0003F2E8},
{"Stream_Switch_Slave_West_1_Slot3", 0x0003F2EC},
{"Stream_Switch_Slave_West_2_Slot0", 0x0003F2F0},
{"Stream_Switch_Slave_West_2_Slot1", 0x0003F2F4},
{"Stream_Switch_Slave_West_2_Slot2", 0x0003F2F8},
{"Stream_Switch_Slave_West_2_Slot3", 0x0003F2FC},
{"Stream_Switch_Slave_West_3_Slot0", 0x0003F300},
{"Stream_Switch_Slave_West_3_Slot1", 0x0003F304},
{"Stream_Switch_Slave_West_3_Slot2", 0x0003F308},
{"Stream_Switch_Slave_West_3_Slot3", 0x0003F30C},
{"Stream_Switch_Slave_North_0_Slot0", 0x0003F310},
{"Stream_Switch_Slave_North_0_Slot1", 0x0003F314},
{"Stream_Switch_Slave_North_0_Slot2", 0x0003F318},
{"Stream_Switch_Slave_North_0_Slot3", 0x0003F31C},
{"Stream_Switch_Slave_North_1_Slot0", 0x0003F320},
{"Stream_Switch_Slave_North_1_Slot1", 0x0003F324},
{"Stream_Switch_Slave_North_1_Slot2", 0x0003F328},
{"Stream_Switch_Slave_North_1_Slot3", 0x0003F32C},
{"Stream_Switch_Slave_North_2_Slot0", 0x0003F330},
{"Stream_Switch_Slave_North_2_Slot1", 0x0003F334},
{"Stream_Switch_Slave_North_2_Slot2", 0x0003F338},
{"Stream_Switch_Slave_North_2_Slot3", 0x0003F33C},
{"Stream_Switch_Slave_North_3_Slot0", 0x0003F340},
{"Stream_Switch_Slave_North_3_Slot1", 0x0003F344},
{"Stream_Switch_Slave_North_3_Slot2", 0x0003F348},
{"Stream_Switch_Slave_North_3_Slot3", 0x0003F34C},
{"Stream_Switch_Slave_East_0_Slot0", 0x0003F350},
{"Stream_Switch_Slave_East_0_Slot1", 0x0003F354},
{"Stream_Switch_Slave_East_0_Slot2", 0x0003F358},
{"Stream_Switch_Slave_East_0_Slot3", 0x0003F35C},
{"Stream_Switch_Slave_East_1_Slot0", 0x0003F360},
{"Stream_Switch_Slave_East_1_Slot1", 0x0003F364},
{"Stream_Switch_Slave_East_1_Slot2", 0x0003F368},
{"Stream_Switch_Slave_East_1_Slot3", 0x0003F36C},
{"Stream_Switch_Slave_East_2_Slot0", 0x0003F370},
{"Stream_Switch_Slave_East_2_Slot1", 0x0003F374},
{"Stream_Switch_Slave_East_2_Slot2", 0x0003F378},
{"Stream_Switch_Slave_East_2_Slot3", 0x0003F37C},
{"Stream_Switch_Slave_East_3_Slot0", 0x0003F380},
{"Stream_Switch_Slave_East_3_Slot1", 0x0003F384},
{"Stream_Switch_Slave_East_3_Slot2", 0x0003F388},
{"Stream_Switch_Slave_East_3_Slot3", 0x0003F38C},
{"Stream_Switch_Slave_ME_Trace_Slot0", 0x0003F390},
{"Stream_Switch_Slave_ME_Trace_Slot1", 0x0003F394},
{"Stream_Switch_Slave_ME_Trace_Slot2", 0x0003F398},
{"Stream_Switch_Slave_ME_Trace_Slot3", 0x0003F39C},
{"Stream_Switch_Slave_Mem_Trace_Slot0", 0x0003F3A0},
{"Stream_Switch_Slave_Mem_Trace_Slot1", 0x0003F3A4},
{"Stream_Switch_Slave_Mem_Trace_Slot2", 0x0003F3A8},
{"Stream_Switch_Slave_Mem_Trace_Slot3", 0x0003F3AC},
{"Stream_Switch_Event_Port_Selection_0", 0x0003FF00},
{"Stream_Switch_Event_Port_Selection_1", 0x0003FF04} };
    

  std::string err;
  std::string value;
  dev->sysfs_get("aie_metadata", err, value);
  if (!err.empty())
    throw xrt_core::error(-EINVAL, err);
  std::stringstream ss(value);
  boost::property_tree::ptree pt;
  boost::property_tree::read_json(ss, pt);
   
  int mKernelFD = open("/dev/dri/renderD128", O_RDWR);
  if (!mKernelFD) {
    throw xrt_core::error(-EINVAL, " Cannot open /dev/dri/renderD128");
  }
  XAie_DevInst* devInst;         // AIE Device Instance

  XAie_SetupConfig(ConfigPtr,
    pt.get<uint8_t>("aie_metadata.driver_config.hw_gen"),
    pt.get<uint64_t>("aie_metadata.driver_config.base_address"),
    pt.get<uint8_t>("aie_metadata.driver_config.column_shift"),
    pt.get<uint8_t>("aie_metadata.driver_config.row_shift"),
    pt.get<uint8_t>("aie_metadata.driver_config.num_columns"),
    pt.get<uint8_t>("aie_metadata.driver_config.num_rows"),
    pt.get<uint8_t>("aie_metadata.driver_config.shim_row"),
    pt.get<uint8_t>("aie_metadata.driver_config.reserved_row_start"),
    pt.get<uint8_t>("aie_metadata.driver_config.reserved_num_rows"),
    pt.get<uint8_t>("aie_metadata.driver_config.aie_tile_row_start"),
    pt.get<uint8_t>("aie_metadata.driver_config.aie_tile_num_rows"));

  /* TODO get partition id and uid from XCLBIN or PDI, currently not supported*/
  uint32_t partition_id = 1;
  uint32_t uid = 0;
  drm_zocl_aie_fd aiefd = { partition_id, uid, 0 };
  int ret = ioctl(mKernelFD, DRM_IOCTL_ZOCL_AIE_FD, &aiefd) ? -errno : 0;
  if (ret)
    throw xrt_core::error(ret, "Create AIE failed. Can not get AIE fd Please load the xclbin");
  int fd = aiefd.fd;

  ConfigPtr.PartProp.Handle = fd;

  AieRC rc;
  XAie_InstDeclare(DevInst, &ConfigPtr);
  if ((rc = XAie_CfgInitialize(&DevInst, &ConfigPtr)) != XAIE_OK)
    throw xrt_core::error(-EINVAL, "Failed to initialize AIE configuration: " + std::to_string(rc));
  devInst = &DevInst;
  if(!devInst)
    throw xrt_core::error(-EINVAL,"invalid aie object");

  row+=1;
  // TODO: get max row and column form aie_metadata
  if(row <= 0 && row >= 9 && col < 0 && col >= 50)
    throw xrt_core::error(-EINVAL,"invalid row or column");
  auto it = regmap.find(v);
  if (it == regmap.end())
    throw xrt_core::error(-EINVAL,"invalid register");

  if(XAie_Read32(devInst,it->second + _XAie_GetTileAddr(devInst,row,col),&val))
    throw xrt_core::error(-EINVAL,"error reading register");
    
#endif
#endif
    return val;
  }
};

// Specialize for other value types.
template <typename ValueType>
struct sysfs_fcn
{
  static ValueType
  get(zynq_device* dev, const char* entry)
  {
    std::string err;
    ValueType value;
    dev->sysfs_get(entry, err, value, static_cast<ValueType>(-1));
    if (!err.empty())
      throw std::runtime_error(err);
    return value;
  }
};

template <>
struct sysfs_fcn<std::string>
{
  static std::string
  get(zynq_device* dev, const char* entry)
  {
    std::string err;
    std::string value;
    dev->sysfs_get(entry, err, value);
    if (!err.empty())
      throw std::runtime_error(err);
    return value;
  }
};

template <typename VectorValueType>
struct sysfs_fcn<std::vector<VectorValueType>>
{
  //using ValueType = std::vector<std::string>;
  using ValueType = std::vector<VectorValueType>;

  static ValueType
  get(zynq_device* dev, const char* entry)
  {
    std::string err;
    ValueType value;
    dev->sysfs_get(entry, err, value);
    if (!err.empty())
      throw std::runtime_error(err);
    return value;
  }
};

template <typename QueryRequestType>
struct sysfs_get : QueryRequestType
{
  const char* entry;

  sysfs_get(const char* e)
    : entry(e)
  {}

  boost::any
  get(const xrt_core::device* device) const
  {
    return sysfs_fcn<typename QueryRequestType::result_type>
      ::get(get_edgedev(device), entry);
  }
};

template <typename QueryRequestType, typename Getter>
struct function0_get : QueryRequestType
{
  boost::any
  get(const xrt_core::device* device) const
  {
    auto k = QueryRequestType::key;
    return Getter::get(device, k);
  }
};

template <typename QueryRequestType, typename Getter>
struct function3_get : QueryRequestType
{
  boost::any
  get(const xrt_core::device* device, const boost::any& row, const boost::any& col, const boost::any& v) const
  {
    auto k = QueryRequestType::key;
    return Getter::get(device, k, row, col, v);
  }
};

template <typename QueryRequestType>
static void
emplace_sysfs_get(const char* entry)
{
  auto x = QueryRequestType::key;
  query_tbl.emplace(x, std::make_unique<sysfs_get<QueryRequestType>>(entry));
}

template <typename QueryRequestType, typename Getter>
static void
emplace_func0_request()
{
  auto k = QueryRequestType::key;
  query_tbl.emplace(k, std::make_unique<function0_get<QueryRequestType, Getter>>());
}

template <typename QueryRequestType, typename Getter>
static void
emplace_func3_request()
{
  auto k = QueryRequestType::key;
  query_tbl.emplace(k, std::make_unique<function3_get<QueryRequestType, Getter>>());
}

static void
initialize_query_table()
{
  emplace_func0_request<query::edge_vendor,             devInfo>();

  emplace_func0_request<query::rom_vbnv,                devInfo>();
  emplace_func0_request<query::rom_fpga_name,           devInfo>();
  emplace_func0_request<query::rom_ddr_bank_size_gb,    devInfo>();
  emplace_func0_request<query::rom_ddr_bank_count_max,  devInfo>();
  emplace_func0_request<query::rom_time_since_epoch,    devInfo>();

  emplace_func0_request<query::clock_freqs_mhz,         devInfo>();
  emplace_func0_request<query::kds_cu_info,             kds_cu_info>();
  emplace_func3_request<query::aie_reg_read,            aie_reg_read>();
 
  emplace_sysfs_get<query::xclbin_uuid>               ("xclbinid");
  emplace_sysfs_get<query::mem_topology_raw>          ("mem_topology");
  emplace_sysfs_get<query::ip_layout_raw>             ("ip_layout");
  emplace_sysfs_get<query::aie_metadata>              ("aie_metadata");
  emplace_sysfs_get<query::graph_status>              ("graph_status");
  emplace_sysfs_get<query::memstat>                   ("memstat");
  emplace_sysfs_get<query::memstat_raw>               ("memstat_raw");
  emplace_sysfs_get<query::error>                     ("errors");
  emplace_func0_request<query::pcie_bdf,                bdf>();
  emplace_func0_request<query::board_name,              board_name>();
  emplace_func0_request<query::is_ready,                is_ready>();
}

struct X { X() { initialize_query_table(); } };
static X x;

}

namespace xrt_core {

const query::request&
device_linux::
lookup_query(query::key_type query_key) const
{
  auto it = query_tbl.find(query_key);

  if (it == query_tbl.end())
    throw query::no_such_key(query_key);

  return *(it->second);
}

device_linux::
device_linux(handle_type device_handle, id_type device_id, bool user)
  : shim<device_edge>(device_handle, device_id, user)
{
}

void
device_linux::
read_dma_stats(boost::property_tree::ptree& pt) const
{
}

void
device_linux::
read(uint64_t offset, void* buf, uint64_t len) const
{

  throw error(-ENODEV, "read failed");
}

void
device_linux::
write(uint64_t offset, const void* buf, uint64_t len) const
{
  throw error(-ENODEV, "write failed");
}


void 
device_linux::
reset(query::reset_type key) const 
{
  switch(key.get_key()) {
  case query::reset_key::hot:
    throw error(-ENODEV, "Hot reset not supported");
  case query::reset_key::kernel:
    throw error(-ENODEV, "OCL dynamic region reset not supported");
  case query::reset_key::ert:
    throw error(-ENODEV, "ERT reset not supported");
  case query::reset_key::ecc:
    throw error(-ENODEV, "Soft Kernel reset not supported");
  case query::reset_key::aie:
    throw error(-ENODEV, "AIE reset not supported");
  default:
    throw error(-ENODEV, "invalid argument");
  }
}

} // xrt_core
