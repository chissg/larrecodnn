////////////////////////////////////////////////////////////////////////////////////////////////////
// Class:       PointIdAlg
// Authors:     D.Stefan (Dorota.Stefan@ncbj.gov.pl),         from DUNE, CERN/NCBJ, since May 2016
//              R.Sulej (Robert.Sulej@cern.ch),               from DUNE, FNAL/NCBJ, since May 2016
//              P.Plonski,                                    from DUNE, WUT,       since May 2016
//
//
// Point Identification Algorithm
//
//      Run CNN or MLP trained to classify a point in 2D projection. Various features can be
//      recognized, depending on the net model/weights used.
//
////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef PointIdAlg_h
#define PointIdAlg_h

// Framework includes
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "fhiclcpp/types/Atom.h"
#include "fhiclcpp/types/Sequence.h"
#include "fhiclcpp/types/Table.h"
#include "canvas/Utilities/InputTag.h"

// LArSoft includes
#include "canvas/Persistency/Common/FindManyP.h" 
#include "larcorealg/Geometry/GeometryCore.h"
#include "larcore/Geometry/Geometry.h"
#include "larcorealg/Geometry/TPCGeo.h"
#include "larcorealg/Geometry/PlaneGeo.h"
#include "larcorealg/Geometry/WireGeo.h"
#include "lardataobj/RecoBase/Wire.h"
#include "lardataobj/RecoBase/Hit.h"
#include "lardataobj/RecoBase/Track.h"
#include "larreco/Calorimetry/CalorimetryAlg.h"
#include "lardata/DetectorInfoServices/DetectorPropertiesService.h"
#include "nusimdata/SimulationBase/MCParticle.h"

#include "larreco/RecoAlg/ImagePatternAlgs/MLP/NNReader.h"
#include "larreco/RecoAlg/ImagePatternAlgs/Keras/keras_model.h"

#include "CLHEP/Random/JamesRandom.h" // for testing on noise, not used by any reco

// ROOT & C++
#include <memory>
//#include <functional>

namespace img
{
    class DataProviderAlg;
}

namespace nnet
{
	class ModelInterface;
	class MlpModelInterface;
	class KerasModelInterface;
	class PointIdAlg;
	class TrainingDataAlg;
}

/// Base class providing data for training / running image based classifiers. It can be used
/// also for any other algoruithms where 2D projection image is useful. Currently the image
/// is 32-bit fp / pixel, as sson as have time will template it so e.g. byte pixels would
/// be possible.
class img::DataProviderAlg
{
public:
	enum EDownscaleMode { kMax = 1, kMaxMean = 2, kMean = 3 };

    struct Config
    {
	    using Name = fhicl::Name;
	    using Comment = fhicl::Comment;

		fhicl::Table<calo::CalorimetryAlg::Config> CalorimetryAlg {
			Name("CalorimetryAlg"),
			Comment("Used to eliminate amplitude variation due to electron lifetime.")
		};

		fhicl::Atom<bool> CalibrateAmpl {
			Name("CalibrateAmpl"),
			Comment("Calibrate ADC values with CalAmpConstants")
		};

		fhicl::Atom<unsigned int> DriftWindow {
			Name("DriftWindow"),
			Comment("Downsampling window (in drift ticks).")
		};

		fhicl::Atom<std::string> DownscaleFn {
			Name("DownscaleFn"),
			Comment("Downsampling function")
		};

		fhicl::Atom<bool> DownscaleFullView {
			Name("DownscaleFullView"),
			Comment("Downsample full view (faster / lower location precision)")
		};

		fhicl::Sequence<float> BlurKernel {
			Name("BlurKernel"),
			Comment("Blur kernel in wire direction")
		};

		fhicl::Atom<float> NoiseSigma {
			Name("NoiseSigma"),
			Comment("White noise sigma")
		};

		fhicl::Atom<float> CoherentSigma {
			Name("CoherentSigma"),
			Comment("Coherent noise sigma")
		};
    };

	DataProviderAlg(const fhicl::ParameterSet& pset) :
		DataProviderAlg(fhicl::Table<Config>(pset, {})())
	{}

    DataProviderAlg(const Config& config);

	virtual ~DataProviderAlg(void);

	bool setWireDriftData(const std::vector<recob::Wire> & wires, // once per view: setup ADC buffer, collect & downscale ADC's
		unsigned int view, unsigned int tpc, unsigned int cryo);

	std::vector<float> const & wireData(size_t widx) const { return fWireDriftData[widx]; }

