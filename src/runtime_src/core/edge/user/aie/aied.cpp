/**
 * Copyright (C) 2020 Xilinx, Inc
 * Author(s): Himanshu Choudhary <hchoudha@xilinx.com>
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

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sstream>
#include <iostream>

#include "aied.h"
#include "core/edge/include/zynq_ioctl.h"
#include "core/edge/user/shim.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace zynqaie {

Aied::Aied(std::shared_ptr<xrt_core::device> device): mCoreDevice(std::move(device))
{
  mPollingThread = std::thread(&Aied::pollAIE, this);
}

Aied::~Aied()
{

}

void 
Aied::pollAIE()
{
  ZYNQ::shim *drv = ZYNQ::shim::handleCheck(mCoreDevice->get_device_handle());
  xclAIECmd cmd;

  while (1) {
    /* Calling XRT interface to wait for commands */
    if (drv->xclAIEGetCmd(&cmd) != 0) {
      continue;
    }

    switch (cmd.opcode) {
    case GRAPH_STATUS: {
      boost::property_tree::ptree pt;
      boost::property_tree::ptree pt_status;

      for (auto graph : mGraphs) {
        pt.put(graph->getname(), graph->getstatus());
      }

      pt_status.add_child("graphs", pt);
      std::stringstream ss;
      boost::property_tree::json_parser::write_json(ss, pt_status);
      std::string tmp(ss.str());
      cmd.size = snprintf(cmd.info,(tmp.size() < AIE_INFO_SIZE) ? tmp.size():AIE_INFO_SIZE
                   , "%s\n", tmp.c_str());
      break;
      }
    default:
      break;
    }
    drv->xclAIEPutCmd(&cmd);
  }
}

void
Aied::registerGraph(graph_type *graph)
{
  mGraphs.push_back(graph);
}

void
Aied::deregisterGraph(graph_type *graph)
{
  mGraphs.erase(std::remove(mGraphs.begin(), mGraphs.end(), graph), mGraphs.end());
}
}
