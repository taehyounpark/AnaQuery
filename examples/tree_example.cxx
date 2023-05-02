#include <memory>
#include <chrono>
#include <iostream>
#include <sstream>

#include <ROOT/RVec.hxx>
#include "TPad.h"
#include "TFile.h"
#include "TLorentzVector.h"
#include "TTreeReaderValue.h"
#include "TTreeReader.h"
#include "TVector2.h"
#include "TH1F.h"

#include "ana/analysis.h"
#include "ana/definition.h"
#include "ana/output.h"

#include "RAnalysis/Tree.h"
#include "RAnalysis/Histogram.h"
#include "RAnalysis/Folder.h"

using RVecF = ROOT::RVec<float>;
using RVecD = ROOT::RVec<double>;
using TLV = TLorentzVector;

class NthP4 : public ana::column::definition<TLV(ROOT::RVec<double>, ROOT::RVec<double>, ROOT::RVec<double>, ROOT::RVec<double>)>
{

public:

  NthP4(unsigned int index, double scale=1.0) : 
    ana::column::definition<TLV(ROOT::RVec<double>, ROOT::RVec<double>, ROOT::RVec<double>, ROOT::RVec<double>)>(),
    m_index(index),
    m_scale(scale)
  {}

  virtual ~NthP4() = default;

  virtual TLV evaluate(ana::observable<ROOT::RVec<double>> pt, ana::observable<ROOT::RVec<double>> eta, ana::observable<ROOT::RVec<double>> phi, ana::observable<ROOT::RVec<double>> es) const override {
    TLV p4;
    p4.SetPtEtaPhiE(pt->at(m_index)*m_scale,eta->at(m_index),phi->at(m_index),es->at(m_index)*m_scale);
    return p4;
  }

protected:

  unsigned int m_index;
  double m_scale;

};
 
int main(int argc, char* argv[]) {

  ana::multithread::enable(2);
  ana::analysis<Tree> hww;
  hww.open( {"hww.root"}, "mini" );

  auto mc_weight = hww.read<float>("mcWeight");
  auto el_sf = hww.read<float>("scaleFactor_ELE");
  auto mu_sf = hww.read<float>("scaleFactor_MUON");

  // auto lep_pt_MeV = hww.read<RVecF>("lep_pt");
  auto lep_pt_MeV = hww.read<RVecF>("lep_pt");
  auto lep_eta = hww.read<RVecF>("lep_eta");
  auto lep_phi = hww.read<RVecF>("lep_phi");
  auto lep_E_MeV = hww.read<RVecF>("lep_E");
  auto lep_Q = hww.read<RVecF>("lep_charge");
  auto lep_type = hww.read<ROOT::RVec<unsigned int>>("lep_type");
  auto met_MeV = hww.read<float>("met_et");
  auto met_phi = hww.read<float>("met_phi");

  // convert MeV -> GeV
  auto MeV = hww.constant(1000.0);
  auto lep_pt = lep_pt_MeV / MeV;  // ROOT::RVec<float> / double
  auto lep_E = lep_E_MeV / MeV;
  auto met = met_MeV / MeV;

  auto lep_eta_max = hww.constant(2.4);
  auto Escale = hww.define([](RVecD E){return E;}).vary("lp4_up",[](RVecD E){return E*1.01;}).vary("lp4_dn",[](RVecD E){return E*0.99;});

  auto lep_pt_sel = Escale(lep_pt)[ lep_eta < lep_eta_max && lep_eta > (-lep_eta_max) ];
  auto lep_E_sel = Escale(lep_E)[ lep_eta < lep_eta_max && lep_eta > (-lep_eta_max) ];
  auto lep_eta_sel = lep_eta[ lep_eta < lep_eta_max && lep_eta > (-lep_eta_max) ];
  auto lep_phi_sel = lep_phi[ lep_eta < lep_eta_max && lep_eta > (-lep_eta_max) ];
  auto nlep_sel = hww.define([](RVecD const& lep){return lep.size();})(lep_pt_sel);

  auto l1p4 = hww.define<NthP4>(0)(lep_pt_sel, lep_eta_sel, lep_phi_sel, lep_E_MeV);
  auto l2p4 = hww.define<NthP4>(1)(lep_pt_sel, lep_eta_sel, lep_phi_sel, lep_E_MeV);

  auto llp4 = hww.define([](const TLV& p4, const TLV& q4){return (p4+q4);})(l1p4,l2p4);
  auto pth = hww.define(
    [](const TLV& p3, float q, float q_phi) {
      TVector2 p2; p2.SetMagPhi(p3.Pt(), p3.Phi());
      TVector2 q2; q2.SetMagPhi(q, q_phi);
      return (p2+q2).Mod();
    })(llp4, met, met_phi);

  using cut = ana::selection::cut;
  using weight = ana::selection::weight;

  auto incl = hww.filter<weight>("incl").apply(mc_weight * el_sf * mu_sf);

  auto nlep_req = hww.constant(2);
  auto cut_2l = incl.filter<cut>("2l")(nlep_sel == nlep_req);

  auto cut_2los = cut_2l.channel<cut>("2los",  [](const RVecF& lep_charge){return lep_charge.at(0)+lep_charge.at(1)==0;})(lep_Q);
  auto cut_2ldf = cut_2los.filter<cut>("2ldf", [](const ROOT::RVec<int>& lep_type){return lep_type.at(0)+lep_type.at(1)==24;})(lep_type);
  auto cut_2lsf = cut_2los.filter<cut>("2lsf", [](const ROOT::RVec<int>& lep_type){return (lep_type.at(0)+lep_type.at(1)==22)||(lep_type.at(0)+lep_type.at(1)==26);})(lep_type);

  auto nlep_incl = hww.book<Histogram<1,float>>(std::string("pth"),5,0,5).fill(nlep_sel).at(incl);

  auto pth_hists = hww.book<Histogram<1,float>>("pth",50,0,400).fill(pth).at(cut_2lsf,cut_2ldf);

  auto get_pt = hww.define([](TLV const& p4){return p4.Pt();});
  auto l1pt = get_pt(l1p4);
  auto l2pt = get_pt(l2p4);
  auto l1n2pt_hists = hww.book<Histogram<1,float>>(std::string("l1n2pt"),50,0,200).fill(l1pt).fill(l2pt).at(cut_2los, cut_2lsf, cut_2ldf);

  std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

  // nominal

  // auto nlep_incl_hist = nlep_incl.result();  // std::shared_ptr<TH1F>

  // auto pth_2lsf_hist = pth_hists["2los/2lsf"].result();
  // auto pth_2ldf_hist = pth_hists["2los/2ldf"].result();

  // systematic

  // auto pth_nom_2lsf_hist = pth_hists.nominal()["2los/2lsf"].result();
  // auto pth_var_2ldf_hist = pth_hists["lp4_up"]["2los/2ldf"].result();

  std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
  std::cout << "Elapsed time = " << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() << " [µs]" << std::endl;

  // auto out_file = TFile::Open("hww_results.root","recreate");
  // ana::output::dump<Folder>(pth_hists, *out_file, "hww");
  // delete out_file;

  // pth_nom_2ldf_hist->SetLineColor(kBlack); pth_nom_2ldf_hist->Draw("hist");
  // pth_var_2ldf_hist->SetLineColor(kRed); pth_var_2ldf_hist->Draw("E same");
  // gPad->Print("pth_varied.pdf");

  return 0;
}