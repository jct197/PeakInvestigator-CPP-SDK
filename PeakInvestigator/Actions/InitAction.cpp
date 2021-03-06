// --------------------------------------------------------------------------
//  PeakInvestigator-API -- C++ library for accessing the public API of
//                              PeakInvestigator.
// --------------------------------------------------------------------------
// Copyright Veritomyx, Inc. 2016.
//
// This software is released under a three-clause BSD license:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of any author or any participating institution
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
// For a full list of authors, refer to the file AUTHORS.
// --------------------------------------------------------------------------
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL ANY OF THE AUTHORS OR THE CONTRIBUTING
// INSTITUTIONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// --------------------------------------------------------------------------
// $Maintainer: Adam Tenderholt $
// $Author: Adam Tenderholt $
// --------------------------------------------------------------------------
//
//

#include <algorithm>
#include <stdexcept>
#include <iostream>

#include <json/json.h>
#include "InitAction.h"

using namespace Veritomyx::PeakInvestigator;

const std::string InitAction::EXAMPLE_RESPONSE("{\"Action\":\"INIT\", \"Job\":\"V-504.1551\", \"ID\":504, \"Funds\":115.01, \"EstimatedCost\":[{\"Instrument\":\"TOF\", \"RTO\":\"RTO-24\", \"Cost\":27.60}, {\"Instrument\":\"Orbitrap\", \"RTO\":\"RTO-24\", \"Cost\":36.22}, {\"Instrument\":\"IonTrap\", \"RTO\":\"RTO-24\", \"Cost\":32.59}]}");

InitAction::InitAction(std::string user, std::string code, int project_id, std::string version_of_PI, int scan_count,
                       const JobAttributes& attributes, int calibration_count, std::string client_key) :
  BaseAction(user, code, "INIT")
{

  project_id_ = project_id;
  version_of_PI_ = version_of_PI;
  scan_count_ = scan_count;
  calibration_count_ = calibration_count;
  attributes_.max_points = attributes.max_points;
  attributes_.min_mass = attributes.min_mass;
  attributes_.max_mass = attributes.max_mass;
  attributes_.start_mass = attributes.start_mass;
  attributes_.end_mass = attributes.end_mass;
  client_key_ = client_key;
}

InitAction::InitAction(const InitAction& action)
{
	user_ = action.user_;
	code_ = action.code_;
	action_ = action.action_;

	Json::Value* value = new Json::Value();
	*value = *action.response_object_;
	response_object_.reset(value);

	project_id_ = action.project_id_;
	version_of_PI_ = action.version_of_PI_;
	scan_count_ = action.scan_count_;
	calibration_count_ = action.calibration_count_;
	attributes_.max_points = action.attributes_.max_points;
	attributes_.min_mass = action.attributes_.min_mass;
	attributes_.max_mass = action.attributes_.max_mass;
	attributes_.start_mass = action.attributes_.start_mass;
	attributes_.end_mass = action.attributes_.end_mass;
	client_key_ = action.client_key_;

	estimated_costs_ = action.estimated_costs_;
}

std::string InitAction::buildQuery() const
{
  std::string query = BaseAction::buildQuery();
  query.append("&ID="); query.append(std::to_string(project_id_));
  query.append("&PI_Version="); query.append(version_of_PI_);
  query.append("&ScanCount="); query.append(std::to_string(scan_count_));
  if (calibration_count_ > 0) {
    query.append("&CalibrationCount="); query.append(std::to_string(calibration_count_));
  }

  query.append("&MaxPoints="); query.append(std::to_string(attributes_.max_points));
  query.append("&MinMass="); query.append(std::to_string(attributes_.min_mass));
  query.append("&MaxMass="); query.append(std::to_string(attributes_.max_mass));
  query.append("&StartMass="); query.append(std::to_string(attributes_.start_mass));
  query.append("&EndMass="); query.append(std::to_string(attributes_.end_mass));
  if (client_key_ != "") {
    query.append("&ClientKey="); query.append(client_key_);
  }

  return query;
}

void InitAction::preCheck()
{
  if (!isReady("INIT"))
  {
    throw std::runtime_error("Failed Pre-Check");
  }
}

std::string InitAction::getJob()
{
  preCheck();
  return getStringAttribute("Job");
}

long InitAction::getProjectId()
{
  preCheck();
  return getLongAttribute("ID");
}

double InitAction::getFunds()
{
  preCheck();
  return getDoubleAttribute("Funds");
}

EstimatedCosts InitAction::getEstimatedCosts()
{
  if(estimated_costs_.size() > 0)
  {
    return estimated_costs_;
  }

  preCheck();

  Json::Value estimated_costs = getAttribute("EstimatedCost");
  for(unsigned int i = 0; i < estimated_costs.size(); i++)
  {
    Json::Value estimated_cost = estimated_costs[i];
    std::string instrument = estimated_cost.get("Instrument", Json::Value()).asString();
    std::string RTO = estimated_cost.get("RTO", Json::Value()).asString();
    double cost = estimated_cost.get("Cost", Json::Value()).asDouble();

    ResponseTimeCosts costs = estimated_costs_.forInstrument(instrument);
    costs[RTO] = cost;
    estimated_costs_[instrument] = costs;
  }

  return estimated_costs_;
}

std::string InitAction::getErrorMessage()
{
  preCheck();
  return BaseAction::getErrorMessage();
}

int InitAction::getErrorCode()
{
  preCheck();
  return BaseAction::getErrorCode();
}

std::list<std::string> ResponseTimeCosts::getRTOs() const
{
  std::list<std::string> RTOs;

  std::map<std::string, double>::const_iterator iter;
  for (iter = begin(); iter != end(); ++iter) {
    RTOs.push_back(iter->first);
  }

  return RTOs;
}

double ResponseTimeCosts::getCost(std::string responseTimeObjective) const
{
  std::map<std::string, double>::const_iterator iter = find(responseTimeObjective);
  if(iter == end())
  {
    std::string error = "The following RTO was not found: " + responseTimeObjective;
    throw std::invalid_argument(error);
  }

  return iter->second;
}

std::list<std::string> EstimatedCosts::getInstruments() const
{
  std::list<std::string> instruments;

  std::map<std::string, ResponseTimeCosts>::const_iterator iter;
  for (iter = begin(); iter != end(); ++iter)
  {
    instruments.push_back(iter->first);
  }

  return instruments;
}

double EstimatedCosts::getMaximumCost(std::string RTO) const
{
  double maxCost = 0.0;

  std::map<std::string, ResponseTimeCosts>::const_iterator iter;
  for(iter = begin(); iter != end(); ++iter)
  {
    ResponseTimeCosts costs = iter->second;
    double cost = costs.getCost(RTO);
    maxCost = std::max(maxCost, cost);
  }

  return maxCost;
}

ResponseTimeCosts EstimatedCosts::forInstrument(std::string instrument) const
{
  EstimatedCosts::const_iterator iter = find(instrument);
  if(iter == end()) {
    return ResponseTimeCosts();
  }

  return iter->second;
}
