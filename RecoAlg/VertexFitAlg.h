//////////////////////////////////////////////////////////////////////
///
/// VertexFitAlg class
///
/// Bruce Baller, baller@fnal.gov
///
/// Algorithm for fitting a 3D vertex given a set of track hits
///
////////////////////////////////////////////////////////////////////////
#ifndef VERTEXFITALG_H
#define VERTEXFITALG_H

#include <math.h>
#include <algorithm>
#include <vector>

// LArSoft includes
#include "Geometry/Geometry.h"
#include "Geometry/TPCGeo.h"
#include "Geometry/PlaneGeo.h"
#include "Geometry/WireGeo.h"
#include "RecoAlg/VertexFitMinuitStruct.h"

// ROOT includes
#include "TMinuit.h"

namespace trkf {

  class VertexFitAlg {
    public:

    VertexFitAlg();
    
    virtual ~VertexFitAlg();
    
    void VertexFit(std::vector<std::vector<geo::WireID>>& hitWID,
                      std::vector<std::vector<double>>& hitX,
                      std::vector<std::vector<double>>& hitXErr,
                      TVector3& VtxPos, TVector3& VtxPosErr,
                      std::vector<TVector3>& TrkDir, std::vector<TVector3>& TrkDirErr,
                      float& ChiDOF);

    // Variables for minuit.
    static VertexFitMinuitStruct fVtxFitMinStr;
    
    static void fcnVtxPos(Int_t &, Double_t *, Double_t &fval, double *par, Int_t flag);

    private:

    art::ServiceHandle<geo::Geometry> geom;

    
  }; // class VertexFitAlg

} // namespace trkf

#endif // ifndef VERTEXFITALG_H