    /// Return value from the ADC buffer, or zero if coordinates are out of the view;
    /// will scale the drift according to the downscale settings.
    float getPixelOrZero(int wire, int drift) const
    {
        size_t didx = getDriftIndex(drift), widx = (size_t)wire;

        if ((widx >= 0) && (widx < fWireDriftData.size()) &&
            (didx >= 0) && (didx < fNCachedDrifts))
        {
            return fWireDriftData[widx][didx];
        }
        else { return 0; }
    }

    /// Pool max value in a patch around the wire/drift pixel.
    float poolMax(int wire, int drift, size_t r = 0) const;

    /// Pool sum of pixels in a patch around the wire/drift pixel.
    float poolSum(int wire, int drift, size_t r = 0) const;

	unsigned int Cryo(void) const { return fCryo; }
	unsigned int TPC(void) const { return fTPC; }
	unsigned int View(void) const { return fView; }

	unsigned int NWires(void) const { return fNWires; }
	unsigned int NScaledDrifts(void) const { return fNScaledDrifts; }
	unsigned int NCachedDrifts(void) const { return fNCachedDrifts; }
	unsigned int DriftWindow(void) const { return fDriftWindow; }

    double LifetimeCorrection(double tick) const { return fCalorimetryAlg.LifetimeCorrection(tick); }

protected:
	unsigned int fCryo, fTPC, fView;
	unsigned int fNWires, fNDrifts, fNScaledDrifts, fNCachedDrifts;

	std::vector< raw::ChannelID_t > fWireChannels;              // wire channels (may need this connection...), InvalidChannelID if not used
	std::vector< std::vector<float> > fWireDriftData;           // 2D data for entire projection, drifts scaled down
	std::vector<float> fLifetimeCorrFactors;                    // precalculated correction factors along full drift

   	EDownscaleMode fDownscaleMode;
   	//std::function<void (std::vector<float> &, std::vector<float> const &, size_t)> fnDownscale;

   	size_t fDriftWindow;
	bool fDownscaleFullView;
	float fDriftWindowInv;

	void downscaleMax(std::vector<float> & dst, std::vector<float> const & adc, size_t tick0) const;
	void downscaleMaxMean(std::vector<float> & dst, std::vector<float> const & adc, size_t tick0) const;
	void downscaleMean(std::vector<float> & dst, std::vector<float> const & adc, size_t tick0) const;
	void downscale(std::vector<float> & dst, std::vector<float> const & adc, size_t tick0) const
	{
	    switch (fDownscaleMode)
	    {
	        case img::DataProviderAlg::kMean: downscaleMean(dst, adc, tick0); break;
	        case img::DataProviderAlg::kMaxMean: downscaleMaxMean(dst, adc, tick0); break;
	        case img::DataProviderAlg::kMax: downscaleMax(dst, adc, tick0); break;
	        default:throw cet::exception("img::DataProviderAlg") << "Downscale mode not supported." << std::endl; break;
	    }
	}

    size_t getDriftIndex(float drift) const
    {
        if (fDownscaleFullView) return (size_t)(drift * fDriftWindowInv);
        else return (size_t)drift;
    }

	bool setWireData(std::vector<float> const & adc, size_t wireIdx);

	virtual void resizeView(size_t wires, size_t drifts);

	// Calorimetry needed to equalize ADC amplitude along drift:
	calo::CalorimetryAlg  fCalorimetryAlg;

	// Geometry and detector properties:
	geo::GeometryCore const* fGeometry;
	detinfo::DetectorProperties const* fDetProp;

private:
    float scaleAdcSample(float val) const;
    std::vector<float> fAmplCalibConst;
    bool fCalibrateAmpl;

    CLHEP::HepJamesRandom fRndEngine;

    void applyBlur();
    std::vector<float> fBlurKernel; // blur not applied if empty

    void addWhiteNoise();
    float fNoiseSigma;              // noise not added if sigma=0

    void addCoherentNoise();
    float fCoherentSigma;           // noise not added if sigma=0
};
// ------------------------------------------------------
// ------------------------------------------------------
// ------------------------------------------------------

class nnet::ModelInterface
{
public:
	virtual ~ModelInterface(void) { }

	unsigned int GetInputLength(void) const { return GetInputCols() * GetInputRows(); }
	virtual unsigned int GetInputCols(void) const = 0;
	virtual unsigned int GetInputRows(void) const = 0;
	virtual int GetOutputLength(void) const = 0;

	virtual bool Run(std::vector< std::vector<float> > const & inp2d) = 0;
	virtual std::vector<float> GetAllOutputs(void) const = 0;
	virtual float GetOneOutput(int neuronIndex) const = 0;

protected:
	ModelInterface(void) { }

