// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.
//
// Contact: iarsene@cern.ch, i.c.arsene@fys.uio.no
//
#include "Framework/AnalysisTask.h"
#include "Framework/AnalysisDataModel.h"
#include "Framework/ASoAHelpers.h"
#include "Framework/DataTypes.h"
#include "AnalysisDataModel/Multiplicity.h"
#include "AnalysisDataModel/EventSelection.h"
#include "AnalysisDataModel/Centrality.h"
#include "AnalysisCore/TriggerAliases.h"
#include "AnalysisDataModel/ReducedInfoTables.h"
#include "PWGDQCore/VarManager.h"
#include "PWGDQCore/HistogramManager.h"
#include "PWGDQCore/AnalysisCut.h"
#include "PWGDQCore/AnalysisCompositeCut.h"
#include "PWGDQCore/HistogramsLibrary.h"
#include "PWGDQCore/CutsLibrary.h"
#include "AnalysisDataModel/PID/PIDResponse.h"
#include "AnalysisDataModel/TrackSelectionTables.h"
#include <iostream>

using std::cout;
using std::endl;

using namespace o2;
using namespace o2::framework;
using namespace o2::framework::expressions;
using namespace o2::aod;

void customize(std::vector<o2::framework::ConfigParamSpec>& workflowOptions)
{
  ConfigParamSpec optionDataType{"isPbPb", VariantType::Bool, false, {"Data type"}};
  workflowOptions.push_back(optionDataType);
}

#include "Framework/runDataProcessing.h"

using MyBarrelTracks = soa::Join<aod::Tracks, aod::TracksExtra, aod::TracksCov, aod::TracksExtended, aod::TrackSelection, aod::pidRespTPC, aod::pidRespTOF, aod::pidRespTOFbeta>;
using MyEvents = soa::Join<aod::Collisions, aod::EvSels, aod::Cents>;
using MyEventsNoCent = soa::Join<aod::Collisions, aod::EvSels>;

// HACK: In order to be able to deduce which kind of aod object is transmitted to the templated VarManager::Fill functions
//         a constexpr static bit map must be defined and sent as template argument
//        The user has to include in this bit map all the tables needed in analysis, as defined in VarManager::ObjTypes
//        Additionally, one should make sure that the requested tables are actually provided in the process() function,
//       otherwise a compile time error will be thrown.
//        This is a temporary fix until the arrow/ROOT issues are solved, at which point it will be possible
//           to automatically detect the object types transmitted to the VarManager
constexpr static uint32_t gkEventFillMap = VarManager::ObjTypes::BC | VarManager::ObjTypes::Collision | VarManager::ObjTypes::CollisionCent;
constexpr static uint32_t gkEventFillMapNoCent = VarManager::ObjTypes::BC | VarManager::ObjTypes::Collision;
constexpr static uint32_t gkTrackFillMap = VarManager::ObjTypes::Track | VarManager::ObjTypes::TrackExtra | VarManager::ObjTypes::TrackDCA | VarManager::ObjTypes::TrackSelection | VarManager::ObjTypes::TrackCov | VarManager::ObjTypes::TrackPID;
constexpr static uint32_t gkMuonFillMap = VarManager::ObjTypes::Track | VarManager::ObjTypes::ReducedTrackMuon;

template <uint32_t eventFillMap, typename T>
struct TableMaker {

  using MyEvent = typename T::iterator;

  Produces<ReducedEvents> event;
  Produces<ReducedEventsExtended> eventExtended;
  Produces<ReducedEventsVtxCov> eventVtxCov;
  Produces<ReducedTracks> trackBasic;
  Produces<ReducedTracksBarrel> trackBarrel;
  Produces<ReducedTracksBarrelCov> trackBarrelCov;
  Produces<ReducedTracksBarrelPID> trackBarrelPID;
  Produces<ReducedMuons> muonBasic;
  Produces<ReducedMuonsExtended> muonExtended;

  float* fValues;

  OutputObj<THashList> fOutputList{"output"};
  HistogramManager* fHistMan;

  Configurable<std::string> fConfigEventCuts{"cfgEventCuts", "eventStandard", "Event selection"};
  Configurable<std::string> fConfigTrackCuts{"cfgBarrelTrackCuts", "jpsiPID1", "barrel track cut"};
  Configurable<std::string> fConfigMuonCuts{"cfgMuonCuts", "muonQualityCuts", "muon cut"};
  Configurable<float> fConfigBarrelTrackPtLow{"cfgBarrelLowPt", 1.0f, "Low pt cut for tracks in the barrel"};
  Configurable<float> fConfigMuonPtLow{"cfgMuonLowPt", 1.0f, "Low pt cut for muons"};

