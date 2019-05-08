// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.
#define BOOST_TEST_MODULE Test Framework AnalysisTask
#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "Framework/AnalysisTask.h"

#include <boost/test/unit_test.hpp>

using namespace o2::framework;

class ATask : public AnalysisTask
{
 public:
  void init(InitContext& ic) final
  {
  }
  void run(ProcessingContext& pc) final
  {
  }

  void processTracks(std::shared_ptr<arrow::Table> tracks)
  {
  }
};

BOOST_AUTO_TEST_CASE(AdaptorCompilation)
{
  auto task = adaptAnalysisTask<ATask>("test");
}