    std::string findFile(const char* fileName) const;
};
// ------------------------------------------------------

class nnet::MlpModelInterface : public nnet::ModelInterface
{
public:
	MlpModelInterface(const char* xmlFileName);

	virtual unsigned int GetInputRows(void) const { return m.GetInputLength(); }
	virtual unsigned int GetInputCols(void) const { return 1; }
	virtual int GetOutputLength(void) const { return m.GetOutputLength(); }

	virtual bool Run(std::vector< std::vector<float> > const & inp2d);
	virtual float GetOneOutput(int neuronIndex) const;
	virtual std::vector<float> GetAllOutputs(void) const;

private:
	nnet::NNReader m;
};
// ------------------------------------------------------

class nnet::KerasModelInterface : public nnet::ModelInterface
{
public:
	KerasModelInterface(const char* modelFileName);

	virtual unsigned int GetInputRows(void) const { return m.get_input_rows(); }
	virtual unsigned int GetInputCols(void) const { return m.get_input_cols(); }
	virtual int GetOutputLength(void) const { return m.get_output_length(); }

	virtual bool Run(std::vector< std::vector<float> > const & inp2d);
	virtual float GetOneOutput(int neuronIndex) const;
	virtual std::vector<float> GetAllOutputs(void) const;

private:
	std::vector<float> fOutput; // buffer for output values
	keras::KerasModel m; // network model
};
// ------------------------------------------------------

class nnet::PointIdAlg : public img::DataProviderAlg
{
public:

    struct Config : public img::DataProviderAlg::Config
    {
	    using Name = fhicl::Name;
	    using Comment = fhicl::Comment;

		fhicl::Atom<std::string> NNetModelFile {
			Name("NNetModelFile"),
			Comment("Neural net model to apply.")
		};

		fhicl::Atom<unsigned int> PatchSizeW {
			Name("PatchSizeW"),
			Comment("How many wires in patch.")
		};

		fhicl::Atom<unsigned int> PatchSizeD {
			Name("PatchSizeD"),
			Comment("How many downsampled ADC entries in patch")
		};
    };

	PointIdAlg(const fhicl::ParameterSet& pset) :
		PointIdAlg(fhicl::Table<Config>(pset, {})())
	{}

    PointIdAlg(const Config& config);

	virtual ~PointIdAlg(void);

	size_t NClasses(void) const;

	// calculate single-value prediction (2-class probability) for [wire, drift] point
	float predictIdValue(unsigned int wire, float drift, size_t outIdx = 0) const;

	// calculate multi-class probabilities for [wire, drift] point
	std::vector<float> predictIdVector(unsigned int wire, float drift) const;

	static std::vector<float> flattenData2D(std::vector< std::vector<float> > const & patch);

	std::vector< std::vector<float> > const & patchData2D(void) const { return fWireDriftPatch; }
	std::vector<float> patchData1D(void) const { return flattenData2D(fWireDriftPatch); }  // flat vector made of the patch data, wire after wire

    bool isInsideFiducialRegion(unsigned int wire, float drift) const;

private:
	std::string fNNetModelFilePath;
	nnet::ModelInterface* fNNet;

	mutable std::vector< std::vector<float> > fWireDriftPatch;  // patch data around the identified point
	size_t fPatchSizeW, fPatchSizeD;

	mutable size_t fCurrentWireIdx, fCurrentScaledDrift;
	bool patchFromDownsampledView(size_t wire, float drift) const;
	bool patchFromOriginalView(size_t wire, float drift) const;
	bool bufferPatch(size_t wire, float drift) const
    {
        if (fDownscaleFullView) { return patchFromDownsampledView(wire, drift); }
        else { return patchFromOriginalView(wire, drift); }
    }
	void resizePatch(void);

	void deleteNNet(void) { if (fNNet) delete fNNet; fNNet = 0; }
};
// ------------------------------------------------------
// ------------------------------------------------------
// ------------------------------------------------------

class nnet::TrainingDataAlg : public img::DataProviderAlg
{
public:

    enum EMask
    {
        kNone     = 0,
        kPdgMask  = 0x00000FFF, // pdg code mask
        kTypeMask = 0x0000F000, // track type mask
        kVtxMask  = 0xFFFF0000  // vertex flags
    };

    enum ETrkType
    {
        kDelta  = 0x1000,      // delta electron
        kMichel = 0x2000,      // Michel electron
        kPriEl  = 0x4000,      // primary electron
        kPriMu  = 0x8000       // primary muon
    };