  // TODO: Enable multiple event and track selections in parallel, and decisions stored in the respective bit fields
  AnalysisCompositeCut* fEventCut;
  AnalysisCompositeCut* fTrackCut;
  AnalysisCompositeCut* fMuonCut;

  // TODO: filter on TPC dedx used temporarily until electron PID will be improved
  Filter barrelSelectedTracks = o2::aod::track::pt >= fConfigBarrelTrackPtLow && nabs(o2::aod::track::eta) <= 0.9f && o2::aod::track::tpcSignal >= 70.0f && o2::aod::track::tpcSignal <= 100.0f && o2::aod::track::tpcChi2NCl < 4.0f && o2::aod::track::itsChi2NCl < 36.0f;
  // TODO: some of the muon variables which could be used in the filter expression are currently DYNAMIC columns (e.g. eta)
  //       Add more basic muon cuts
  // TODO: Use Partition to avoid the cross-talk between filters which use variables with the same name (e.g. pt for both barrel and muon tracks)
  //       Replace by Filter when the bug is fixed
  Partition<o2::aod::Muons> selectedMuons = o2::aod::muon::pt >= fConfigMuonPtLow;

  void init(o2::framework::InitContext&)
  {
    fValues = new float[VarManager::kNVars];
    VarManager::SetDefaultVarNames();
    fHistMan = new HistogramManager("analysisHistos", "aa", VarManager::kNVars);
    fHistMan->SetUseDefaultVariableNames(kTRUE);
    fHistMan->SetDefaultVarNames(VarManager::fgVariableNames, VarManager::fgVariableUnits);

    DefineHistograms("Event_BeforeCuts;Event_AfterCuts;TrackBarrel_BeforeCuts;TrackBarrel_AfterCuts;Muons_BeforeCuts;Muons_AfterCuts;"); // define all histograms
    VarManager::SetUseVars(fHistMan->GetUsedVars());                                                   // provide the list of required variables so that VarManager knows what to fill
    fOutputList.setObject(fHistMan->GetMainHistogramList());
    DefineCuts();
  }

  void DefineCuts()
  {
    fEventCut = new AnalysisCompositeCut(true);
    TString eventCutStr = fConfigEventCuts.value;
    fEventCut->AddCut(dqcuts::GetAnalysisCut(eventCutStr.Data()));

    // NOTE: for now, the model of this task is that just one track cut is applied; multiple parallel cuts should be enabled in the future
    fTrackCut = new AnalysisCompositeCut(true);
    TString trackCutStr = fConfigTrackCuts.value;
    fTrackCut->AddCut(dqcuts::GetCompositeCut(trackCutStr.Data()));

    fMuonCut = new AnalysisCompositeCut(true);
    TString muonCutStr = fConfigMuonCuts.value;
    fMuonCut->AddCut(dqcuts::GetCompositeCut(muonCutStr.Data()));
    VarManager::SetUseVars(AnalysisCut::fgUsedVars); // provide the list of required variables so that VarManager knows what to fill
  }

  void process(MyEvent const& collision, aod::Muons const& tracksMuon, aod::BCs const& bcs, soa::Filtered<MyBarrelTracks> const& tracksBarrel)
  {
    uint64_t tag = 0;
    uint32_t triggerAliases = 0;
    for (int i = 0; i < kNaliases; i++) {
      if (collision.alias()[i] > 0) {
        triggerAliases |= (uint32_t(1) << i);
      }
    }

    VarManager::ResetValues(0, VarManager::kNEventWiseVariables, fValues);
    VarManager::FillEvent<eventFillMap>(collision, fValues); // extract event information and place it in the fgValues array
    fHistMan->FillHistClass("Event_BeforeCuts", fValues);    // automatically fill all the histograms in the class Event

    if (!fEventCut->IsSelected(fValues)) {
      return;
    }

    fHistMan->FillHistClass("Event_AfterCuts", fValues);

    event(tag, collision.bc().runNumber(), collision.posX(), collision.posY(), collision.posZ(), collision.numContrib());
    eventExtended(collision.bc().globalBC(), collision.bc().triggerMask(), triggerAliases, fValues[VarManager::kCentVZERO]);
    eventVtxCov(collision.covXX(), collision.covXY(), collision.covXZ(), collision.covYY(), collision.covYZ(), collision.covZZ(), collision.chi2());

    uint64_t trackFilteringTag = 0;
    trackBasic.reserve(tracksBarrel.size());
    trackBarrel.reserve(tracksBarrel.size());
    trackBarrelCov.reserve(tracksBarrel.size());
    trackBarrelPID.reserve(tracksBarrel.size());

    for (auto& track : tracksBarrel) {
      VarManager::FillTrack<gkTrackFillMap>(track, fValues);
      fHistMan->FillHistClass("TrackBarrel_BeforeCuts", fValues);
      if (!fTrackCut->IsSelected(fValues)) {
        continue;
      }
      fHistMan->FillHistClass("TrackBarrel_AfterCuts", fValues);

      if (track.isGlobalTrack()) {
        trackFilteringTag |= (uint64_t(1) << 0);
      }
      if (track.isGlobalTrackSDD()) {
        trackFilteringTag |= (uint64_t(1) << 1);
      }
      trackBasic(event.lastIndex(), track.globalIndex(), trackFilteringTag, track.pt(), track.eta(), track.phi(), track.sign());
      trackBarrel(track.tpcInnerParam(), track.flags(), track.itsClusterMap(), track.itsChi2NCl(),
                  track.tpcNClsFindable(), track.tpcNClsFindableMinusFound(), track.tpcNClsFindableMinusCrossedRows(),
                  track.tpcNClsShared(), track.tpcChi2NCl(),
                  track.trdChi2(), track.tofChi2(),
                  track.length(), track.dcaXY(), track.dcaZ());
      trackBarrelCov(track.cYY(), track.cZZ(), track.cSnpSnp(), track.cTglTgl(), track.c1Pt21Pt2());
      trackBarrelPID(track.tpcSignal(),
                     track.tpcNSigmaEl(), track.tpcNSigmaMu(),
                     track.tpcNSigmaPi(), track.tpcNSigmaKa(), track.tpcNSigmaPr(),
                     track.tpcNSigmaDe(), track.tpcNSigmaTr(), track.tpcNSigmaHe(), track.tpcNSigmaAl(),
                     track.tofSignal(), track.beta(),
                     track.tofNSigmaEl(), track.tofNSigmaMu(),
                     track.tofNSigmaPi(), track.tofNSigmaKa(), track.tofNSigmaPr(),
                     track.tofNSigmaDe(), track.tofNSigmaTr(), track.tofNSigmaHe(), track.tofNSigmaAl(),
                     track.trdSignal());
    }

    muonBasic.reserve(selectedMuons.size());
    muonExtended.reserve(selectedMuons.size());
    for (auto& muon : selectedMuons) {
      // TODO: add proper information for muon tracks
      if (muon.bcId() != collision.bcId()) {
        continue;
      }
      VarManager::FillTrack<gkMuonFillMap>(muon, fValues);
      fHistMan->FillHistClass("Muons_BeforeCuts", fValues);
      // TODO: the trackFilteringTag will not be needed to encode whether the track is a muon since there is a dedicated table for muons
      trackFilteringTag |= (uint64_t(1) << 0); // this is a MUON arm track  TODO: to be taken out
      if (!fMuonCut->IsSelected(fValues)) {
        continue;
      }
      fHistMan->FillHistClass("Muons_AfterCuts", fValues);

      muonBasic(event.lastIndex(), trackFilteringTag, muon.pt(), muon.eta(), muon.phi(), muon.sign());
      muonExtended(muon.inverseBendingMomentum(), muon.thetaX(), muon.thetaY(), muon.zMu(), muon.bendingCoor(), muon.nonBendingCoor(), muon.chi2(), muon.chi2MatchTrigger());
    }
  }

  void DefineHistograms(TString histClasses)
  {
    std::unique_ptr<TObjArray> objArray(histClasses.Tokenize(";"));
    for (Int_t iclass = 0; iclass < objArray->GetEntries(); ++iclass) {
      TString classStr = objArray->At(iclass)->GetName();
      fHistMan->AddHistClass(classStr.Data());

      // NOTE: The level of detail for histogramming can be controlled via configurables
      if (classStr.Contains("Event")) {
        dqhistograms::DefineHistograms(fHistMan, objArray->At(iclass)->GetName(), "event", "trigger,cent");
      }

      if (classStr.Contains("Track")) {
        dqhistograms::DefineHistograms(fHistMan, objArray->At(iclass)->GetName(), "track", "tpcpid");
      }
      if (classStr.Contains("Muons")) {
        dqhistograms::DefineHistograms(fHistMan, objArray->At(iclass)->GetName(), "track", "muon");
      }
    }
  }
};

WorkflowSpec defineDataProcessing(ConfigContext const& cfgc)
{
  WorkflowSpec workflow;
  const bool isPbPb = cfgc.options().get<bool>("isPbPb");
  if (isPbPb) {
    workflow.push_back(adaptAnalysisTask<TableMaker<gkEventFillMap, MyEvents>>(cfgc, TaskName{"table-maker"}));
  } else {
    workflow.push_back(adaptAnalysisTask<TableMaker<gkEventFillMapNoCent, MyEventsNoCent>>(cfgc, TaskName{"table-maker"}));
  }

  return workflow;
}