	enum EVtxId
	{
		kNuNC  = 0x0010000, kNuCC = 0x0020000, kNuPri = 0x0040000,  // nu interaction type
		kNuE   = 0x0100000, kNuMu = 0x0200000, kNuTau = 0x0400000,  // nu flavor
		kHadr  = 0x1000000,       // hadronic inelastic scattering
		kPi0   = 0x2000000,       // pi0 produced in this vertex
		kDecay = 0x4000000,       // point of particle decay
		kConv  = 0x8000000,       // gamma conversion
		kElectronEnd = 0x10000000 // clear end of an electron
	};

    struct Config : public img::DataProviderAlg::Config
    {
	    using Name = fhicl::Name;
	    using Comment = fhicl::Comment;

		fhicl::Atom< art::InputTag > WireLabel {
			Name("WireLabel"),
			Comment("Tag of recob::Wire.")
		};

		fhicl::Atom< art::InputTag > HitLabel {
			Name("HitLabel"),
			Comment("Tag of recob::Hit.")
		};

		fhicl::Atom< art::InputTag > TrackLabel {
			Name("TrackLabel"),
			Comment("Tag of recob::Track.")
		};

		fhicl::Atom< art::InputTag > SimulationLabel {
			Name("SimulationLabel"),
			Comment("Tag of simulation producer.")
		};

		fhicl::Atom< bool > SaveVtxFlags {
			Name("SaveVtxFlags"),
			Comment("Include (or not) vertex info in PDG map.")
		};
		
		fhicl::Atom<unsigned int> AdcDelayTicks {
			Name("AdcDelayTicks"),
			Comment("ADC pulse peak delay in ticks (non-zero for not deconvoluted waveforms).")
		};
    };

	TrainingDataAlg(const fhicl::ParameterSet& pset) :
		TrainingDataAlg(fhicl::Table<Config>(pset, {})())
	{}

    TrainingDataAlg(const Config& config);

	virtual ~TrainingDataAlg(void);

	void reconfigure(const Config& config);

	bool setEventData(const art::Event& event,   // collect & downscale ADC's, charge deposits, pdg labels
		unsigned int view, unsigned int tpc, unsigned int cryo);

	bool setDataEventData(const art::Event& event,   // collect & downscale ADC's, charge deposits, pdg labels
		unsigned int view, unsigned int tpc, unsigned int cryo);


	bool findCrop(float max_e_cut, unsigned int & w0, unsigned int & w1, unsigned int & d0, unsigned int & d1) const;

	std::vector<float> const & wireEdep(size_t widx) const { return fWireDriftEdep[widx]; }
	std::vector<int> const & wirePdg(size_t widx) const { return fWireDriftPdg[widx]; }

protected:

	virtual void resizeView(size_t wires, size_t drifts) override;

private:

	struct WireDrift // used to find MCParticle start/end 2D projections
	{
		size_t Wire;
		int Drift;
		int TPC;
		int Cryo;
	};

	WireDrift getProjection(const TLorentzVector& tvec, unsigned int view) const;

	bool setWireEdepsAndLabels(
		std::vector<float> const & edeps,
		std::vector<int> const & pdgs,
		size_t wireIdx);

	void collectVtxFlags(
		std::unordered_map< size_t, std::unordered_map< int, int > > & wireToDriftToVtxFlags,
		const std::unordered_map< int, const simb::MCParticle* > & particleMap,
		unsigned int view) const;

    static float particleRange2(const simb::MCParticle & particle)
    {
        float dx = particle.EndX() - particle.Vx();
        float dy = particle.EndY() - particle.Vy();
        float dz = particle.EndZ() - particle.Vz();
        return dx*dx + dy*dy + dz*dz;
    }
    bool isElectronEnd(
        const simb::MCParticle & particle,
        const std::unordered_map< int, const simb::MCParticle* > & particleMap) const;

    bool isMuonDecaying(
        const simb::MCParticle & particle,
        const std::unordered_map< int, const simb::MCParticle* > & particleMap) const;

	std::vector< std::vector<float> > fWireDriftEdep;
	std::vector< std::vector<int> > fWireDriftPdg;

	art::InputTag fWireProducerLabel;
	art::InputTag fHitProducerLabel;
	art::InputTag fTrackModuleLabel;
	art::InputTag fSimulationProducerLabel;
	bool fSaveVtxFlags;

    unsigned int fAdcDelay;

    std::vector<size_t> fEventsPerBin;
};
// ------------------------------------------------------
// ------------------------------------------------------
// ------------------------------------------------------

#endif
